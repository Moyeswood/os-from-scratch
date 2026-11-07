#ifndef FD_H
#define FD_H

#include "types.h"

// 内核打开文件对象。fd 表只保存指针，fork 后父子共享 refs/off。
typedef struct task_file task_file_t;

task_file_t* fd_stdin(void);
task_file_t* fd_stdout(void);

// inum + 访问模式。失败返回 0。
task_file_t* fd_file_new(uint32_t inum, uint8_t readable,
                         uint8_t writable, uint8_t append);

// 创建一对共享环形缓冲区的 pipe 端点。
int fd_pipe_pair(task_file_t** reader, task_file_t** writer);

void fd_retain(task_file_t* file);
void fd_release(task_file_t* file);
int fd_is_pipe(task_file_t* file);

int fd_file_read(task_file_t* file, void* buf, uint32_t count);
int fd_file_write(task_file_t* file, const void* buf, uint32_t count);
int fd_file_seek(task_file_t* file, int offset, int whence);

// pipe 读写会阻塞到另一端产生数据/腾出空间或关闭。
int fd_pipe_read(task_file_t* file, void* buf, uint32_t count);
int fd_pipe_write(task_file_t* file, const void* buf, uint32_t count);

#endif
