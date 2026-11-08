#include "lib.h"

#define LINE_MAX  256
#define WORD_MAX  64
#define ARGS_MAX  12
#define PIPE_MAX  4
#define JOB_MAX   4
#define JOB_TEXT  128

typedef struct {
    char* argv[ARGS_MAX];
    int argc;
    char* input;
    char* output;
    int append;
    char* error_output;
    int error_append;
    int stderr_to_stdout;
} command_t;

typedef struct {
    int used;
    int id;
    int pgid;
    int pids[PIPE_MAX];
    int pid_count;
    int stopped;
    char text[JOB_TEXT];
} job_t;

static job_t jobs[JOB_MAX];
static char token_storage[LINE_MAX];
static char* shell_env_names[8];
static int shell_env_count;
static int last_status;
static char default_home[] = "HOME=/";
static char default_path[] = "PATH=/bin";
static char default_newos[] = "NEWOS=1";

static int slen(const char* s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void scopy(char* dst, const char* src, int cap) {
    int i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    if (cap) dst[i] = '\0';
}

static int parse_number(const char* text, int* value) {
    if (!text || !*text) return -1;
    if (*text == '%') ++text;
    int n = 0;
    int digits = 0;
    while (*text >= '0' && *text <= '9') {
        n = n * 10 + (*text++ - '0');
        ++digits;
    }
    if (!digits || *text) return -1;
    *value = n;
    return 0;
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

static int join_path(char* out, int cap, const char* dir, const char* name) {
    int pos = 0;
    int overflow = 0;
    for (int i = 0; dir && dir[i]; ++i) {
        if (pos + 1 >= cap) {
            overflow = 1;
            break;
        }
        out[pos++] = dir[i];
    }
    if (pos && out[pos - 1] != '/' && pos + 1 < cap) out[pos++] = '/';
    else if (pos && out[pos - 1] != '/') overflow = 1;
    for (int i = 0; name && name[i]; ++i) {
        if (pos + 1 >= cap) {
            overflow = 1;
            break;
        }
        out[pos++] = name[i];
    }
    if (overflow) return -1;
    out[pos] = '\0';
    return 0;
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
    return c == '<' || c == '>' || c == '|' || c == '&' || c == ';';
}

static char pipe_word[] = "|";
static char in_word[] = "<";
static char out_word[] = ">";
static char append_word[] = ">>";
static char amp_word[] = "&";
static char err_out_word[] = "2>";
static char err_append_word[] = "2>>";
static char err_stdout_word[] = "2>&1";

static const char* env_value(const char* name, int len) {
    for (int i = 0; i < shell_env_count; ++i) {
        const char* key = shell_env_names[i];
        if (!key) continue;
        int j = 0;
        while (j < len && key[j] == name[j]) ++j;
        if (j == len && key[j] == '=') return key + j + 1;
    }
    return "";
}

static int append_text(char* out, int* pos, const char* text, int len) {
    for (int i = 0; i < len; ++i) {
        if (*pos + 1 >= LINE_MAX) return -1;
        out[(*pos)++] = text[i];
    }
    return 0;
}

static int tokenize(char* line, char** words, int max) {
    int count = 0;
    char* p = line;
    int pos = 0;
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
            } else if (c == '&') {
                words[count++] = amp_word;
            } else {
                words[count++] = p - 1;
                (p - 1)[1] = '\0';
                p++;
            }
            continue;
        }
        if (*p == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1') {
            words[count++] = err_stdout_word;
            p += 4;
            continue;
        }
        if (*p == '2' && p[1] == '>') {
            if (p[2] == '>') {
                words[count++] = err_append_word;
                p += 3;
            } else {
                words[count++] = err_out_word;
                p += 2;
            }
            continue;
        }

        char* token = &token_storage[pos];
        int any = 0;
        char quote = 0;
        while (*p) {
            if (!quote && is_space(*p)) break;
            if (!quote && is_operator(*p)) break;
            if (*p == '\\' && quote != '\'') {
                ++p;
                if (!*p) break;
                if (append_text(token_storage, &pos, p, 1) < 0) return -1;
                ++p;
                any = 1;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                if (!quote) {
                    quote = *p++;
                    any = 1;
                    continue;
                }
                if (quote == *p) {
                    quote = 0;
                    ++p;
                    continue;
                }
            }
            if (*p == '$' && quote != '\'') {
                ++p;
                if (*p == '?') {
                    char num[12];
                    int n = 0, v = last_status;
                    if (!v) num[n++] = '0';
                    while (v) { num[n++] = (char)('0' + v % 10); v /= 10; }
                    while (n) {
                        char c = num[--n];
                        if (append_text(token_storage, &pos, &c, 1) < 0) return -1;
                    }
                    ++p;
                    any = 1;
                    continue;
                }
                if (*p == '$') {
                    char num[12];
                    int n = 0, v = getpid();
                    while (v) { num[n++] = (char)('0' + v % 10); v /= 10; }
                    while (n) {
                        char c = num[--n];
                        if (append_text(token_storage, &pos, &c, 1) < 0) return -1;
                    }
                    ++p;
                    any = 1;
                    continue;
                }
                const char* var = p;
                while ((*p >= 'a' && *p <= 'z') ||
                       (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '_') ++p;
                int vlen = (int)(p - var);
                if (vlen) {
                    const char* value = env_value(var, vlen);
                    if (append_text(token_storage, &pos, value, slen(value)) < 0)
                        return -1;
                } else {
                    if (append_text(token_storage, &pos, "$", 1) < 0) return -1;
                }
                any = 1;
                continue;
            }
            if (append_text(token_storage, &pos, p, 1) < 0) return -1;
            ++p;
            any = 1;
        }
        if (quote || !any) return -1;
        token_storage[pos++] = '\0';
        words[count++] = token;
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
        cmd->error_output = 0;
        cmd->error_append = 0;
        cmd->stderr_to_stdout = 0;
        for (int i = start; i < split; ++i) {
            if (scmp(words[i], "2>&1") == 0) {
                cmd->stderr_to_stdout = 1;
            } else if (scmp(words[i], "<") == 0 ||
                scmp(words[i], ">") == 0 ||
                scmp(words[i], ">>") == 0 ||
                scmp(words[i], "2>") == 0 ||
                scmp(words[i], "2>>") == 0) {
                if (i + 1 >= split) return -1;
                if (words[i][0] == '<') {
                    cmd->input = words[++i];
                } else if (words[i][0] == '2') {
                    cmd->error_append = words[i][2] == '>';
                    cmd->error_output = words[++i];
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
    if (cmd->stderr_to_stdout && dup2(1, 2) < 0) return -1;
    if (cmd->error_output) {
        uint32_t flags = O_CREAT | O_WRONLY |
                         (cmd->error_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->error_output, flags);
        if (fd < 0 || dup2(fd, 2) < 0) return -1;
        if (fd != 2) close(fd);
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

static void print_stat_entry(const char* name, const user_stat_t* info) {
    swrite(1, info->type == 2 ? "d " : "- ");
    putdec(info->nlink);
    swrite(1, " ");
    putdec(info->size);
    swrite(1, " ");
    swrite(1, name);
    if (info->type == 2) swrite(1, "/");
    swrite(1, "\n");
}

static int utility_ls(const command_t* cmd) {
    int arg = 1;
    int long_format = 0;
    if (arg < cmd->argc && scmp(cmd->argv[arg], "-l") == 0) {
        long_format = 1;
        ++arg;
    }
    const char* path = arg < cmd->argc ? cmd->argv[arg] : ".";
    user_dirent_t entry;
    user_stat_t info;
    if (stat(path, &info) < 0) {
        swrite(2, "ls: no such file or directory\n");
        return 1;
    }
    if (info.type != 2) {
        if (long_format) print_stat_entry(basename(path), &info);
        else {
            swrite(1, basename(path));
            swrite(1, "\n");
        }
        return 0;
    }

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
        if (long_format) {
            char child[256];
            user_stat_t child_info;
            if (join_path(child, sizeof(child), path, entry.name) == 0 &&
                stat(child, &child_info) == 0) {
                print_stat_entry(entry.name, &child_info);
            } else {
                child_info.inum = entry.inum;
                child_info.type = entry.type;
                child_info.size = entry.size;
                child_info.nlink = 1;
                print_stat_entry(entry.name, &child_info);
            }
        } else {
            swrite(1, entry.name);
            if (entry.type == 2) swrite(1, "/");
            swrite(1, "\n");
        }
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

static int utility_mv(const command_t* cmd) {
    if (cmd->argc != 3) {
        swrite(2, "mv: usage: mv <old> <new>\n");
        return 1;
    }
    if (rename(cmd->argv[1], cmd->argv[2]) < 0) {
        swrite(2, "mv: rename failed (missing source or existing target)\n");
        return 1;
    }
    return 0;
}

static int utility_ln(const command_t* cmd) {
    if (cmd->argc != 3) {
        swrite(2, "ln: usage: ln <existing-file> <new-path>\n");
        return 1;
    }
    if (link(cmd->argv[1], cmd->argv[2]) < 0) {
        swrite(2, "ln: cannot create hard link\n");
        return 1;
    }
    return 0;
}

static int utility_truncate(const command_t* cmd) {
    int size;
    if (cmd->argc != 3 || parse_number(cmd->argv[2], &size) < 0) {
        swrite(2, "truncate: usage: truncate <file> <size>\n");
        return 1;
    }
    if (truncate(cmd->argv[1], (uint32_t)size) < 0) {
        swrite(2, "truncate: failed\n");
        return 1;
    }
    return 0;
}

static int utility_stat(const command_t* cmd) {
    if (cmd->argc != 2) {
        swrite(2, "stat: missing operand\n");
        return 1;
    }
    user_stat_t info;
    if (stat(cmd->argv[1], &info) < 0) {
        swrite(2, "stat: no such file or directory\n");
        return 1;
    }
    swrite(1, info.type == 2 ? "type=directory" : "type=file");
    swrite(1, " size=");
    putdec(info.size);
    swrite(1, " nlink=");
    putdec(info.nlink);
    swrite(1, " inode=");
    putdec(info.inum);
    swrite(1, "\n");
    return 0;
}

static int execute_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    if (scmp(name, "echo") == 0) return utility_echo(cmd);
    if (scmp(name, "pwd") == 0) return utility_pwd();
    if (scmp(name, "ls") == 0) return utility_ls(cmd);
    if (scmp(name, "cat") == 0) return utility_cat(cmd);
    if (scmp(name, "mkdir") == 0) return utility_mkdir(cmd);
    if (scmp(name, "rm") == 0) return utility_rm(cmd);
    if (scmp(name, "mv") == 0) return utility_mv(cmd);
    if (scmp(name, "ln") == 0) return utility_ln(cmd);
    if (scmp(name, "truncate") == 0) return utility_truncate(cmd);
    if (scmp(name, "stat") == 0) return utility_stat(cmd);
    if (scmp(name, "env") == 0) {
        for (int i = 0; i < shell_env_count; ++i) {
            swrite(1, shell_env_names[i]);
            swrite(1, "\n");
        }
        return 0;
    }
    if (scmp(name, "true") == 0) return 0;
    if (scmp(name, "false") == 0) return 1;
    if (scmp(name, "cd") == 0) {
        const char* path = cmd->argc > 1 ? cmd->argv[1] : "/";
        if (chdir(path) < 0) {
            swrite(2, "cd: no such directory\n");
            return 1;
        }
        return 0;
    }
    if (scmp(name, "help") == 0) {
        swrite(1, "builtins: cd exit jobs fg bg kill wait help\n");
        swrite(1, "tools: echo cat ls pwd mkdir rm mv ln truncate stat env\n");
        swrite(1, "syntax: 'quotes' \"$VAR\" a\\ b; cmd [> out] [2> err] [2>&1] [| cmd] [&]\n");
        return 0;
    }
    return -1;
}

static void exec_external(const command_t* cmd) {
    char* envp[9];
    for (int i = 0; i < shell_env_count && i < 8; ++i)
        envp[i] = shell_env_names[i];
    envp[shell_env_count < 8 ? shell_env_count : 8] = 0;
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

static int is_parent_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    return scmp(name, "cd") == 0 || scmp(name, "exit") == 0 ||
           scmp(name, "jobs") == 0 || scmp(name, "fg") == 0 ||
           scmp(name, "bg") == 0 || scmp(name, "kill") == 0 ||
           scmp(name, "wait") == 0;
}

static int job_pid_count(const job_t* job) {
    int count = 0;
    for (int i = 0; i < job->pid_count; ++i)
        if (job->pids[i] > 0) ++count;
    return count;
}

static int job_wait(job_t* job) {
    int status = 0;
    for (int i = 0; i < job->pid_count; ++i) {
        if (job->pids[i] <= 0) continue;
        int result = waitpid_flags(job->pids[i], &status, WUNTRACED);
        if (result == job->pids[i]) {
            if (WIFSTOPPED(status)) {
                job->stopped = 1;
                return status;
            }
            job->pids[i] = 0;
        } else if (result < 0) {
            job->pids[i] = 0;
        }
    }
    if (!job_pid_count(job)) job->used = 0;
    return status;
}

static void report_stopped(const job_t* job) {
    swrite(1, "[JOB] stopped ");
    putdec((uint32_t)job->id);
    swrite(1, ": ");
    swrite(1, job->text);
    swrite(1, "\n");
}

static void job_remove_pid(int pid) {
    for (int j = 0; j < JOB_MAX; ++j)
        for (int i = 0; i < jobs[j].pid_count; ++i)
            if (jobs[j].pids[i] == pid) jobs[j].pids[i] = 0;
}

static int reap_background(void) {
    int status;
    int reaped = 0;
    int pid;
    while ((pid = waitpid_flags(-1, &status, WNOHANG)) > 0) {
        job_remove_pid(pid);
        ++reaped;
    }
    return reaped;
}

static job_t* find_job(int id) {
    for (int i = 0; i < JOB_MAX; ++i)
        if (jobs[i].used && jobs[i].id == id) return &jobs[i];
    return 0;
}

static job_t* add_job(int pgid, const int pids[PIPE_MAX], int count,
                      const char* text, int announce) {
    job_t* slot = 0;
    int slot_id = 0;
    for (int i = 0; i < JOB_MAX; ++i)
        if (jobs[i].used && !jobs[i].stopped && !job_pid_count(&jobs[i]))
            jobs[i].used = 0;
    for (int i = 0; i < JOB_MAX; ++i) {
        if (!jobs[i].used) {
            slot = &jobs[i];
            slot_id = i + 1;
            break;
        }
    }
    if (!slot) return 0;
    slot->used = 1;
    slot->id = slot_id;
    slot->pgid = pgid;
    slot->pid_count = count;
    slot->stopped = 0;
    for (int i = 0; i < PIPE_MAX; ++i)
        slot->pids[i] = i < count ? pids[i] : 0;
    scopy(slot->text, text, JOB_TEXT);
    if (announce) {
        swrite(1, "[JOB] started ");
        putdec((uint32_t)slot->id);
        swrite(1, ": ");
        swrite(1, slot->text);
        swrite(1, "\n");
    }
    return slot;
}

static int builtin_jobs(void) {
    reap_background();
    int shown = 0;
    for (int i = 0; i < JOB_MAX; ++i) {
        job_t* job = &jobs[i];
        if (!job->used) continue;
        int alive = job_pid_count(job);
        swrite(1, "[JOB] ");
        putdec((uint32_t)job->id);
        swrite(1, " ");
        swrite(1, alive ? (job->stopped ? "STOPPED " : "RUNNING ") : "DONE ");
        swrite(1, job->text);
        swrite(1, "\n");
        shown = 1;
        if (!alive) job->used = 0;
    }
    if (!shown) swrite(1, "[JOB] no jobs\n");
    return 0;
}

static int builtin_fg(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "fg: missing job\n");
        return 1;
    }
    int id;
    if (parse_number(cmd->argv[1], &id) < 0) return 1;
    reap_background();
    job_t* job = find_job(id);
    if (!job) {
        swrite(2, "fg: no such job\n");
        return 1;
    }
    if (!job_pid_count(job)) {
        swrite(1, "[JOB] already done\n");
        job->used = 0;
        return 0;
    }
    if (job->stopped) {
        if (kill(-job->pgid, SIGCONT) < 0) {
            swrite(2, "fg: cannot resume job\n");
            return 1;
        }
        job->stopped = 0;
    }
    swrite(1, "[JOB] foreground ");
    putdec((uint32_t)job->id);
    swrite(1, "\n");
    tcsetpgrp(job->pgid);
    int status = job_wait(job);
    tcsetpgrp(0);
    if (job->stopped) {
        report_stopped(job);
        return WSTOPSIG(status);
    }
    return WEXITSTATUS(status);
}

static int builtin_bg(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "bg: missing job\n");
        return 1;
    }
    int id;
    if (parse_number(cmd->argv[1], &id) < 0) return 1;
    reap_background();
    job_t* job = find_job(id);
    if (!job) {
        swrite(2, "bg: no such job\n");
        return 1;
    }
    if (!job->stopped) {
        swrite(2, "bg: job is not stopped\n");
        return 1;
    }
    if (kill(-job->pgid, SIGCONT) < 0) {
        swrite(2, "bg: cannot resume job\n");
        return 1;
    }
    job->stopped = 0;
    swrite(1, "[JOB] background ");
    putdec((uint32_t)job->id);
    swrite(1, "\n");
    return 0;
}

static int builtin_kill(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "kill: missing target\n");
        return 1;
    }
    int sig = SIGTERM;
    int arg = 1;
    if (cmd->argc > 2 && cmd->argv[1][0] == '-') {
        int parsed;
        if (parse_number(cmd->argv[1] + 1, &parsed) == 0) {
            sig = parsed;
            arg = 2;
        }
    }
    if (cmd->argv[arg][0] == '%') {
        int id;
        if (parse_number(cmd->argv[arg], &id) < 0) return 1;
        reap_background();
        job_t* job = find_job(id);
        if (!job) {
            swrite(2, "kill: no such job\n");
            return 1;
        }
        return kill(-job->pgid, sig) < 0 ? 1 : 0;
    }
    int pid;
    if (parse_number(cmd->argv[arg], &pid) < 0 || pid <= 0) return 1;
    return kill(pid, sig) < 0 ? 1 : 0;
}

static int builtin_wait(void) {
    tcsetpgrp(0);
    int status = 0;
    for (int j = 0; j < JOB_MAX; ++j) {
        job_t* job = &jobs[j];
        if (!job->used) continue;
        status = job_wait(job);
        if (job->stopped) return WSTOPSIG(status);
    }
    return WEXITSTATUS(status);
}

static int execute_pipeline(command_t cmds[PIPE_MAX], int count,
                            int background, const char* text) {
    if (count == 1 && !background && is_parent_builtin(&cmds[0])) {
        const char* name = basename(cmds[0].argv[0]);
        if (scmp(name, "exit") == 0) {
            for (int i = 0; i < JOB_MAX; ++i)
                if (jobs[i].used) kill(-jobs[i].pgid, SIGTERM);
            return -1;
        }
        if (scmp(name, "jobs") == 0) return builtin_jobs();
        if (scmp(name, "fg") == 0) return builtin_fg(&cmds[0]);
        if (scmp(name, "bg") == 0) return builtin_bg(&cmds[0]);
        if (scmp(name, "kill") == 0) return builtin_kill(&cmds[0]);
        if (scmp(name, "wait") == 0) return builtin_wait();
        return run_command(&cmds[0], 0);
    }

    int pipes[PIPE_MAX - 1][2];
    for (int i = 0; i < count - 1; ++i) {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
        if (pipe(pipes[i]) < 0) {
            swrite(2, "sh: pipe failed\n");
            for (int j = 0; j < i; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    int pids[PIPE_MAX] = { -1, -1, -1, -1 };
    for (int i = 0; i < count; ++i) {
        int pid = fork();
        if (pid == 0) {
            int group = i == 0 ? 0 : pids[0];
            setpgid(0, group);
            if (i > 0 && dup2(pipes[i - 1][0], 0) < 0) exit(1);
            if (i < count - 1 && dup2(pipes[i][1], 1) < 0) exit(1);
            for (int j = 0; j < count - 1; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            exit(run_command(&cmds[i], 1));
        }
        if (pid < 0) {
            pids[i] = -1;
            break;
        }
        pids[i] = pid;
        setpgid(pid, i == 0 ? pid : pids[0]);
    }

    for (int i = 0; i < count - 1; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    if (pids[0] < 0) return 1;

    job_t* job = add_job(pids[0], pids, count, text, background);
    if (!job) {
        swrite(2, "sh: job table full\n");
        kill(-pids[0], SIGTERM);
        return 1;
    }
    if (background) return 0;

    tcsetpgrp(pids[0]);
    int status = job_wait(job);
    tcsetpgrp(0);
    if (job->stopped) {
        report_stopped(job);
        return WSTOPSIG(status);
    }
    return WEXITSTATUS(status);
}

static void shell_prompt(void) {
    char pwd[256];
    if (!getcwd(pwd, sizeof(pwd))) pwd[0] = '\0';
    swrite(1, "sh:");
    swrite(1, pwd);
    swrite(1, "> ");
}

static int execute_segment(char* segment) {
    char* words[WORD_MAX];
    int nwords = tokenize(segment, words, WORD_MAX);
    if (nwords < 0) {
        swrite(2, "sh: syntax error\n");
        return 2;
    }
    command_t cmds[PIPE_MAX];
    int count = 0;
    int background = 0;
    int parsed = parse_commands(words, nwords, cmds, &count, &background);
    if (parsed < 0) {
        swrite(2, "sh: syntax error\n");
        return 2;
    }
    if (!parsed) return 0;
    char command_text[JOB_TEXT];
    scopy(command_text, segment, sizeof(command_text));
    int status = execute_pipeline(cmds, count, background, command_text);
    last_status = status < 0 ? 0 : status;
    return status;
}

static int execute_line(char* line) {
    char* start = line;
    char quote = 0;
    int escaped = 0;
    for (char* p = line; ; ++p) {
        char c = *p;
        if (escaped) {
            escaped = 0;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            escaped = 1;
            continue;
        }
        if (c == '\'' || c == '"') {
            if (!quote) quote = c;
            else if (quote == c) quote = 0;
            continue;
        }
        if ((!c || c == ';') && !quote) {
            char saved = *p;
            *p = '\0';
            while (is_space(*start)) start++;
            if (*start) {
                int status = execute_segment(start);
                if (status < 0) return -1;
            }
            if (!saved) return 0;
            start = p + 1;
        }
    }
}

static int shell_loop(void) {
    char line[LINE_MAX];
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
        if (execute_line(line) < 0) return 0;
        shell_prompt();
    }
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    shell_env_count = 0;
    while (envp && envp[shell_env_count] && shell_env_count < 8)
        shell_env_names[shell_env_count] = envp[shell_env_count++];
    if (!shell_env_count) {
        shell_env_names[0] = default_home;
        shell_env_names[1] = default_path;
        shell_env_names[2] = default_newos;
        shell_env_count = 3;
    }
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
