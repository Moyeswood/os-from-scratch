#ifndef EDITOR_H
#define EDITOR_H

// 内置文本编辑器 (nano 风格教学版), shell 的 edit 命令调用。
// 支持撤销/重做、搜索、跳行和按词导航:
//   ^S 保存  ^X 退出  ^Z 撤销  ^Y 重做  ^F 搜索  ^G 跳行
//   Ctrl+方向键按词移动, Ctrl+Backspace 删除前一个词。
// 有修改时 ^X 会询问 Y=保存退出 / N=丢弃 / Esc=取消。
void editor_run(const char* path);

// vi 风格 modal 编辑器，复用同一个文本缓冲、撤销和文件系统实现。
void vi_run(const char* path);

#endif
