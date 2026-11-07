#include "lib.h"

#define LINE_MAX  256
#define WORD_MAX  24
#define ARGS_MAX  12
#define PIPE_MAX  2

typedef struct {
    char* argv[ARGS_MAX];
    int argc;
    char* input;
    char* output;
    int append;
} command_t;

static int slen(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int scmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void swrite(int fd, const char* s) {
    if (s) write(fd, s, (uint32_t)slen(s));
}

static int write_all(int fd, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t done = 0;
    while (done < len) {
        int n = write(fd, p + done, len - done);
        if (n <= 0) return -1;
        done += (uint32_t)n;
    }
    return (int)done;
}

static const char* basename(const char* path) {
    const char* base = path;
    for (const char* p = path; p && *p; ++p)
        if (*p == '/') base = p + 1;
    return base;
}

static int read_byte(int fd, char* c) {
    int n;
    do {
        n = read(fd, c, 1);
    } while (n < 0);
    return n;
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int is_operator(char c) {
    return c == '<' || c == '>' || c == '|' || c == '&';
}

static char pipe_word[] = "|";
static char in_word[] = "<";
static char out_word[] = ">";
static char append_word[] = ">>";
static char amp_word[] = "&";

static int tokenize(char* line, char** words, int max) {
    int count = 0;
    char* p = line;
    while (*p && count < max) {
        while (is_space(*p)) p++;
        if (!*p) break;
        if (is_operator(*p)) {
            char c = *p++;
            if (c == '>') {
                if (*p == '>') {
                    p++;
                    words[count++] = append_word;
                } else {
                    words[count++] = out_word;
                }
            } else if (c == '<') {
                words[count++] = in_word;
            } else if (c == '|') {
                words[count++] = pipe_word;
            } else {
                words[count++] = amp_word;
            }
            continue;
        }
        words[count++] = p;
        while (*p && !is_space(*p) && !is_operator(*p)) p++;
        if (*p) *p++ = '\0';
    }
    return count;
}

static int parse_commands(char** words, int count, command_t cmds[PIPE_MAX],
                          int* cmd_count, int* background) {
    *cmd_count = 0;
    *background = 0;
    if (count > 0 && scmp(words[count - 1], "&") == 0) {
        *background = 1;
        count--;
    }
    for (int i = 0; i < count; ++i)
        if (scmp(words[i], "&") == 0) return -1;
    if (!count) return 0;

    int start = 0;
    for (int split = 0; split <= count; ++split) {
        if (split < count && scmp(words[split], "|") != 0) continue;
        if (split == start || *cmd_count >= PIPE_MAX) return -1;

        command_t* cmd = &cmds[(*cmd_count)++];
        cmd->argc = 0;
        cmd->input = 0;
        cmd->output = 0;
        cmd->append = 0;
        for (int i = start; i < split; ++i) {
            if (scmp(words[i], "<") == 0 ||
                scmp(words[i], ">") == 0 ||
                scmp(words[i], ">>") == 0) {
                if (i + 1 >= split) return -1;
                if (words[i][0] == '<') {
                    cmd->input = words[++i];
                } else {
                    cmd->append = words[i][1] == '>';
                    cmd->output = words[++i];
                }
            } else {
                if (cmd->argc >= ARGS_MAX - 1) return -1;
                cmd->argv[cmd->argc++] = words[i];
            }
        }
        cmd->argv[cmd->argc] = 0;
        if (!cmd->argc) return -1;
        start = split + 1;
    }
    return *cmd_count;
}

static int setup_redirections(const command_t* cmd) {
    if (cmd->input) {
        int fd = open(cmd->input, O_RDONLY);
        if (fd < 0 || dup2(fd, 0) < 0) return -1;
        if (fd != 0) close(fd);
    }
    if (cmd->output) {
        uint32_t flags = O_CREAT | O_WRONLY |
                         (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output, flags);
        if (fd < 0 || dup2(fd, 1) < 0) return -1;
        if (fd != 1) close(fd);
    }
    return 0;
}

static int utility_echo(const command_t* cmd) {
    for (int i = 1; i < cmd->argc; ++i) {
        if (i > 1) swrite(1, " ");
        swrite(1, cmd->argv[i]);
    }
    swrite(1, "\n");
    return 0;
}

static int utility_pwd(void) {
    char pwd[256];
    if (!getcwd(pwd, sizeof(pwd))) return 1;
    swrite(1, pwd);
    swrite(1, "\n");
    return 0;
}

static int utility_ls(const command_t* cmd) {
    const char* path = cmd->argc > 1 ? cmd->argv[1] : ".";
    user_dirent_t entry;
    int entries = 0;
    for (uint32_t idx = 0; ; ++idx) {
        int got = readdir(path, idx, &entry);
        if (got <= 0) {
            if (got < 0 && !entries) {
                swrite(2, "ls: not a directory\n");
                return 1;
            }
            break;
        }
        if (!entry.inum || scmp(entry.name, ".") == 0 ||
            scmp(entry.name, "..") == 0) continue;
        entries++;
        swrite(1, entry.name);
        if (entry.type == 2) swrite(1, "/");
        swrite(1, "\n");
    }
    return 0;
}

static int cat_one(const char* path) {
    int fd = path ? open(path, O_RDONLY) : 0;
    if (fd < 0) {
        swrite(2, "cat: cannot open ");
        swrite(2, path);
        swrite(2, "\n");
        return 1;
    }
    char buf[256];
    int status = 0;
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0 || write_all(1, buf, (uint32_t)n) < 0) {
            status = 1;
            break;
        }
    }
    if (path && fd != 0) close(fd);
    return status;
}

static int utility_cat(const command_t* cmd) {
    if (cmd->argc == 1) return cat_one(0);
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i)
        if (cat_one(cmd->argv[i])) status = 1;
    return status;
}

static int utility_mkdir(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "mkdir: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i)
        if (mkdir(cmd->argv[i]) < 0) status = 1;
    return status;
}

static int utility_rm(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "rm: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i)
        if (remove(cmd->argv[i]) < 0) status = 1;
    return status;
}

static int execute_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    if (scmp(name, "echo") == 0) return utility_echo(cmd);
    if (scmp(name, "pwd") == 0) return utility_pwd();
    if (scmp(name, "ls") == 0) return utility_ls(cmd);
    if (scmp(name, "cat") == 0) return utility_cat(cmd);
    if (scmp(name, "mkdir") == 0) return utility_mkdir(cmd);
    if (scmp(name, "rm") == 0) return utility_rm(cmd);
    if (scmp(name, "cd") == 0) {
        const char* path = cmd->argc > 1 ? cmd->argv[1] : "/";
        if (chdir(path) < 0) {
            swrite(2, "cd: no such directory\n");
            return 1;
        }
        return 0;
    }
    if (scmp(name, "help") == 0) {
        swrite(1, "builtins: cd exit help; tools: echo cat ls pwd mkdir rm\n");
        swrite(1, "syntax: cmd [args] [< in] [> out] [>> out] [| cmd2] [&]\n");
        return 0;
    }
    return -1;
}

static void exec_external(const command_t* cmd) {
    static char* envp[] = { "HOME=/", "PATH=/bin", "NEWOS=1", 0 };
    char path[128];
    const char* name = cmd->argv[0];
    if (name[0] == '/') {
        for (int i = 0; i < (int)sizeof(path) - 1 && name[i]; ++i)
            path[i] = name[i];
    } else {
        const char prefix[] = "/bin/";
        int pos = 0;
        for (int i = 0; prefix[i]; ++i) path[pos++] = prefix[i];
        for (int i = 0; name[i] && pos < (int)sizeof(path) - 1; ++i)
            path[pos++] = name[i];
        path[pos] = '\0';
    }
    execve(path, cmd->argv, envp);
    swrite(2, "sh: not found: ");
    swrite(2, cmd->argv[0]);
    swrite(2, "\n");
}

static int run_command(const command_t* cmd, int child) {
    if (child && setup_redirections(cmd) < 0) {
        swrite(2, "sh: redirection failed\n");
        return 1;
    }
    int builtin = execute_builtin(cmd);
    if (builtin >= 0) return builtin;
    exec_external(cmd);
    return 127;
}

static int wait_foreground(int pid) {
    int status = 0;
    return waitpid(pid, &status) < 0 ? 1 : (status & 0xFF);
}

static void reap_background(void) {
    int status;
    while (waitpid_flags(-1, &status, WNOHANG) > 0) {}
}

static int is_parent_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    return scmp(name, "cd") == 0 || scmp(name, "exit") == 0;
}

static int execute_pipeline(command_t cmds[PIPE_MAX], int count,
                            int background) {
    if (count == 1 && !background && is_parent_builtin(&cmds[0])) {
        if (scmp(basename(cmds[0].argv[0]), "exit") == 0) return -1;
        return run_command(&cmds[0], 0);
    }

    int pipefd[2] = { -1, -1 };
    if (count == 2 && pipe(pipefd) < 0) {
        swrite(2, "sh: pipe failed\n");
        return 1;
    }

    int pids[PIPE_MAX] = { -1, -1 };
    for (int i = 0; i < count; ++i) {
        int pid = fork();
        if (pid == 0) {
            if (count == 2) {
                if (i == 0) {
                    close(pipefd[0]);
                    if (dup2(pipefd[1], 1) < 0) exit(1);
                    close(pipefd[1]);
                } else {
                    close(pipefd[1]);
                    if (dup2(pipefd[0], 0) < 0) exit(1);
                    close(pipefd[0]);
                }
            }
            exit(run_command(&cmds[i], 1));
        }
        if (pid < 0) {
            pids[i] = -1;
            break;
        }
        pids[i] = pid;
    }

    if (count == 2) {
        close(pipefd[0]);
        close(pipefd[1]);
    }
    if (!background) {
        int status = 0;
        for (int i = 0; i < count; ++i)
            if (pids[i] > 0 && waitpid(pids[i], &status) < 0) status = 1;
        return status & 0xFF;
    }
    return 0;
}

static void shell_prompt(void) {
    char pwd[256];
    if (!getcwd(pwd, sizeof(pwd))) pwd[0] = '\0';
    swrite(1, "sh:");
    swrite(1, pwd);
    swrite(1, "> ");
}

static int shell_loop(void) {
    char line[LINE_MAX];
    char* words[WORD_MAX];
    int len = 0;
    shell_prompt();
    for (;;) {
        char c;
        int n = read_byte(0, &c);
        if (n <= 0) {
            if (!len) {
                swrite(1, "\n");
                return 0;
            }
        } else if (c == '\n' || c == '\r') {
            swrite(1, "\n");
        } else if (c == '\b') {
            if (len) {
                len--;
                swrite(1, "\b \b");
            }
            continue;
        } else if ((uint8_t)c >= 32 && len < LINE_MAX - 1) {
            line[len++] = c;
            char out[1] = { c };
            write_all(1, out, 1);
            continue;
        } else {
            continue;
        }

        line[len] = '\0';
        len = 0;
        reap_background();
        int nwords = tokenize(line, words, WORD_MAX);
        command_t cmds[PIPE_MAX];
        int count = 0;
        int background = 0;
        int parsed = parse_commands(words, nwords, cmds, &count, &background);
        if (parsed < 0) {
            swrite(2, "sh: syntax error\n");
        } else if (parsed > 0) {
            if (execute_pipeline(cmds, count, background) < 0) return 0;
        }
        shell_prompt();
    }
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    (void)envp;
    const char* name = argc > 0 && argv[0] ? basename(argv[0]) : "sh";
    command_t cmd;
    cmd.argc = argc;
    for (int i = 0; i < argc && i < ARGS_MAX; ++i) cmd.argv[i] = argv[i];
    if (argc < ARGS_MAX) cmd.argv[argc] = 0;
    cmd.input = 0;
    cmd.output = 0;
    cmd.append = 0;

    if (scmp(name, "sh") == 0) exit(shell_loop());
    int status = execute_builtin(&cmd);
    if (status >= 0) exit(status);
    exec_external(&cmd);
    exit(127);
}
