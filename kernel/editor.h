#ifndef EDITOR_H
#define EDITOR_H

// 内置文本编辑器 (nano 风格教学版), shell 的 edit 命令调用。
// 打开/创建 path 指向的文件, 编辑, Ctrl+S 保存, Ctrl+X 退出
// (有修改时第一次 ^X 只警告, 再按一次才放弃修改退出)。
void editor_run(const char* path);

#endif
