#ifndef EXEC_H
#define EXEC_H

#include "types.h"
#include "user.h"

#define EXEC_MAX_ARGS 16
#define EXEC_MAX_ENVS 8
#define EXEC_ARG_STORAGE 3072

typedef struct {
    int argc;
    int envc;
    const char* argv[EXEC_MAX_ARGS + 1];
    const char* envp[EXEC_MAX_ENVS + 1];
    char storage[EXEC_ARG_STORAGE];
} exec_args_t;

// 把内嵌用户程序发布到 /bin，供 run/exec 从文件系统加载。
void exec_install_builtins(void);

// 从文件系统读入一个 flat 可执行文件。调用者负责 kfree(image)。
int exec_load_file(const char* path, uint8_t** image, uint32_t* size);

// 把当前进程原地替换为新镜像。成功后直接改系统调用返回帧，
// 回到新程序入口；任何失败都保留旧地址空间并返回 -1。
int exec_replace_current(user_frame_t* frame, const char* path,
                         const exec_args_t* args);

// 从路径提取 <= cap-1 字节的程序名，用于 ps。
void exec_basename(char* out, uint32_t cap, const char* path);

#endif
