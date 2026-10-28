// ============================================================
//  tetris.c — 俄罗斯方块 (C 移植版)
//
//  移植自 tetris/tetris.asm (1435 行实模式汇编版), 游戏逻辑 1:1 对照:
//   - 7-bag 随机 (Fisher-Yates 洗牌 + 16 位 LCG)
//   - 简易 wall kick (偏移 0, +1, -1, +2, -2)
//   - 锁定延迟 LOCK_DELAY=7 滴答, 最多重置 MAX_RESETS=15 次
//   - 重力间隔 = 11 - 等级 滴答 (1 滴答 ≈ 1/18.2 秒)
//   - 消行闪烁动画、5 套主题配色、PC 扬声器音效
//
//  和保护模式内核适配 (原版依赖的 BIOS 服务的替代品):
//   - INT 1Ah 时钟  → PIT IRQ0 滴答 (timer.h 的 g_ticks)
//   - INT 16h 键盘  → 直接轮询 8042 (game_key)
//   - INT 10h 文字  → graphics.c 的 8x8 点阵字体
//   - INT 10h 显示  → graphics.c 直接写帧缓冲 0xA0000
//
//  行为改动: 原版 ESC 通过 8042 复位整机, 这里改为退出游戏回到 Shell
//
//  ★ 架构要点: 本游戏整个运行在 IRQ1 (键盘中断) 处理器内部 —
//  Shell 的命令分发是在 keyboard_handler 里完成的。8259A 不会再次
//  投递同优先级的 IRQ1, 所以游戏内按键必须轮询 8042 硬件 (和
//  testmode 的"按任意键"同样的模式); 而 IRQ0 (定时器) 优先级更高,
//  仍会正常嵌套投递, hlt 睡眠 + 滴答计时的游戏循环得以成立。
// ============================================================

#include "tetris.h"
#include "graphics.h"
#include "timer.h"
#include "io.h"
#include "types.h"

// ---------- 常量 (与原版一致) ----------
#define BOARD_W   10
#define BOARD_H   20
#define BOARD_X   120
#define BOARD_Y   20
#define LOCK_DELAY   7
#define MAX_RESETS  15

// ---------- 游戏状态 ----------
static uint8_t board[BOARD_W * BOARD_H];
static int cur_piece, cur_rot, cur_x, cur_y;
static int t_x, t_y, t_rot;          // 试探位置 (碰撞检测用)
static int next_piece;
static uint32_t score;
static int lines, level;
static int paused, gameover;
static int dirty;
static uint32_t last_tick;
static uint16_t seed = 0x1234;
static uint32_t hiscore;
static uint8_t fullrow[BOARD_H];
static int flash_n, flash_mode;
static int grounded, reset_req, lock_resets;
static uint32_t lock_start;
static int hold_piece, hold_used;
static int mute;
static int quit;                     // ESC 退出标记
static int pcolor;                   // draw_shape 用的颜色

// 7 种方块 x 4 旋转, 每项 16 位 = 4 行 x 4 列位图
// (bit15 = 第 0 行第 0 列, 逐行向低位排列; 和原版 pieces 表一致)
static const uint16_t pieces[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },  // I
    { 0x6600, 0x6600, 0x6600, 0x6600 },  // O
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },  // T
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },  // S
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },  // Z
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },  // J
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },  // L
};

// 棋盘值 (0=空, 1..7=方块号+1) -> VGA 调色板下标
// (8 = 影子; 17..23 = 方块 1..7 的暗色版, 落地锁定计时时使用)
static const uint8_t colors[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 17, 18, 19, 20, 21, 22, 23 };
static const char kicks[] = { 0, 1, -1, 2, -2 };

// 5 套方块主题, 每套 8 种颜色 (I O T S Z J L 影子) x RGB (0..63)
static const uint8_t themes[5][24] = {
    {  0,56,56,  63,56, 0,  50,12,50,  12,56,12,  60,12,12,  16,24,63,  63,38, 6,  14,14,14 },  // 经典鲜艳
    { 35,58,63,  63,60,38,  55,38,60,  40,60,40,  63,40,40,  42,48,63,  63,50,36,  18,18,22 },  // 粉彩
    {  0,63,45,  63,63, 0,  63, 0,48,  30,63, 0,  63, 8,20,  24,12,63,  63,32, 0,  14,10,18 },  // 霓虹
    { 20,50,63,  30,58,63,  40,40,63,  10,45,55,  30,30,60,  10,20,50,  45,55,63,  10,14,20 },  // 冷色
    { 63,50,10,  63,63,30,  63,25, 5,  55,40, 0,  63, 8, 0,  45,10, 5,  63,30,20,  20,10,10 },  // 火焰
};

// 10 个等级的边框颜色 (R,G,B)
static const uint8_t border_tbl[10][3] = {
    {32,32,32}, {20,40,63}, { 0,55,45}, {30,60,20}, {63,60, 0},
    {63,40, 0}, {63,20,10}, {63,10,30}, {45,10,63}, {63,63,63},
};

static const uint16_t score_tbl[] = { 0, 100, 300, 500, 800 };   // 1/2/3/4 行消行基础分

// 7-bag 袋子
static uint8_t bag[7];
static int bag_pos;

// ---------- 前置声明 (函数间有互相调用) ----------
static void lock_and_next();
static void flash_lines();
static void clear_lines();
static void spawn();
static void spawn_cur();
static void apply_palette();
static void do_hold();
static void init_game();
static void draw_board();
static void draw_ghost();
static void draw_piece();
static void draw_next();
static void draw_hold();
static void draw_stats();

// ---------- 随机数 (与原版 16 位 LCG 一致) ----------
static uint8_t random_mod(int n) {
    seed = (uint16_t)((uint32_t)seed * 25173 + 13849);
    return (uint8_t)((seed >> 8) % n);   // 取高字节 (低位随机性较差)
}

// 装满 0..6 并 Fisher-Yates 洗牌
static void refill_bag() {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i >= 1; i--) {
        int j = random_mod(i + 1);
        uint8_t t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bag_pos = 0;
}

// 7-bag: 取下一个方块号, 袋子用完就重新装满并洗牌
static uint8_t bag_next() {
    if (bag_pos >= 7) refill_bag();
    return bag[bag_pos++];
}

static uint16_t get_mask(int piece, int rot) {
    return pieces[piece][rot];
}

// ---------- 碰撞检测 ----------
// 用 (t_x, t_y, t_rot) 摆放 cur_piece, 返回 1 = 撞了 (越界或压到已有方块)
static int collide() {
    uint16_t mask = get_mask(cur_piece, t_rot);
    for (int i = 0; i < 16; i++) {
        if (mask & 0x8000) {
            int col = t_x + (i & 3);
            int row = t_y + (i >> 2);
            // 无符号比较: 负数 (左边出界) 会被当成很大的数 → 也算越界
            if ((unsigned)col >= BOARD_W || (unsigned)row >= BOARD_H) return 1;
            if (board[row * BOARD_W + col]) return 1;
        }
        mask <<= 1;
    }
    return 0;
}

// 把当前方块写进棋盘 (值 = 方块号+1, 0 表示空)
static void lock_piece() {
    uint16_t mask = get_mask(cur_piece, cur_rot);
    for (int i = 0; i < 16; i++) {
        if (mask & 0x8000) {
            int col = cur_x + (i & 3);
            int row = cur_y + (i >> 2);
            board[row * BOARD_W + col] = cur_piece + 1;
        }
        mask <<= 1;
    }
}

// 尝试平移. 返回 1 = 成功, 0 = 被挡
static int try_move(int dx, int dy) {
    t_x = cur_x + dx;
    t_y = cur_y + dy;
    t_rot = cur_rot;
    if (collide()) return 0;
    cur_x = t_x;
    cur_y = t_y;
    dirty = 1;
    reset_req = 1;   // 成功移动过: 若已落地, 锁定计时可以重置
    return 1;
}

// 旋转 (顺时针). 撞墙时依次尝试左右偏移 0, +1, -1, +2, -2 (简易 wall kick)
static void rotate_piece() {
    int new_rot = (cur_rot + 1) & 3;
    for (int i = 0; i < 5; i++) {
        t_rot = new_rot;
        t_x = cur_x + kicks[i];
        t_y = cur_y;
        if (!collide()) {
            cur_rot = new_rot;
            cur_x = t_x;
            dirty = 1;
            reset_req = 1;   // 旋转成功也请求重置
            return;
        }
    }
}

// 直落到底并锁定
static void hard_drop() {
    while (try_move(0, 1)) score += 2;
    lock_and_next();
}

// 定时下落一格, 落不下去也不立刻锁定, 交给 lock_check 的计时
static void gravity() {
    try_move(0, 1);
}

// 脚下是否已经没有空间? 1 = 方块已落地 (再往下一格就会碰撞)
static int on_ground() {
    t_x = cur_x;
    t_y = cur_y + 1;
    t_rot = cur_rot;
    return collide();
}

// 锁定延迟:
//   刚落地        -> 开始计时
//   落地后移动/旋转 -> 重置计时 (每块最多 MAX_RESETS 次)
//   计时满 LOCK_DELAY -> 真正锁定
//   脚下又有空间    -> 取消落地状态, 继续受重力
static void lock_check() {
    if (on_ground()) {
        if (!grounded) {
            grounded = 1;              // 刚刚落地: 变暗
            dirty = 1;
            lock_start = g_ticks;
        } else if (reset_req && lock_resets < MAX_RESETS) {
            lock_resets++;             // 最后一刻滑动: 重新计时
            lock_start = g_ticks;
        } else if (g_ticks - lock_start >= LOCK_DELAY) {
            lock_and_next();
        }
    } else if (grounded) {
        grounded = 0;                  // 脚下又有空间: 恢复亮色
        dirty = 1;
    }
    reset_req = 0;
}

// 锁定后: 消行、刷分、生新块
static void lock_and_next() {
    lock_piece();
    beep(5423);      // 220 Hz: 落地
    flash_lines();   // 有满行就先闪烁
    clear_lines();
    spawn();
    hold_used = 0;   // 新方块出现, 允许再次暂存
}

// ---------- 游戏流程 ----------

// 生成新方块 (当前 = next, 再随机一个 next); 出生点就碰撞则游戏结束
static void spawn() {
    cur_piece = next_piece;
    next_piece = bag_next();
    spawn_cur();
}

// 把 cur_piece 摆到出生点 (暂存换出时也走这里)
static void spawn_cur() {
    grounded = 0;      // 新方块: 清掉锁定相关状态
    lock_resets = 0;
    reset_req = 0;
    cur_rot = 0;
    cur_x = 3;
    cur_y = 0;
    t_x = 3;
    t_y = 0;
    t_rot = 0;
    if (collide()) {
        gameover = 1;
        beep(10847);   // 110 Hz 低音: 游戏结束
    }
    dirty = 1;
}

static void init_game() {
    for (int i = 0; i < BOARD_W * BOARD_H; i++) board[i] = 0;
    score = 0;
    lines = 0;
    paused = 0;
    gameover = 0;
    level = 1;
    hold_piece = 0xFF;   // 0xFF = 暂存区为空
    hold_used = 0;
    seed ^= g_ticks;     // 用时钟计数做随机数种子
    last_tick = g_ticks;
    bag_pos = 7;         // 袋子空, 第一次取牌时会先装袋
    next_piece = bag_next();
    spawn();
    apply_palette();     // 按等级 1 设置配色
    dirty = 1;
}

// ---------- 键盘 ----------

// 轮询 8042 取一个"按下"事件, 返回 set 1 扫描码; 无按键/释放事件返回 -1
// (为什么轮询而不是键盘中断: 见文件头部"架构要点")
static int game_key() {
    if (!(inb(0x64) & 1)) return -1;    // 状态端口 bit0 = 输出缓冲区有数据
    uint8_t sc = inb(0x60);
    if (sc & 0x80) return -1;           // 释放事件: 读走但不处理
    return sc;
}

// 按键处理 (扫描码和原版 handle_key 一致)
static void handle_key(uint8_t key) {
    if (key == 0x01) { quit = 1; return; }          // ESC: 退出游戏 (原版是复位整机)
    if (key == 0x13) { init_game(); return; }       // R: 重新开始
    if (key == 0x32) { mute ^= 1; return; }         // M: 静音开关
    if (gameover) return;
    if (key == 0x19) { paused ^= 1; dirty = 1; return; }  // P: 暂停
    if (paused) return;
    if (key == 0x2E) { do_hold(); return; }         // C: 暂存
    if (key == 0x4B || key == 0x1E) try_move(-1, 0);           // 左 / A
    else if (key == 0x4D || key == 0x20) try_move(1, 0);       // 右 / D
    else if (key == 0x48 || key == 0x11) rotate_piece();       // 上 / W: 旋转
    else if (key == 0x50 || key == 0x1F) {                     // 下 / S: 软降
        if (try_move(0, 1)) score += 1;                        // 软降每格 +1 分
    }
    else if (key == 0x39) hard_drop();                         // 空格: 直落
}

// ---------- 消行 ----------

// 从最下面一行往上扫, 满行就把上面整体下移一行, 然后重查同一行
static void clear_lines() {
    int n = 0;
    int row = BOARD_H - 1;
    while (row >= 0) {
        int full = 1;
        for (int c = 0; c < BOARD_W; c++) {
            if (!board[row * BOARD_W + c]) { full = 0; break; }
        }
        if (!full) { row--; continue; }
        n++;
        // 上面整体下移一行 (从下往上复制, 避免覆盖)
        for (int r = row; r >= 1; r--) {
            for (int c = 0; c < BOARD_W; c++) {
                board[r * BOARD_W + c] = board[(r - 1) * BOARD_W + c];
            }
        }
        for (int c = 0; c < BOARD_W; c++) board[c] = 0;   // 最上面一行清零
        // 同一行重新检查
    }
    if (n) {
        lines += n;
        score += score_tbl[n] * level;
        int new_level = lines / 10 + 1;
        if (new_level > 10) new_level = 10;   // 等级上限 10
        if (new_level != level) {
            level = new_level;
            apply_palette();   // 升级: 换一套配色和边框色
            beep(905);         // 1318 Hz: 升级提示音
        }
    }
}

// 暂存 / 换出
static void do_hold() {
    if (hold_used) return;
    hold_used = 1;
    int old = hold_piece;
    hold_piece = cur_piece;
    if (old == 0xFF) {
        spawn();               // 暂存区为空: 取下一个
    } else {
        cur_piece = old;
        spawn_cur();           // 换出暂存的方块
    }
    beep(1808);                // 660 Hz
    dirty = 1;
}

// ---------- 绘图 ----------

// 画一个格子 (7x7, 留 1 像素缝). col/row 是相对棋盘左上角的格子坐标
static void draw_cell(int col, int row, uint8_t val) {
    gfx_fillrect(BOARD_X + col * 8, BOARD_Y + row * 8, 7, 7, colors[val]);
}

// 在 40x25 文本网格的 (col,row) 处画字符串 (每格 8x8 像素)
static void put_str(int col, int row, const char* s) {
    gfx_set_cursor(col * 8, row * 8);
    gfx_puts(s);
}

// 画棋盘边框 + 界面文字
static void draw_frame() {
    gfx_fillrect(BOARD_X - 2, BOARD_Y - 2, 84, 2, 9);         // 上边
    gfx_fillrect(BOARD_X - 2, BOARD_Y + 160, 84, 2, 9);       // 下边
    gfx_fillrect(BOARD_X - 2, BOARD_Y - 2, 2, 164, 9);        // 左边
    gfx_fillrect(BOARD_X + 80, BOARD_Y - 2, 2, 164, 9);       // 右边

    put_str(2, 4, "SCORE");
    put_str(2, 7, "LINES");
    put_str(2, 10, "LEVEL");
    put_str(2, 17, "BEST");
    put_str(27, 3, "NEXT");
    put_str(27, 10, "HOLD");
    put_str(26, 14, "ARROWS/WASD");
    put_str(26, 15, "UP: ROTATE");
    put_str(26, 16, "SPACE: DROP");
    put_str(26, 17, "P: PAUSE");
    put_str(26, 18, "ESC: EXIT");
    put_str(26, 19, "C: HOLD");
    put_str(26, 20, "M: SOUND");
}

// 重画整个游戏画面
static void draw_all() {
    draw_board();
    if (!gameover) {
        draw_ghost();   // 先画影子, 再画真方块盖在上面
        draw_piece();
    }
    draw_next();
    draw_hold();
    draw_stats();
}

static void draw_board() {
    for (int r = 0; r < BOARD_H; r++) {
        for (int c = 0; c < BOARD_W; c++) {
            draw_cell(c, r, board[r * BOARD_W + c]);
        }
    }
}

// 按 t_x, t_y, t_rot 位置、pcolor 颜色画出当前方块形状
static void draw_shape() {
    uint16_t mask = get_mask(cur_piece, t_rot);
    for (int i = 0; i < 16; i++) {
        if (mask & 0x8000) draw_cell(t_x + (i & 3), t_y + (i >> 2), pcolor);
        mask <<= 1;
    }
}

// 画正在下落的方块 (落地锁定计时中用暗色版, 提示"快锁定了")
static void draw_piece() {
    t_x = cur_x;
    t_y = cur_y;
    t_rot = cur_rot;
    pcolor = cur_piece + 1 + (grounded ? 8 : 0);
    draw_shape();
}

// 画影子: 从当前位置一直往下试, 找到最后一个不碰撞的 y
static void draw_ghost() {
    t_x = cur_x;
    t_y = cur_y;
    t_rot = cur_rot;
    while (1) {
        t_y++;
        if (collide()) { t_y--; break; }
    }
    pcolor = 8;   // 颜色表第 8 项 = 暗灰
    draw_shape();
}

// 右侧"下一个"预览 (格子坐标落在棋盘右边: 列 13.., 行 3..)
static void draw_next() {
    gfx_fillrect(216, 36, 48, 40, GFX_BLACK);   // 先擦掉旧的
    uint16_t mask = get_mask(next_piece, 0);
    for (int i = 0; i < 16; i++) {
        if (mask & 0x8000) draw_cell(13 + (i & 3), 3 + (i >> 2), next_piece + 1);
        mask <<= 1;
    }
}

// 右侧"暂存"区 (列 13.., 行 9..)
static void draw_hold() {
    gfx_fillrect(216, 88, 48, 24, GFX_BLACK);
    if (hold_piece == 0xFF) return;   // 暂存区为空: 只擦背景
    uint16_t mask = get_mask(hold_piece, 0);
    for (int i = 0; i < 16; i++) {
        if (mask & 0x8000) draw_cell(13 + (i & 3), 9 + (i >> 2), hold_piece + 1);
        mask <<= 1;
    }
}

// 在文本网格 (col,row) 处打印 32 位无符号数 (先擦背景)
static void print_num(int col, int row, uint32_t v) {
    gfx_fillrect(col * 8, row * 8, 80, 8, GFX_BLACK);
    char buf[12];                        // 32 位最多 10 位数字
    int n = 0;
    do {
        buf[n++] = '0' + (v % 10);
        v /= 10;
    } while (v);
    gfx_set_cursor(col * 8, row * 8);
    for (int i = n - 1; i >= 0; i--) gfx_putc(buf[i]);
}

// 分数/行数/等级/最高分/状态
static void draw_stats() {
    print_num(2, 5, score);
    print_num(2, 8, lines);
    print_num(2, 11, level);

    if (score > hiscore) hiscore = score;   // 刷新最高分 (内存内, 重启清空)
    print_num(2, 18, hiscore);

    gfx_fillrect(8, 112, 104, 16, GFX_BLACK);   // 清空状态区
    if (gameover) {
        put_str(2, 14, "GAME OVER");
        put_str(2, 15, "R: RESTART");
    } else if (paused) {
        put_str(2, 14, "PAUSED");
    }
}

// ---------- 消行闪烁动画 ----------

// 等待 n 个滴答 (期间用 hlt 省 CPU)
static void wait_ticks(int n) {
    uint32_t start = g_ticks;
    while (g_ticks - start < (uint32_t)n) __asm__ volatile("hlt");
}

// 把满行画成白色 (flash_mode=1) 或按棋盘原色重画 (flash_mode=0)
static void paint_full_rows() {
    for (int r = 0; r < BOARD_H; r++) {
        if (!fullrow[r]) continue;
        if (flash_mode) {
            gfx_fillrect(BOARD_X, BOARD_Y + r * 8, 80, 8, 15);   // 整行铺白
        } else {
            for (int c = 0; c < BOARD_W; c++) draw_cell(c, r, board[r * BOARD_W + c]);
        }
    }
}

// 先找出满行记入 fullrow[], 有满行才闪: 白 -> 原色 重复 3 次, 最后停在白色
static void flash_lines() {
    int cnt = 0;
    for (int r = 0; r < BOARD_H; r++) {
        fullrow[r] = 1;
        for (int c = 0; c < BOARD_W; c++) {
            if (!board[r * BOARD_W + c]) { fullrow[r] = 0; break; }
        }
        if (fullrow[r]) cnt++;
    }
    if (!cnt) return;

    beep(1356);    // 880 Hz: 消行
    draw_all();    // 先把刚锁定的方块画出来
    flash_n = 3;
    do {
        flash_mode = 1; paint_full_rows(); wait_ticks(2);
        flash_mode = 0; paint_full_rows(); wait_ticks(2);
    } while (--flash_n);
    flash_mode = 1; paint_full_rows();   // 最后停在白色, 紧接着行被删除
    wait_ticks(3);
}

// ---------- 调色板 ----------

// 按当前等级设置 VGA 调色板 (DAC):
//   下标 1..8  = 7 种方块 + 影子, 取第 (等级-1) mod 5 套主题
//   下标 17..23 = 7 种方块的暗色版 (RGB 分量减半)
//   下标 9    = 边框, 每级一种颜色
static void apply_palette() {
    int theme = (level - 1) % 5;
    for (int c = 0; c < 8; c++) {
        gfx_set_color(1 + c,
                      themes[theme][c * 3],
                      themes[theme][c * 3 + 1],
                      themes[theme][c * 3 + 2]);
    }
    for (int c = 0; c < 7; c++) {
        gfx_set_color(17 + c,
                      themes[theme][c * 3] >> 1,
                      themes[theme][c * 3 + 1] >> 1,
                      themes[theme][c * 3 + 2] >> 1);
    }
    gfx_set_color(9,
                  border_tbl[level - 1][0],
                  border_tbl[level - 1][1],
                  border_tbl[level - 1][2]);
}

// ---------- 标题画面 ----------

static void title_screen() {
    gfx_clear(GFX_BLACK);
    for (int i = 0; i < 7; i++) {
        gfx_fillrect(80 + i * 24, 60, 16, 16, 1 + i);   // 七色方块
    }
    put_str(14, 11, "T E T R I S");
    put_str(9, 13, "MyQwen-OS EDITION");
    put_str(13, 18, "PRESS ANY KEY");

    while (game_key() < 0) __asm__ volatile("hlt");   // 等任意键
    gfx_clear(GFX_BLACK);
}

// ---------- 主循环 ----------

void tetris_run() {
    quit = 0;
    apply_palette();   // 标题画面也要用等级 1 配色
    title_screen();
    draw_frame();
    init_game();

    while (!quit) {
        if (dirty) { draw_all(); dirty = 0; }
        __asm__ volatile("hlt");   // 睡到下一个中断 (定时器/键盘), 省 CPU

        int key;
        while ((key = game_key()) >= 0) handle_key(key);

        if (paused || gameover) {
            last_tick = g_ticks;    // 暂停/结束期间不累计时间
            lock_start = g_ticks;   // 暂停期间锁定计时也不累计
        } else {
            lock_check();
            if (g_ticks - last_tick >= (uint32_t)(11 - level)) {
                last_tick = g_ticks;
                gravity();
            }
        }
    }
}
