// ============================================================
//  mario.c — 超级马里奥 (C 版)
//
//  320x200x256 色 (模式 13h), 16x16 图块, 12 行 x 160 列关卡。
//  卷轴镜头 + 视差背景 + 平台跳跃物理 + 板栗仔敌人 + 金币/问号块/
//  水管/楼梯/旗杆 + HUD + PC 扬声器音效。
//
//  运行环境与俄罗斯方块相同: 游戏跑在 IRQ1 (键盘中断) 处理器内部,
//  按键轮询 8042; 定时器 IRQ0 (18.2 Hz) 嵌套投递提供滴答节奏;
//  8259A 的同优先级中断阻塞保证游戏期间不会被抢占式调度打断。
//
//  操作: 左右方向键/WASD 跑, 空格/上/W 跳, P 暂停, R 重开, ESC 退出
// ============================================================

#include "mario.h"
#include "graphics.h"
#include "timer.h"
#include "io.h"
#include "keyboard.h"
#include "types.h"

// ---------- 常量 ----------
#define TILE     16
#define ROWS     12
#define COLS     160
#define HUD_H    8
#define MW       12      // 马里奥碰撞盒
#define MH       16
#define TIME_START 500
#define GOOMBA_N 8
#define PARTICLE_N 28
#define FLAG_COL 155
#define CHECKPOINT_COL 76

// 定点数 (8.8): 物理量全部用 256 倍整数, 免浮点
#define FIX8(n) ((int)((n) * 256))
#define RUN_V    FIX8(2.5)    // 跑速 (px/滴答)
#define JUMP_V   FIX8(-7)     // 起跳初速 (最高约 58px = 3.6 格)
#define GRAV     FIX8(0.42)   // 重力加速度 (px/滴答²)
#define MAX_FALL FIX8(7)      // 下落限速
#define GOOMBA_V FIX8(0.5)    // 板栗仔速度

// ---------- 调色板 (0=透明, 15=白色给 gfx_putc 用) ----------
enum {
    COL_SKY = 1, COL_WHITE = 2, COL_HILL = 3,
    COL_GROUND = 4, COL_GROUND_D = 5,
    COL_BRICK = 6, COL_BRICK_L = 7,
    COL_QUES = 8, COL_QUES_L = 9,
    COL_USED = 10, COL_USED_L = 11,
    COL_PIPE = 12, COL_PIPE_L = 13, COL_PIPE_D = 14,
    COL_TEXT = 15,
    COL_COIN = 16, COL_COIN_D = 17,
    COL_RED = 18, COL_SKIN = 19, COL_HAIR = 20,
    COL_OVER = 21, COL_SHOE = 22,
    COL_GOOMBA = 23, COL_GOOMBA_L = 24, COL_FEET = 25,
    COL_RED_D = 26, COL_BLUE_L = 27, COL_GOLD_D = 28,
    COL_PANEL = 29, COL_PANEL_L = 30, COL_STAR = 31,
};

static void mario_palette(int stage) {
    gfx_set_color(COL_SKY, 36, 58, 63);
    gfx_set_color(COL_WHITE, 63, 63, 63);
    gfx_set_color(COL_HILL, 20, 48, 12);
    gfx_set_color(COL_GROUND, 60, 40, 12);
    gfx_set_color(COL_GROUND_D, 42, 26, 8);
    gfx_set_color(COL_BRICK, 48, 16, 6);
    gfx_set_color(COL_BRICK_L, 56, 26, 10);
    gfx_set_color(COL_QUES, 58, 44, 4);
    gfx_set_color(COL_QUES_L, 63, 55, 14);
    gfx_set_color(COL_USED, 42, 30, 14);
    gfx_set_color(COL_USED_L, 52, 38, 18);
    gfx_set_color(COL_PIPE, 16, 50, 16);
    gfx_set_color(COL_PIPE_L, 30, 62, 26);
    gfx_set_color(COL_PIPE_D, 6, 30, 8);
    gfx_set_color(COL_TEXT, 63, 63, 63);
    gfx_set_color(COL_COIN, 62, 50, 8);
    gfx_set_color(COL_COIN_D, 44, 34, 6);
    gfx_set_color(COL_RED, 56, 8, 8);
    gfx_set_color(COL_SKIN, 62, 44, 26);
    gfx_set_color(COL_HAIR, 28, 12, 4);
    gfx_set_color(COL_OVER, 4, 20, 56);
    gfx_set_color(COL_SHOE, 22, 10, 4);
    gfx_set_color(COL_GOOMBA, 46, 18, 10);
    gfx_set_color(COL_GOOMBA_L, 58, 30, 14);
    gfx_set_color(COL_FEET, 8, 6, 4);
    gfx_set_color(COL_RED_D, 36, 2, 4);
    gfx_set_color(COL_BLUE_L, 20, 38, 62);
    gfx_set_color(COL_GOLD_D, 42, 28, 2);
    gfx_set_color(COL_PANEL, 3, 7, 18);
    gfx_set_color(COL_PANEL_L, 16, 28, 52);
    gfx_set_color(COL_STAR, 63, 56, 22);
    if (stage == 1) {
        gfx_set_color(COL_SKY, 10, 10, 34);
        gfx_set_color(COL_HILL, 20, 18, 48);
        gfx_set_color(COL_GROUND, 48, 34, 30);
        gfx_set_color(COL_GROUND_D, 26, 20, 24);
        gfx_set_color(COL_BRICK, 34, 16, 30);
        gfx_set_color(COL_BRICK_L, 55, 26, 38);
        gfx_set_color(COL_PIPE, 28, 24, 54);
        gfx_set_color(COL_PIPE_L, 48, 42, 62);
        gfx_set_color(COL_PIPE_D, 12, 10, 30);
    }
}

// ---------- 关卡 (文本画, 每行 160 字符) ----------
//   . 空    # 地面    B 砖块    ? 问号块    C 金币
//   T R 水管顶(左/右)   L J 水管身(左/右)   | 旗杆
static const char* level_rom[ROWS] = {
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    "................................................................................................................................................................",
    ".....................CCC..........................................CCC...................................B....................CCC.......................................",
    "......................M.........................BBB..................................S.............B..............................?........................|....",
    "..................................................................................................BB.......................................................|....",
    "........................................TR.......................................................BBB..........TR...........................................|....",
    "........................................LJ....................TR................................BBBB..........LJ............................TR.............|....",
    "##############....############....######LJ#############....###LJ###################...########################LJ################...#########LJ#############|####",
};

// 图块编号 (level[][] 里存这些值)
enum {
    T_GROUND = 1, T_BRICK = 2, T_QUES = 3, T_USED = 4,
    T_PIPE_TL = 5, T_PIPE_TR = 6, T_PIPE_BL = 7, T_PIPE_BR = 8,
    T_POLE = 9, T_COIN = 10, T_QUES_M = 11, T_QUES_S = 12,
};

// ---------- 像素画 (字符 = 颜色, 见 mario_palette) ----------
//   . 透明  W 白  G 山绿  g/d 地面  b/B 砖  Q/q 问号块  U/u 用过的块
//   P/p/D 水管  C/c 金币  R 红  K 皮肤  H 头发  O 背带裤  h 鞋  M/m/F 板栗仔

// 地面 (16x16)
static const char* art_ground[16] = {
    "gggggggggggggggg",
    "gddddddddddddddg",
    "gddgdddgddgddddg",
    "gddddddgdddddgdd",
    "gddgddddddgddddg",
    "gddddgdddddddgdd",
    "gddgddddddgddddg",
    "gddddddgdddddddg",
    "gddgddddddddgddg",
    "gddddddgdddddddg",
    "gddgdddddddddddg",
    "gddddddgdddddddg",
    "gddgddddddgddddg",
    "gddddddddgdddddg",
    "gddddddddddddddg",
    "gggggggggggggggg",
};

// 砖块 (16x16)
static const char* art_brick[16] = {
    "BBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBB",
    "bbbbbbbbbbbbbbbb",
    "BBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBB",
    "bbbbbbbbbbbbbbbb",
    "BBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBB",
    "bbbbbbbbbbbbbbbb",
    "BBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBB",
    "bbbbbbbbbbbbbbbb",
    "BBBBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBBBB",
    "bbbbbbbbbbbbbbbb",
    "BBBBBBBBBBBBBBBB",
};

// 问号块 (16x16, U = 深色问号)
static const char* art_ques[16] = {
    "QQQQQQQQQQQQQQQQ",
    "QqqqqqqqqqqqqqqQ",
    "QqqqqqqqqqqqqqqQ",
    "QqqqqqqqqqqqqqqQ",
    "QqqqUUUUUqqqqqqQ",
    "QqqqqqqqqUqqqqqQ",
    "QqqqqqqqqUqqqqqQ",
    "QqqqqqqqUqqqqqqQ",
    "QqqqqqqUqqqqqqqQ",
    "QqqqqqUqqqqqqqqQ",
    "QqqqqqqqqqqqqqqQ",
    "QqqqqqUUqqqqqqqQ",
    "QqqqqqUUqqqqqqqQ",
    "QqqqqqqqqqqqqqqQ",
    "QqqqqqqqqqqqqqqQ",
    "QQQQQQQQQQQQQQQQ",
};

// 用过的块 (16x16)
static const char* art_used[16] = {
    "UUUUUUUUUUUUUUUU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UuuuuuuuuuuuuuuU",
    "UUUUUUUUUUUUUUUU",
};

// 水管左/右 (16x16, 左块带亮边, 右块带暗边)
static const char* art_pipe_l[16] = {
    "pppppppppppppppp",
    "pppppppppppppppp",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
    "ppPPPPPPPPPPPPDD",
};
static const char* art_pipe_r[16] = {
    "pppppppppppppppp",
    "pppppppppppppppp",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
    "DDPPPPPPPPPPPPDD",
};

// 旗杆 (16x16, 白色圆球 + 绿色杆)
static const char* art_pole[16] = {
    ".....WWWW.......",
    "...pWWWWWWp.....",
    "....pWWWWp......",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
    "......pp........",
};

// 马里奥 (12x16): 站立/走1/走2/跳
static const char* art_mario0[16] = {
    "....RRRR....",
    "...RRRRRR...",
    "...HHKKKHH..",
    "..HHKKKKKHH.",
    "..HHKKKKKHH.",
    "..RRRRRRRR..",
    "..RRRRRRRR..",
    "..OOOOOOOO..",
    "..OOOOOOOO..",
    "..OO....OO..",
    "..OO....OO..",
    "..OO....OO..",
    ".hhh....hhh.",
    ".hhh....hhh.",
    ".hhh....hhh.",
    "............",
};
static const char* art_mario1[16] = {
    "....RRRR....",
    "...RRRRRR...",
    "...HHKKKHH..",
    "..HHKKKKKHH.",
    "..HHKKKKKHH.",
    "..RRRRRRRR..",
    "..RRRRRRRR..",
    "..OOOOOOOO..",
    "..OOOOOOOO..",
    "...OO..OO...",
    "...OO..OO...",
    "..OOO..OOO..",
    ".hhh....hhh.",
    "hhh......hhh",
    "hhh......hhh",
    "............",
};
static const char* art_mario2[16] = {
    "....RRRR....",
    "...RRRRRR...",
    "...HHKKKHH..",
    "..HHKKKKKHH.",
    "..HHKKKKKHH.",
    "..RRRRRRRR..",
    "..RRRRRRRR..",
    "..OOOOOOOO..",
    ".OOO....OOO.",
    ".OOO....OOO.",
    ".hhh....hhh.",
    ".hhh....hhh.",
    "............",
    "............",
    "............",
    "............",
};

// 板栗仔 (16x16): 走1/走2
static const char* art_goomba0[16] = {
    "......MMMM......",
    "....MMMMMMMM....",
    "...MMMMMMMMMM...",
    "..MMMMMMMMMMMM..",
    "..MWWWWMWWWWM...",
    "..MWFFWMWFFWM...",
    ".MMMMMMMMMMMMMM.",
    ".MMMMMMMMMMMMMM.",
    "..MMMMMMMMMMMM..",
    "...MMMMMMMMMM...",
    "....MMMMMMMM....",
    "....mmmmmmmm....",
    "....FFFFFFFF....",
    "...FFFFFFFF.....",
    "................",
    "................",
};
static const char* art_goomba1[16] = {
    "......MMMM......",
    "....MMMMMMMM....",
    "...MMMMMMMMMM...",
    "..MMMMMMMMMMMM..",
    "..MWWWWMWWWWM...",
    "..MWFFWMWFFWM...",
    ".MMMMMMMMMMMMMM.",
    ".MMMMMMMMMMMMMM.",
    "..MMMMMMMMMMMM..",
    "...MMMMMMMMMM...",
    "....MMMMMMMM....",
    "....mmmmmmmm....",
    "..FFFFFFFF......",
    "..FFFFFFFF......",
    "................",
    "................",
};
// 被踩扁 (16x16, 内容在上 4 行, 画在 gy+8)
static const char* art_goomba2[16] = {
    "MMMMMMMMMMMMMMMM",
    "MMMMmmmmmmmmMMMM",
    "MMMMmmmmmmmmMMMM",
    "MMMMMMMMMMMMMMMM",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
};

// 金币 (8x8, 4 帧旋转动画)
static const char* art_coin0[8] = {
    ".CCCCCC.",
    "CCccccCC",
    "Cccccccc",
    "Cccccccc",
    "Cccccccc",
    "Cccccccc",
    "CCccccCC",
    ".CCCCCC.",
};
static const char* art_coin1[8] = {
    "...CC...",
    "..CCCC..",
    "..CccC..",
    "..CccC..",
    "..CccC..",
    "..CccC..",
    "..CCCC..",
    "...CC...",
};
static const char* art_coin2[8] = {
    "........",
    "...CC...",
    "...Cc...",
    "...Cc...",
    "...Cc...",
    "...Cc...",
    "...CC...",
    "........",
};

// 云 (24x8) 和 山 (24x12) — 视差背景
static const char* art_cloud[8] = {
    "......WWWW..............",
    "....WWWWWWWW............",
    "...WWWWWWWWWW...........",
    "..WWWWWWWWWWWWW.........",
    ".WWWWWWWWWWWWWWWW.......",
    "WWWWWWWWWWWWWWWWWWW.....",
    ".WWWWWWWWWWWWWWWWWW.....",
    "..WWWWWWWWWWWWWWWW......",
};
static const char* art_hill[12] = {
    "....GGGG................",
    "..GGGGGGGG..............",
    ".GGGGGGGGGG.............",
    ".GGGGGGGGGGGG...........",
    "GGGGGGGGGGGGGG..........",
    "GGGGGGGGGGGGGGG.........",
    "GGGGGGGGGGGGGGGG........",
    "GGGGGGGGGGGGGGGGG.......",
    "GGGGGGGGGGGGGGGGGG......",
    "GGGGGGGGGGGGGGGGGGG.....",
    "GGGGGGGGGGGGGGGGGGGG....",
    "GGGGGGGGGGGGGGGGGGGGG...",
};

// ---------- 状态 ----------
static void title_screen();   // 前置声明 (handle_key/update 会引用)
static void init_game();

static uint8_t level[ROWS][COLS];
static int mx, my;           // 马里奥位置 (8.8 定点)
static int vx, vy;
static int on_ground, face, mframe;
static int lives, score, coins, time_left, time_tick;
static int invuln, dead, gameover, won, win_timer, paused;
static int power, star_timer, coyote, jump_buffer;
static int combo, combo_timer;
static int stage, checkpoint_col, transition, title_select;
static int shake, message_timer;
static char message[28];
static int cam;              // 镜头 (px)
static int held_left, held_right, held_jump;
static int jump_pressed;     // 跳跃按下锁存 (瞬时按下+释放对也不丢)
static int frame_dirty;      // 状态有变化才重绘 (暂停后一帧收敛, 省 CPU)
static int quit;

typedef struct {
    int x, y;        // 8.8 定点
    int dir, vy;
    int alive, squashed, sq_timer;
} goomba_t;
static goomba_t goombas[GOOMBA_N];

typedef struct {
    int x, y, vx, vy, life;
    uint8_t color;
} particle_t;
static particle_t particles[PARTICLE_N];

// 转换后的像素画缓冲
static uint8_t tile_art[13][256];
static uint8_t mario_art[3][192];
static uint8_t goomba_art[3][256];
static uint8_t coin_art[4][64];
static uint8_t cloud_art[192];
static uint8_t hill_art[288];

// ---------- 字符 → 颜色 / 图块 ----------
static uint8_t art_color(char ch) {
    switch (ch) {
        case 'W': return COL_WHITE;
        case 'G': return COL_HILL;
        case 'g': return COL_GROUND;
        case 'd': return COL_GROUND_D;
        case 'b': return COL_BRICK;
        case 'B': return COL_BRICK_L;
        case 'Q': return COL_QUES;
        case 'q': return COL_QUES_L;
        case 'U': return COL_USED;
        case 'u': return COL_USED_L;
        case 'P': return COL_PIPE;
        case 'p': return COL_PIPE_L;
        case 'D': return COL_PIPE_D;
        case 'C': return COL_COIN;
        case 'c': return COL_COIN_D;
        case 'R': return COL_RED;
        case 'K': return COL_SKIN;
        case 'H': return COL_HAIR;
        case 'O': return COL_OVER;
        case 'h': return COL_SHOE;
        case 'M': return COL_GOOMBA;
        case 'm': return COL_GOOMBA_L;
        case 'F': return COL_FEET;
    }
    return 0;   // '.' 和未知 = 透明
}

static uint8_t level_char(char ch) {
    switch (ch) {
        case '#': return T_GROUND;
        case 'B': return T_BRICK;
        case '?': return T_QUES;
        case 'M': return T_QUES_M;
        case 'S': return T_QUES_S;
        case 'U': return T_USED;
        case 'T': return T_PIPE_TL;
        case 'R': return T_PIPE_TR;
        case 'L': return T_PIPE_BL;
        case 'J': return T_PIPE_BR;
        case '|': return T_POLE;
        case 'C': return T_COIN;
    }
    return 0;   // '.' = 空
}

static void art_load(const char* art[], int w, int h, uint8_t* out) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            out[r * w + c] = art_color(art[r][c]);
}

static void mario_init() {
    mario_palette(0);
    art_load(art_ground, 16, 16, tile_art[T_GROUND]);
    art_load(art_brick, 16, 16, tile_art[T_BRICK]);
    art_load(art_ques, 16, 16, tile_art[T_QUES]);
    art_load(art_ques, 16, 16, tile_art[T_QUES_M]);
    art_load(art_ques, 16, 16, tile_art[T_QUES_S]);
    art_load(art_used, 16, 16, tile_art[T_USED]);
    art_load(art_pipe_l, 16, 16, tile_art[T_PIPE_TL]);
    art_load(art_pipe_r, 16, 16, tile_art[T_PIPE_TR]);
    art_load(art_pipe_l, 16, 16, tile_art[T_PIPE_BL]);
    art_load(art_pipe_r, 16, 16, tile_art[T_PIPE_BR]);
    art_load(art_pole, 16, 16, tile_art[T_POLE]);
    art_load(art_mario0, 12, 16, mario_art[0]);
    art_load(art_mario1, 12, 16, mario_art[1]);
    art_load(art_mario2, 12, 16, mario_art[2]);
    art_load(art_goomba0, 16, 16, goomba_art[0]);
    art_load(art_goomba1, 16, 16, goomba_art[1]);
    art_load(art_goomba2, 16, 16, goomba_art[2]);
    art_load(art_coin0, 8, 8, coin_art[0]);
    art_load(art_coin1, 8, 8, coin_art[1]);
    art_load(art_coin2, 8, 8, coin_art[2]);
    art_load(art_coin1, 8, 8, coin_art[3]);
    art_load(art_cloud, 24, 8, cloud_art);
    art_load(art_hill, 24, 12, hill_art);
}

// ---------- 关卡/碰撞 ----------
static int tile_solid(int t) {
    return t == T_GROUND || t == T_BRICK || t == T_QUES ||
        t == T_QUES_M || t == T_QUES_S || t == T_USED
        || t == T_PIPE_TL || t == T_PIPE_TR || t == T_PIPE_BL || t == T_PIPE_BR
        || t == T_POLE;
}

static int tile_question(int t) {
    return t == T_QUES || t == T_QUES_M || t == T_QUES_S;
}

// 屏幕像素 (px,py) 处的图块是否实心 (越界: 左右=墙, 上下=开放)
static int solid_at(int px, int py) {
    int c = px >> 4;
    int r = (py - HUD_H) >> 4;
    if (c < 0 || c >= COLS) return 1;
    if (r < 0 || r >= ROWS) return 0;
    return tile_solid(level[r][c]);
}

// ---------- 游戏状态 ----------
static void spawn_burst(int x, int y, uint8_t color, int count);
static void set_message(const char* text, int ticks);

static void clear_dynamic_state(void) {
    for (int i = 0; i < PARTICLE_N; i++) particles[i].life = 0;
    shake = 0; message_timer = 0; message[0] = '\0';
}

static void load_stage(int index, int from_checkpoint, int keep_power) {
    stage = index;
    mario_palette(stage);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            level[r][c] = level_char(level_rom[r][c]);

    level[7][8] = T_QUES_M;
    if (stage == 1) {
        level[7][12] = T_QUES_S;
        for (int c = 34; c < 39; c++) level[11][c] = 0;
        for (int c = 100; c < 106; c++) level[11][c] = 0;
        for (int c = 38; c < 42; c++) level[8][c] = T_BRICK;
        for (int c = 38; c < 42; c += 2) level[6][c] = T_COIN;
        for (int c = 108; c < 113; c++) level[7][c] = T_BRICK;
    }

    int start_col = from_checkpoint ? checkpoint_col : 2;
    mx = start_col * TILE << 8;
    my = FIX8(168);
    vx = 0; vy = 0;
    on_ground = 0; face = 1; mframe = 0;
    time_left = TIME_START; time_tick = 2;
    invuln = 0; dead = 0; gameover = 0; won = 0; win_timer = 0; paused = 0;
    cam = 0;
    held_left = held_right = held_jump = 0;
    jump_pressed = 0; coyote = 0; jump_buffer = 0;
    combo = 0; combo_timer = 0; transition = 0;
    if (!keep_power) { power = 0; star_timer = 0; }
    clear_dynamic_state();

    static const int gcols0[GOOMBA_N] = { 26, 50, 66, 87, 104, 120, 132, 145 };
    static const int gcols1[GOOMBA_N] = { 22, 42, 58, 77, 90, 114, 131, 146 };
    const int* gcols = stage ? gcols1 : gcols0;
    for (int i = 0; i < GOOMBA_N; i++) {
        goombas[i].x = gcols[i] * TILE << 8;
        goombas[i].y = FIX8(168);
        goombas[i].dir = -1;
        goombas[i].vy = 0;
        goombas[i].alive = 1;
        goombas[i].squashed = 0;
        goombas[i].sq_timer = 0;
    }
}

static void init_game(void) {
    lives = 3; score = 0; coins = 0;
    checkpoint_col = 0; power = 0; star_timer = 0;
    load_stage(title_select, 0, 0);
}

static void retry_stage(void) {
    load_stage(stage, checkpoint_col != 0, 0);
}

static void next_stage(void) {
    checkpoint_col = 0;
    if (stage == 0) load_stage(1, 0, 1);
    else {
        won = 2;
        win_timer = 260;
    }
}

// ---------- 键盘 ----------
// 里程碑 11a: 从键盘环形缓冲取原始字节 (按下 <0x80, 释放 >=0x80),
// 无按键返回 -1。老版轮询 8042 (游戏跑在 IRQ1 处理链里的年代);
// 现在 IRQ1 处理器把扫描码推进环形缓冲, 这里消费。
static int mario_key() {
    return kbd_pop();
}

static void handle_key(int sc) {
    int press = !(sc & 0x80);
    uint8_t k = sc & 0x7F;

    if (k == 0x01 && press) { quit = 1; return; }              // ESC 退出
    if (gameover && press) { title_screen(); init_game(); return; }
    if (k == 0x13 && press) { retry_stage(); return; }         // R 重开当前关
    if (k == 0x19 && press) {                                  // P 暂停
        paused ^= 1;
        frame_dirty = 1;
        return;
    }

    if (k == 0x4B || k == 0x1E) held_left = press;             // 左 / A
    if (k == 0x4D || k == 0x20) held_right = press;            // 右 / D
    if (k == 0x39 || k == 0x48 || k == 0x11) {                 // 空格 / 上 / W
        held_jump = press;
        if (press) jump_pressed = 1;   // 锁存按下沿, 防止 press+release
    }                                  // 在同一轮轮询里被一起消费掉
}

// ---------- 物理 ----------
static void move_mario_x() {
    mx += vx;
    int px = mx >> 8, py = my >> 8;
    if (vx > 0) {
        int edge = px + MW - 1;
        for (int yy = py + 2; yy <= py + MH - 3; yy += 6) {
            if (solid_at(edge, yy)) {
                mx = ((edge >> 4) * TILE - MW) << 8;
                vx = 0;
                return;
            }
        }
    } else if (vx < 0) {
        int edge = px;
        for (int yy = py + 2; yy <= py + MH - 3; yy += 6) {
            if (solid_at(edge, yy)) {
                mx = (((edge >> 4) + 1) * TILE) << 8;
                vx = 0;
                return;
            }
        }
    }
}

static void spawn_burst(int x, int y, uint8_t color, int count) {
    for (int n = 0; n < count; n++) {
        for (int i = 0; i < PARTICLE_N; i++) {
            particle_t* p = &particles[i];
            if (p->life) continue;
            p->x = x;
            p->y = y;
            p->vx = ((n & 1) ? 1 : -1) * (32 + (n * 19) % 48);
            p->vy = -80 - (n * 23) % 96;
            p->life = 18 + (n * 7) % 18;
            p->color = color;
            break;
        }
    }
}

static void coin_reward(void) {
    coins++;
    score += 200;
    if (coins >= 100) {
        coins = 0;
        lives++;
        beep(2711);
    }
}

static void bump_block(int c, int r) {
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return;
    uint8_t* t = &level[r][c];
    if (tile_question(*t)) {
        int kind = *t;
        *t = T_USED;
        if (kind == T_QUES_M) {
            power = 1;
            set_message("SUPER CHARGE!", 90);
        } else if (kind == T_QUES_S) {
            star_timer = 420;
            set_message("STAR POWER!", 90);
        }
        else coin_reward();
        spawn_burst(c * TILE + 8, r * TILE + HUD_H + 4,
                    kind == T_QUES_S ? COL_STAR : COL_COIN, 7);
        shake = 5;
        beep(kind == T_QUES ? 1356 : 1808);
    } else if (*t == T_BRICK) {
        if (power) {
            *t = 0;
            score += 50;
            spawn_burst(c * TILE + 8, r * TILE + HUD_H + 8, COL_BRICK_L, 10);
            shake = 7;
            beep(1356);
        } else {
            shake = 3;
            beep(905);
        }
    }
}

static void move_mario_y() {
    my += vy;
    int px = mx >> 8, py = my >> 8;
    on_ground = 0;
    if (vy > 0) {
        int feet = py + MH - 1;
        // 【关键】探测脚底 +1px: 站立时重力每滴答把脚底推下 0.4px,
        // 若只探测脚底本身, 落地检测会隔滴答闪烁 → on_ground 抖动 →
        // 跳跃按下沿可能落在 og=0 的滴答上被丢弃
        for (int xx = px + 2; xx <= px + MW - 3; xx += 5) {
            if (solid_at(xx, feet + 1)) {
                int r = (feet + 1 - HUD_H) >> 4;
                my = (r * TILE + HUD_H - MH) << 8;
                vy = 0;
                on_ground = 1;
                return;
            }
        }
    } else if (vy < 0) {
        int head = py;
        for (int xx = px + 2; xx <= px + MW - 3; xx += 5) {
            if (solid_at(xx, head)) {
                int r = (head - HUD_H) >> 4;
                my = ((r + 1) * TILE + HUD_H) << 8;   // 头顶顶在块底
                bump_block(xx >> 4, r);
                vy = 0;
                return;
            }
        }
    }
}

static void collect_coins() {
    int px = mx >> 8, py = my >> 8;
    int c0 = px >> 4, c1 = (px + MW - 1) >> 4;
    int r0 = (py - HUD_H) >> 4, r1 = (py + MH - 1 - HUD_H) >> 4;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) continue;
            if (level[r][c] == T_COIN) {
                level[r][c] = 0;
                coin_reward();
                spawn_burst(c * TILE + 8, r * TILE + HUD_H + 8,
                            COL_COIN, 5);
                beep(1356);
            }
        }
}

static void start_death() {
    dead = 1;
    vy = FIX8(-6);       // 死亡跳起后坠落
    vx = 0;
    shake = 9;
    spawn_burst((mx >> 8) + 6, (my >> 8) + 8, COL_WHITE, 12);
    beep(10847);
}

static void hurt_mario(void) {
    if (star_timer || invuln) return;
    if (power) {
        power = 0;
        invuln = 100;
        shake = 8;
        spawn_burst((mx >> 8) + 6, (my >> 8) + 8, COL_PANEL_L, 12);
        beep(1808);
    } else {
        start_death();
    }
}

static void update_dead() {
    my += vy;
    vy += GRAV;
    if ((my >> 8) > 240) {          // 掉出屏幕: 扣命重来
        lives--;
        if (lives > 0) {
            retry_stage();
            invuln = 90;            // 重生短暂无敌
        } else {
            gameover = 1;
        }
    }
}

static void set_message(const char* text, int ticks) {
    int i = 0;
    while (text[i] && i < (int)sizeof(message) - 1) {
        message[i] = text[i];
        i++;
    }
    message[i] = '\0';
    message_timer = ticks;
}

static void update_particles(void) {
    for (int i = 0; i < PARTICLE_N; i++) {
        particle_t* p = &particles[i];
        if (!p->life) continue;
        p->x += p->vx >> 4;
        p->y += p->vy >> 4;
        p->vy += 14;
        p->life--;
    }
}

static void update_goombas() {
    int mpx = mx >> 8, mpy = my >> 8;
    for (int i = 0; i < GOOMBA_N; i++) {
        goomba_t* g = &goombas[i];
        if (!g->alive) continue;
        if (g->squashed) {
            if (--g->sq_timer <= 0) g->alive = 0;
            continue;
        }

        // 水平: 撞墙回头 (不检测脚下, 会自己走下悬崖)
        g->x += g->dir * GOOMBA_V;
        int gx = g->x >> 8, gy = g->y >> 8;
        if (g->dir < 0) {
            if (solid_at(gx, gy + 6) || solid_at(gx, gy + 13)) g->dir = 1;
        } else {
            if (solid_at(gx + 15, gy + 6) || solid_at(gx + 15, gy + 13)) g->dir = -1;
        }

        // 垂直: 重力 + 落地
        g->vy += GRAV;
        if (g->vy > MAX_FALL) g->vy = MAX_FALL;
        g->y += g->vy;
        gx = g->x >> 8; gy = g->y >> 8;
        if (g->vy > 0) {
            int feet = gy + 15;
            if (solid_at(gx + 3, feet + 1) || solid_at(gx + 12, feet + 1)) {
                int r = (feet + 1 - HUD_H) >> 4;
                g->y = (r * TILE + HUD_H - 16) << 8;
                g->vy = 0;
            }
        }
        if (gy > 240) { g->alive = 0; continue; }

        // 与马里奥的碰撞
        if (mpx + MW - 1 >= gx && mpx <= gx + 15 &&
            mpy + MH - 1 >= gy && mpy <= gy + 15) {
            if (star_timer) {
                g->squashed = 1;
                g->sq_timer = 25;
                score += 300;
                spawn_burst(gx + 8, gy + 8, COL_STAR, 9);
                beep(5423);
            } else if (vy > 0 && (mpy + MH - 1) <= gy + 9) {
                // 踩扁: 加分 + 反弹
                g->squashed = 1;
                g->sq_timer = 25;
                combo = combo_timer > 0 ? combo + 1 : 1;
                if (combo > 5) combo = 5;
                combo_timer = 100;
                score += 100 * combo;
                spawn_burst(gx + 8, gy + 8, COL_WHITE, 6);
                beep(5423);
                vy = FIX8(-4);
                on_ground = 0;
            } else {
                hurt_mario();
            }
        }
    }
}

static void update() {
    frame_dirty = 1;
    if (dead) { update_dead(); update_particles(); return; }
    if (gameover) return;
    if (won) {
        if (--win_timer <= 0) {
            if (won == 1) {
                won = 0;
                next_stage();
            } else {
                title_screen();
                init_game();
            }
        }
        return;
    }

    // 倒计时
    if (--time_tick <= 0) {
        time_tick = 2;
        if (--time_left <= 0) { start_death(); return; }
    }
    if (invuln > 0) invuln--;
    if (star_timer > 0) star_timer--;
    if (combo_timer > 0) combo_timer--;
    if (message_timer > 0) message_timer--;
    if (shake > 0) shake--;

    // 输入 → 速度
    if (held_left && !held_right) { vx = -RUN_V; face = -1; }
    else if (held_right && !held_left) { vx = RUN_V; face = 1; }
    else vx = 0;

    // 跳跃缓冲 + coyote time: 更宽容的起跳窗口。
    if (jump_pressed) jump_buffer = 7;
    if (on_ground) coyote = 6;
    else if (coyote > 0) coyote--;
    if (jump_buffer > 0 && coyote > 0) {
        vy = JUMP_V;
        on_ground = 0;
        coyote = 0;
        jump_buffer = 0;
        spawn_burst((mx >> 8) + 4, (my >> 8) + MH, COL_WHITE, 3);
        beep(1808);
    } else if (jump_buffer > 0) {
        jump_buffer--;
    }
    jump_pressed = 0;

    // 按住跳键获得稍高的上升段，形成可变跳跃高度。
    vy += (held_jump && vy < 0) ? GRAV - FIX8(0.08) : GRAV;
    if (vy > MAX_FALL) vy = MAX_FALL;

    // 物理 + 交互
    move_mario_x();
    move_mario_y();
    collect_coins();
    update_goombas();
    update_particles();

    if (on_ground && combo_timer == 0) combo = 0;

    // 中途检查点：死亡后不会永远退回关卡开头。
    if (!checkpoint_col &&
        (mx >> 8) >= CHECKPOINT_COL * TILE) {
        checkpoint_col = CHECKPOINT_COL;
        score += 500;
        set_message("CHECKPOINT!", 100);
        spawn_burst((mx >> 8) + 6, (my >> 8) + 8, COL_STAR, 12);
        beep(2711);
    }

    // 旗杆胜利
    if ((mx >> 8) + MW - 1 >= FLAG_COL * TILE) {
        won = 1;
        win_timer = 190;
        score += time_left * 20;
        spawn_burst((mx >> 8) + 6, (my >> 8) + 8, COL_STAR, 24);
        beep(2711);
        return;
    }
    // 掉进深渊
    if ((my >> 8) > 240) { start_death(); return; }

    // 动画帧
    if (!on_ground) mframe = 2;
    else if (vx != 0) mframe = (g_ticks & 8) ? 1 : 0;
    else mframe = 0;

    // 镜头平滑跟随
    int target = (mx >> 8) - 130;
    if (target < 0) target = 0;
    if (target > COLS * TILE - GFX_WIDTH) target = COLS * TILE - GFX_WIDTH;
    cam += (target - cam) / 3;
}

// ---------- 渲染 ----------
static void hud_num(int x, int y, int v, int digits) {
    gfx_fillrect(x, y, digits * 8, HUD_H, COL_PANEL);
    char buf[8];
    int n = 0;
    do { buf[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n < digits) buf[n++] = '0';
    gfx_set_cursor(x, y);
    for (int i = n - 1; i >= 0; i--) gfx_putc(buf[i]);
}

static void draw_hud() {
    gfx_fillrect(0, 0, GFX_WIDTH, HUD_H, COL_PANEL);
    gfx_fillrect(0, HUD_H - 1, GFX_WIDTH, 1, COL_PANEL_L);
    gfx_set_cursor(0, 0); gfx_puts("M");
    hud_num(8, 0, score, 6);
    gfx_set_cursor(9 * 8, 0); gfx_puts(" C");
    hud_num(11 * 8, 0, coins, 2);
    gfx_set_cursor(14 * 8, 0); gfx_puts(" W");
    hud_num(16 * 8, 0, stage + 1, 1);
    gfx_set_cursor(17 * 8, 0); gfx_puts("-1");
    gfx_set_cursor(20 * 8, 0); gfx_puts(" T");
    hud_num(22 * 8, 0, time_left, 3);
    gfx_set_cursor(26 * 8, 0); gfx_puts(" L");
    hud_num(28 * 8, 0, lives, 1);
    gfx_set_cursor(30 * 8, 0); gfx_puts(" PWR");
    if (power || star_timer) {
        gfx_fillrect(34 * 8, 1, 6, 6,
                     star_timer ? COL_STAR : COL_RED_D);
    }
}

static void center_text(const char* s, int row) {
    int len = 0;
    while (s[len]) len++;
    gfx_set_cursor((GFX_WIDTH - len * 8) / 2, row * 8);
    gfx_puts(s);
}

static void draw_bg() {
    if (stage == 0) {
        gfx_fillrect(252 - (cam >> 4), 20, 28, 20, COL_QUES);
        gfx_fillrect(256 - (cam >> 4), 16, 20, 28, COL_QUES);
        gfx_fillrect(260 - (cam >> 4), 20, 12, 20, COL_QUES_L);
    } else {
        for (int i = 0; i < 24; i++) {
            int x = i * 67 - ((cam >> 3) % 1609);
            if (x < -2) x += 1609;
            if (x >= 0 && x < GFX_WIDTH && (((g_ticks >> 4) + i) & 3))
                gfx_fillrect(x, 12 + (i * 37) % 112, 2, 2, COL_STAR);
        }
        gfx_fillrect(252 - (cam >> 4), 22, 24, 24, COL_WHITE);
        gfx_fillrect(260 - (cam >> 4), 20, 12, 24, COL_PANEL_L);
    }

    int off_c = (cam / 3) % 660;         // 云: 1/3 视差
    for (int i = -1; i < 3; i++) {
        int x = i * 220 + 60 - off_c;
        if (x > -30 && x < GFX_WIDTH) {
            gfx_blit(x, 24 + (i & 1) * 22, 24, 8, cloud_art);
        }
    }
    int off_h = (cam / 2) % 900;         // 山: 1/2 视差
    for (int i = -1; i < 2; i++) {
        int x = i * 300 + 30 - off_h;
        if (x > -30 && x < GFX_WIDTH) {
            gfx_blit(x, 156, 24, 12, hill_art);
        }
    }
    int off_b = (cam / 2) % 520;
    for (int i = -1; i < 2; i++) {
        int x = i * 260 + 120 - off_b;
        if (x > -40 && x < GFX_WIDTH) {
            gfx_fillrect(x, 176, 32, 8, COL_HILL);
            gfx_fillrect(x + 6, 170, 8, 8, COL_HILL);
            gfx_fillrect(x + 18, 172, 7, 6, COL_HILL);
        }
    }
}

static void draw_particles(void) {
    for (int i = 0; i < PARTICLE_N; i++) {
        particle_t* p = &particles[i];
        if (!p->life) continue;
        int size = p->life > 10 ? 3 : 2;
        gfx_fillrect(p->x - cam, p->y, size, size, p->color);
    }
}

static void draw_center_panel(const char* title, const char* subtitle) {
    gfx_fillrect(58, 72, 204, 56, COL_PANEL);
    gfx_fillrect(58, 72, 204, 2, COL_STAR);
    gfx_fillrect(58, 126, 204, 2, COL_PANEL_L);
    center_text(title, 10);
    if (subtitle) center_text(subtitle, 13);
}

static void render() {
    gfx_clear(COL_SKY);
    draw_bg();
    int jitter = shake ? ((g_ticks & 1) ? 2 : -2) : 0;

    // 图块
    int c0 = cam >> 4;
    for (int c = c0; c <= c0 + 20; c++) {
        for (int r = 0; r < ROWS; r++) {
            uint8_t t = level[r][c];
            int sx = c * TILE - cam + jitter;
            int sy = HUD_H + r * TILE;
            if (t == 0) continue;
            if (t == T_COIN) {          // 金币: 旋转动画
                gfx_blit(sx + 4, sy + 4, 8, 8, coin_art[(g_ticks >> 3) & 3]);
                continue;
            }
            gfx_blit(sx, sy, 16, 16, tile_art[t]);
        }
    }

    // 板栗仔
    for (int i = 0; i < GOOMBA_N; i++) {
        goomba_t* g = &goombas[i];
        if (!g->alive) continue;
        int sx = (g->x >> 8) - cam + jitter;
        int sy = g->y >> 8;
        if (g->squashed) {
            gfx_blit(sx, sy + 8, 16, 16, goomba_art[2]);
        } else {
            gfx_blit(sx, sy, 16, 16, goomba_art[(g_ticks >> 3) & 1]);
        }
    }

    // 马里奥 (无敌时闪烁)
    if (!(invuln && (g_ticks & 2))) {
        int sx = (mx >> 8) - cam + jitter;
        int sy = my >> 8;
        if (power) {
            gfx_fillrect(sx - 2, sy + 3, 16, 4, COL_RED_D);
            gfx_fillrect(sx, sy + 7, 12, 2, COL_RED);
        }
        if (star_timer && ((g_ticks >> 1) & 1)) {
            gfx_fillrect(sx - 3, sy + 2, 2, 2, COL_STAR);
            gfx_fillrect(sx + 13, sy + 6, 2, 2, COL_WHITE);
            gfx_fillrect(sx + 10, sy - 2, 2, 2, COL_STAR);
        }
        gfx_blit(sx, sy, 12, 16, mario_art[mframe]);
    }

    draw_particles();
    draw_hud();

    if (message_timer > 0) {
        gfx_fillrect(74, 32, 172, 11, COL_PANEL);
        center_text(message, 4);
    }
    if (paused) {
        draw_center_panel("PAUSED", "P TO RESUME");
    }
    if (gameover) {
        draw_center_panel("GAME OVER", "PRESS ANY KEY");
    }
    if (won) {
        const char* sub = stage == 0 ? "NEXT: STARRY NIGHT" :
                          "THANK YOU FOR PLAYING";
        draw_center_panel(won == 1 ? "COURSE CLEAR!" : "WORLD COMPLETE",
                          sub);
    }
}

// ---------- 标题画面 ----------
static void title_screen() {
    stage = 0;
    mario_palette(0);
    gfx_clear(COL_SKY);
    draw_bg();
    gfx_fillrect(50, 10, 220, 80, COL_PANEL);
    gfx_fillrect(50, 10, 220, 2, COL_QUES_L);
    gfx_fillrect(50, 88, 220, 2, COL_PANEL_L);
    gfx_fillrect(8, 156, 48, 8, COL_HILL);
    gfx_fillrect(12, 148, 10, 10, COL_HILL);
    gfx_fillrect(34, 150, 9, 8, COL_HILL);
    for (int x = 0; x < GFX_WIDTH; x += 16) {
        gfx_blit(x, 184, 16, 16, tile_art[T_GROUND]);
    }
    gfx_blit(118, 168, 12, 16, mario_art[0]);
    gfx_blit(192, 168, 16, 16, goomba_art[0]);
    gfx_fillrect(150, 156, 8, 8, COL_COIN);
    gfx_fillrect(154, 154, 2, 12, COL_COIN);

    gfx_set_cursor(15 * 8, 3 * 8); gfx_puts("SUPER MARIO");
    gfx_set_cursor(12 * 8, 5 * 8); gfx_puts("STARRY NIGHT EDITION");
    gfx_set_cursor(8 * 8, 8 * 8);  gfx_puts("RUN: ARROWS / WASD");
    gfx_set_cursor(8 * 8, 10 * 8); gfx_puts("JUMP: SPACE / UP / W");
    center_text("1 DAYLIGHT  2 STARRY NIGHT", 12);
    center_text("P PAUSE  R RETRY", 14);
    center_text("PRESS 1/2 OR ANY KEY", 17);

    // 等一个"按下"事件 (释放码不算)
    title_select = 0;
    while (1) {
        int k = mario_key();
        if (k >= 0 && k < 0x80) {
            if (k == 0x03) title_select = 1;
            break;
        }
        __asm__ volatile("hlt");
    }
}

// ---------- 主循环 ----------
void mario_run() {
    mario_init();
    title_screen();
    init_game();
    quit = 0;

    uint32_t last = g_ticks;
    while (!quit) {
        int sc;
        while ((sc = mario_key()) >= 0) handle_key(sc);

        // 【关键】渲染比滴答慢: 主循环可能跟不上 18 Hz, 漏掉的滴答
        // 在这里全部补上, 保证游戏时间始终按真实滴答推进
        while (g_ticks != last) {
            last++;
            if (!paused) update();
        }
        if (frame_dirty) {
            frame_dirty = 0;
            render();
        }
        __asm__ volatile("hlt");   // 睡到下一个中断, 省 CPU
    }
}
