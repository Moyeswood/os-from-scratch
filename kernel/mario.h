#ifndef MARIO_H
#define MARIO_H

// 超级马里奥 (C 版). 调用前必须已在模式 13h (rm_switch_mode(0x13)),
// 返回时游戏已退出 (玩家按 ESC), 图形模式还保持 13h, 由调用者切回文本模式
void mario_run();

#endif
