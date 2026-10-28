#ifndef TETRIS_H
#define TETRIS_H

// 俄罗斯方块 (C 移植版). 调用前必须已在模式 13h (rm_switch_mode(0x13)),
// 返回时游戏已退出 (玩家按 ESC), 图形模式还保持 13h, 由调用者切回文本模式
void tetris_run();

#endif
