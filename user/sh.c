#include "lib.h"

#define LINE_MAX  256
#define WORD_MAX  64
#define ARGS_MAX  20
#define PIPE_MAX  4
#define JOB_MAX   4
#define JOB_TEXT  128
#define GLOB_STORAGE 2048
#define HISTORY_MAX 24
#define HISTORY_FILE "/.sh_history"
#define HISTORY_LIMIT 4096
#define FIELD_SEP '\x1f'
#define ENV_MAX 16
#define ENV_TEXT_MAX 128
#define ENV_NAME_MAX 48
#define SUBST_MAX 1024
#define SUBST_LITERAL '\x1e'
#define COPY_DEPTH_MAX 12
#define TAIL_RING_MAX 16

typedef struct {
    char* argv[ARGS_MAX];
    int argc;
    char* input;
    char* output;
    int append;
    char* error_output;
    int error_append;
    int stderr_to_stdout;
    int pipe_input;         /* 修复: 标记 stdin 来自管道 (由 execute_pipeline 设置) */
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
static char glob_storage[GLOB_STORAGE];
static char shell_env_storage[ENV_MAX][ENV_TEXT_MAX];
static unsigned char shell_env_exported[ENV_MAX];
static int shell_env_count;
static int last_status;
static int shell_exit_status;
static char history[HISTORY_MAX][LINE_MAX];
static int history_count;
static int history_index;
static int history_loading;
static char substitution_storage[SUBST_MAX];
static int continuation_prompt;
static char default_home[] = "HOME=/";
static char default_path[] = "PATH=/bin";
static char default_newos[] = "NEWOS=1";
static char previous_cwd[LINE_MAX] = "/";

static void shell_prompt(void);
static int execute_line(char* line);
static int execute_script_file(const char* path);
static void swrite_dec(int fd, uint32_t value);

static const char* shell_builtin_names[] = {
    "[", ".", "bg", "cat", "cd", "clear", "cp", "echo", "env", "exit",
    "export", "false", "fg", "grep", "head", "help", "hexdump",
    "history", "jobs", "kill", "ln", "ls", "mkdir", "mv", "printf",
    "pwd", "read", "rm", "set", "sleep", "source", "stat", "tail",
    "test", "touch", "truncate", "true", "type", "unset", "uptime",
    "wait", "wc", "which", 0
};

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
        int d = *text++ - '0';
        /* 修复: 10 位数字会符号溢出 (UB), 超 INT_MAX 直接拒绝 */
        if (n > (2147483647 - d) / 10) return -1;
        n = n * 10 + d;
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
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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
        const char* key = shell_env_storage[i];
        if (!key) continue;
        int j = 0;
        while (j < len && key[j] == name[j]) ++j;
        if (j == len && key[j] == '=') return key + j + 1;
    }
    return "";
}

static int env_valid_name(const char* name, int len) {
    if (len <= 0 || len >= ENV_NAME_MAX) return 0;
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_'))
        return 0;
    for (int i = 1; i < len; ++i)
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '_'))
            return 0;
    return 1;
}

static int env_find(const char* name, int len) {
    for (int i = 0; i < shell_env_count; ++i) {
        int j = 0;
        while (j < len && shell_env_storage[i][j] == name[j]) ++j;
        if (j == len && shell_env_storage[i][j] == '=') return i;
    }
    return -1;
}

static int env_put(const char* name, int name_len, const char* value,
                   int exported) {
    if (!env_valid_name(name, name_len) || !value) return -1;
    int value_len = slen(value);
    if (name_len + value_len + 2 > ENV_TEXT_MAX) return -1;
    int index = env_find(name, name_len);
    if (index < 0) {
        if (shell_env_count >= ENV_MAX) return -1;
        index = shell_env_count++;
    }
    int pos = 0;
    for (int i = 0; i < name_len; ++i)
        shell_env_storage[index][pos++] = name[i];
    shell_env_storage[index][pos++] = '=';
    for (int i = 0; i < value_len; ++i)
        shell_env_storage[index][pos++] = value[i];
    shell_env_storage[index][pos] = '\0';
    shell_env_exported[index] |= (unsigned char)exported;
    return 0;
}

static int env_set_word(const char* word, int exported) {
    if (!word) return -1;
    int split = 0;
    while (word[split] && word[split] != '=') ++split;
    if (!word[split]) return -1;
    return env_put(word, split, word + split + 1, exported);
}

static int env_unset(const char* name, int len) {
    int index = env_find(name, len);
    if (index < 0) return 0;
    for (int i = index; i + 1 < shell_env_count; ++i) {
        scopy(shell_env_storage[i], shell_env_storage[i + 1], ENV_TEXT_MAX);
        shell_env_exported[i] = shell_env_exported[i + 1];
    }
    --shell_env_count;
    shell_env_exported[shell_env_count] = 0;
    shell_env_storage[shell_env_count][0] = '\0';
    return 0;
}

static int is_assignment_word(const char* word) {
    int split = 0;
    while (word && word[split] && word[split] != '=') ++split;
    return word && word[split] == '=' && env_valid_name(word, split);
}

static void env_snapshot(char saved[ENV_MAX][ENV_TEXT_MAX],
                         unsigned char exported[ENV_MAX], int* count) {
    *count = shell_env_count;
    for (int i = 0; i < shell_env_count; ++i) {
        scopy(saved[i], shell_env_storage[i], ENV_TEXT_MAX);
        exported[i] = shell_env_exported[i];
    }
}

static void env_restore(char saved[ENV_MAX][ENV_TEXT_MAX],
                        const unsigned char exported[ENV_MAX], int count) {
    shell_env_count = count;
    for (int i = 0; i < count; ++i) {
        scopy(shell_env_storage[i], saved[i], ENV_TEXT_MAX);
        shell_env_exported[i] = exported[i];
    }
    for (int i = count; i < ENV_MAX; ++i) {
        shell_env_storage[i][0] = '\0';
        shell_env_exported[i] = 0;
    }
}

static int append_text(char* out, int* pos, const char* text, int len) {
    for (int i = 0; i < len; ++i) {
        if (*pos + 1 >= LINE_MAX) return -1;
        out[(*pos)++] = text[i];
    }
    return 0;
}

static int tokenize(char* line, char** words, int max,
                    unsigned char globs[WORD_MAX]) {
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
        globs[count] = 0;
        while (*p) {
            if (!quote && is_space(*p)) break;
            if (!quote && is_operator(*p)) break;
            if (*p == SUBST_LITERAL) {
                ++p;
                if (!*p) break;
                if (append_text(token_storage, &pos, p, 1) < 0) return -1;
                ++p;
                any = 1;
                continue;
            }
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
                    for (int i = 0; value[i]; ++i) {
                        char c = value[i];
                        if (!quote && is_space(c)) c = FIELD_SEP;
                        if (!quote && (c == '*' || c == '?')) globs[count] = 1;
                        if (append_text(token_storage, &pos, &c, 1) < 0)
                            return -1;
                    }
                } else {
                    if (append_text(token_storage, &pos, "$", 1) < 0) return -1;
                }
                any = 1;
                continue;
            }
            if (!quote && (*p == '*' || *p == '?')) globs[count] = 1;
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

static int split_fields(char** words, int count, unsigned char globs[WORD_MAX],
                        char** out, unsigned char out_globs[WORD_MAX]) {
    int out_count = 0;
    for (int i = 0; i < count; ++i) {
        char* token = words[i];
        char* start = token;
        int had_separator = 0;
        char* p = token;
        for (;;) {
            if (*p == FIELD_SEP) {
                had_separator = 1;
                *p = '\0';
                if (p > start) {
                    if (out_count >= WORD_MAX) return -1;
                    out[out_count] = start;
                    out_globs[out_count++] = globs[i];
                }
                start = ++p;
                continue;
            }
            if (!*p) {
                if (p > start || (!had_separator && !*token)) {
                    if (out_count >= WORD_MAX) return -1;
                    out[out_count] = start;
                    out_globs[out_count++] = globs[i];
                }
                break;
            }
            ++p;
        }
    }
    return out_count;
}

static int glob_match(const char* pattern, const char* name) {
    if (!*pattern) return !*name;
    if (*pattern == '*') {
        while (*pattern == '*') ++pattern;
        if (!*pattern) return 1;
        for (const char* p = name; ; ++p) {
            if (glob_match(pattern, p)) return 1;
            if (!*p) return 0;
        }
    }
    if (!*name || (*pattern != '?' && *pattern != *name)) return 0;
    return glob_match(pattern + 1, name + 1);
}

static int glob_matches(const char* pattern, char* storage, int cap,
                        char** out, int max) {
    char dir[LINE_MAX];
    const char* base = pattern;
    int split = -1;
    for (int i = 0; pattern[i]; ++i)
        if (pattern[i] == '/') split = i;
    if (split >= 0) {
        base = pattern + split + 1;
        if (!*base) return 0;
        if (split == 0) {
            dir[0] = '/';
            dir[1] = '\0';
        } else {
            if (split >= (int)sizeof(dir)) return -1;
            for (int i = 0; i < split; ++i) dir[i] = pattern[i];
            dir[split] = '\0';
        }
    } else {
        dir[0] = '.';
        dir[1] = '\0';
    }

    char* matches[WORD_MAX];
    int found = 0;
    int pos = 0;
    user_dirent_t entry;
    for (uint32_t idx = 0; ; ++idx) {
        int got = readdir(dir, idx, &entry);
        if (got <= 0) break;
        if (!entry.inum || scmp(entry.name, ".") == 0 ||
            scmp(entry.name, "..") == 0)
            continue;
        if (base[0] != '.' && entry.name[0] == '.') continue;
        if (!glob_match(base, entry.name)) continue;
        if (found >= max) return -1;

        int before = pos;
        if (split >= 0) {
            if (dir[0] == '/' && dir[1] == '\0') {
                if (pos + 1 >= cap) return -1;
                storage[pos++] = '/';
            } else {
                for (int i = 0; dir[i]; ++i) {
                    if (pos + 1 >= cap) return -1;
                    storage[pos++] = dir[i];
                }
                if (pos + 1 >= cap) return -1;
                storage[pos++] = '/';
            }
        }
        for (int i = 0; entry.name[i]; ++i) {
            if (pos + 1 >= cap) return -1;
            storage[pos++] = entry.name[i];
        }
        storage[pos++] = '\0';
        matches[found++] = storage + before;
    }

    for (int i = 1; i < found; ++i) {
        char* value = matches[i];
        int j = i - 1;
        while (j >= 0 && scmp(matches[j], value) > 0) {
            matches[j + 1] = matches[j];
            --j;
        }
        matches[j + 1] = value;
    }
    for (int i = 0; i < found; ++i) out[i] = matches[i];
    return found;
}

static int expand_words(char** words, int count, unsigned char globs[WORD_MAX],
                        char** out, int* out_count) {
    char* fields[WORD_MAX];
    unsigned char field_globs[WORD_MAX];
    count = split_fields(words, count, globs, fields, field_globs);
    if (count < 0) return -1;

    int words_out = 0;
    int storage_pos = 0;
    for (int i = 0; i < count; ++i) {
        char* matches[WORD_MAX];
        int found = 0;
        if (field_globs[i] && !is_assignment_word(fields[i]))
            found = glob_matches(fields[i], glob_storage + storage_pos,
                                 GLOB_STORAGE - storage_pos,
                                 matches, WORD_MAX - words_out);
        if (found < 0) return -1;
        if (!found) {
            if (words_out >= WORD_MAX) return -1;
            out[words_out++] = fields[i];
            continue;
        }
        for (int j = 0; j < found; ++j) {
            if (words_out >= WORD_MAX) return -1;
            out[words_out++] = matches[j];
        }
        int used_end = storage_pos;
        for (int j = 0; j < found; ++j) {
            char* p = matches[j];
            while (*p) ++p;
            int end = (int)(p + 1 - glob_storage);
            if (end > used_end) used_end = end;
        }
        storage_pos = used_end;
    }
    *out_count = words_out;
    return 0;
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
        cmd->pipe_input = 0;
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
    swrite_dec(1, info->nlink);
    swrite(1, " ");
    swrite_dec(1, info->size);
    swrite(1, " ");
    swrite(1, name);
    if (info->type == 2) swrite(1, "/");
    swrite(1, "\n");
}

static int utility_ls(const command_t* cmd) {
    int arg = 1;
    int long_format = 0;
    int show_all = 0;
    while (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
           cmd->argv[arg][1]) {
        for (int i = 1; cmd->argv[arg][i]; ++i) {
            if (cmd->argv[arg][i] == 'l') long_format = 1;
            else if (cmd->argv[arg][i] == 'a') show_all = 1;
            else {
                swrite(2, "ls: usage: ls [-la] [path]\n");
                return 2;
            }
        }
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
        if (!entry.inum) continue;
        if (!show_all && (scmp(entry.name, ".") == 0 ||
                          scmp(entry.name, "..") == 0))
            continue;
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

static void swrite_char(int fd, char c) {
    write(fd, &c, 1);
}

static void swrite_dec(int fd, uint32_t value) {
    char tmp[12];
    int pos = 0;
    if (!value) {
        swrite_char(fd, '0');
        return;
    }
    while (value) {
        tmp[pos++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (pos) swrite_char(fd, tmp[--pos]);
}

static void swrite_hex(int fd, uint32_t value, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        swrite_char(fd, hex[(value >> shift) & 0xF]);
}

static int parse_signed_number(const char* text, int* value) {
    if (!text || !*text) return -1;
    int negative = 0;
    if (*text == '-') {
        negative = 1;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    int parsed;
    if (parse_number(text, &parsed) < 0) return -1;
    *value = negative ? -parsed : parsed;
    return 0;
}

static int read_line_fd(int fd, char* line, int cap) {
    int len = 0;
    int any = 0;
    for (;;) {
        char c;
        int n = read_byte(fd, &c);
        /* 修复: 满缓冲或最后一行无换行时也要 NUL 终结,
         * 否则调用方会读过界读栈上垃圾 */
        if (n <= 0) {
            if (any) line[len] = '\0';
            return any ? 1 : 0;
        }
        any = 1;
        if (c == '\n') {
            line[len] = '\0';
            return 1;
        }
        if (c != '\r' && len < cap - 1)
            line[len++] = c;
    }
}

static int utility_touch(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "touch: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i) {
        user_stat_t info;
        if (stat(cmd->argv[i], &info) == 0) {
            if (info.type != 1) status = 1;
            continue;
        }
        int fd = open(cmd->argv[i], O_CREAT | O_WRONLY);
        if (fd < 0) {
            status = 1;
            continue;
        }
        close(fd);
    }
    if (status) swrite(2, "touch: cannot create file\n");
    return status;
}

static int copy_file_path(const char* source, const char* target) {
    user_stat_t source_info;
    user_stat_t target_info;
    if (stat(source, &source_info) < 0 || source_info.type != 1) return -1;
    if (stat(target, &target_info) == 0 && target_info.inum == source_info.inum)
        return -1;

    int input = open(source, O_RDONLY);
    if (input < 0) return -1;
    int output = open(target, O_CREAT | O_WRONLY | O_TRUNC);
    if (output < 0) {
        close(input);
        return -1;
    }
    uint8_t buffer[256];
    int result = 0;
    for (;;) {
        int n = read(input, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0 || write_all(output, buffer, (uint32_t)n) < 0) {
            result = -1;
            break;
        }
    }
    close(input);
    close(output);
    return result;
}

static int copy_tree(const char* source, const char* target, int depth) {
    if (depth > COPY_DEPTH_MAX) return -1;
    user_stat_t source_info;
    if (stat(source, &source_info) < 0) return -1;
    if (source_info.type != 2) return copy_file_path(source, target);

    user_stat_t target_info;
    if (stat(target, &target_info) < 0) {
        if (mkdir(target) < 0) return -1;
    } else if (target_info.type != 2) {
        return -1;
    } else if (target_info.inum == source_info.inum) {
        /* 修复: 拒绝目录自拷贝 (cp -r dir dir 会自嵌套直到深度上限) */
        swrite(2, "cp: cannot copy directory into itself\n");
        return -1;
    }

    user_dirent_t entry;
    for (uint32_t index = 0; ; ++index) {
        int got = readdir(source, index, &entry);
        if (got == 0) break;
        if (got < 0) return -1;
        if (!entry.inum || scmp(entry.name, ".") == 0 ||
            scmp(entry.name, "..") == 0) continue;
        char source_child[LINE_MAX];
        char target_child[LINE_MAX];
        if (join_path(source_child, sizeof(source_child),
                      source, entry.name) < 0 ||
            join_path(target_child, sizeof(target_child),
                      target, entry.name) < 0 ||
            copy_tree(source_child, target_child, depth + 1) < 0)
            return -1;
    }
    return 0;
}

static int utility_cp(const command_t* cmd) {
    int recursive = 0;
    int arg = 1;
    while (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
           cmd->argv[arg][1]) {
        for (int i = 1; cmd->argv[arg][i]; ++i) {
            if (cmd->argv[arg][i] == 'r' || cmd->argv[arg][i] == 'R')
                recursive = 1;
            else {
                swrite(2, "cp: usage: cp [-r] source target\n");
                return 1;
            }
        }
        ++arg;
    }
    if (cmd->argc - arg != 2) {
        swrite(2, "cp: usage: cp [-r] source target\n");
        return 1;
    }
    const char* source = cmd->argv[arg];
    const char* target_arg = cmd->argv[arg + 1];
    user_stat_t source_info;
    if (stat(source, &source_info) < 0) {
        swrite(2, "cp: source not found\n");
        return 1;
    }
    if (source_info.type == 2 && !recursive) {
        swrite(2, "cp: source is a directory (use -r)\n");
        return 1;
    }

    char target[LINE_MAX];
    user_stat_t target_info;
    if (stat(target_arg, &target_info) == 0 && target_info.type == 2) {
        if (join_path(target, sizeof(target), target_arg,
                      basename(source)) < 0) {
            swrite(2, "cp: path too long\n");
            return 1;
        }
    } else {
        scopy(target, target_arg, sizeof(target));
    }
    if (copy_tree(source, target, 0) < 0) {
        swrite(2, "cp: copy failed\n");
        return 1;
    }
    return 0;
}

static int utility_head(const command_t* cmd) {
    int count = 10;
    int arg = 1;
    if (arg < cmd->argc && scmp(cmd->argv[arg], "-n") == 0) {
        if (arg + 1 >= cmd->argc ||
            parse_number(cmd->argv[arg + 1], &count) < 0 || count < 0) {
            swrite(2, "head: usage: head [-n N] [file...]\n");
            return 1;
        }
        arg += 2;
    } else if (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
               cmd->argv[arg][1] >= '0' && cmd->argv[arg][1] <= '9') {
        if (parse_number(cmd->argv[arg] + 1, &count) < 0) return 1;
        ++arg;
    }

    int files = cmd->argc - arg;
    if (!files) {
        /* 修复: 裸 head 从键盘控制台读永不 EOF, 会永久挂起。
         * 只有管道输入或 < 重定向时才允许读 stdin */
        if (!cmd->pipe_input && !cmd->input) {
            swrite(2, "head: no input (usage: head [-n N] FILE, or pipe into head)\n");
            return 1;
        }
        char line[LINE_MAX];
        for (int i = 0; i < count && read_line_fd(0, line, sizeof(line));
             ++i) {
            swrite(1, line);
            swrite(1, "\n");
        }
        return 0;
    }

    int status = 0;
    for (int file = arg; file < cmd->argc; ++file) {
        int fd = open(cmd->argv[file], O_RDONLY);
        if (fd < 0) {
            swrite(2, "head: cannot open: ");
            swrite(2, cmd->argv[file]);
            swrite(2, "\n");
            status = 1;
            continue;
        }
        if (files > 1) {
            swrite(1, "==> ");
            swrite(1, cmd->argv[file]);
            swrite(1, " <==\n");
        }
        char line[LINE_MAX];
        for (int i = 0; i < count && read_line_fd(fd, line, sizeof(line));
             ++i) {
            swrite(1, line);
            swrite(1, "\n");
        }
        close(fd);
    }
    return status;
}

static int copy_fd_range(int fd, int start, int end) {
    if (lseek(fd, start, SEEK_SET) < 0) return 1;
    uint8_t buffer[256];
    int left = end - start;
    while (left > 0) {
        uint32_t want = left > (int)sizeof(buffer) ?
                        sizeof(buffer) : (uint32_t)left;
        int n = read(fd, buffer, want);
        if (n <= 0 || write_all(1, buffer, (uint32_t)n) < 0) return 1;
        left -= n;
    }
    return 0;
}

static int tail_seekable(int fd, int count) {
    int end = lseek(fd, 0, SEEK_END);
    if (end < 0) return -1;
    if (count == 0) return 0;
    int start = 0;
    int lines = 0;
    for (int pos = end - 1; pos >= 0; --pos) {
        char c;
        if (lseek(fd, pos, SEEK_SET) < 0 || read(fd, &c, 1) != 1)
            return 1;
        if (c == '\n' && pos != end - 1) {
            ++lines;
            start = pos + 1;
            if (lines >= count) break;
        }
    }
    if (lines < count) start = 0;
    return copy_fd_range(fd, start, end);
}

static int tail_streaming(int fd, int count) {
    if (count == 0) return 0;
    static char ring[TAIL_RING_MAX][LINE_MAX];
    int used = 0;
    int next = 0;
    if (count > TAIL_RING_MAX) count = TAIL_RING_MAX;
    char line[LINE_MAX];
    while (read_line_fd(fd, line, sizeof(line))) {
        scopy(ring[next], line, LINE_MAX);
        next = (next + 1) % TAIL_RING_MAX;
        if (used < TAIL_RING_MAX) ++used;
    }
    /* 修复: 只输出最后 count 行 (此前忽略 -n 打印了全部缓冲行) */
    int shown = used < count ? used : count;
    int start = (next - shown + TAIL_RING_MAX) % TAIL_RING_MAX;
    for (int i = 0; i < shown; ++i) {
        int index = (start + i) % TAIL_RING_MAX;
        swrite(1, ring[index]);
        swrite(1, "\n");
    }
    return 0;
}

static int utility_tail(const command_t* cmd) {
    int count = 10;
    int arg = 1;
    if (arg < cmd->argc && scmp(cmd->argv[arg], "-n") == 0) {
        if (arg + 1 >= cmd->argc ||
            parse_number(cmd->argv[arg + 1], &count) < 0 || count < 0) {
            swrite(2, "tail: usage: tail [-n N] [file...]\n");
            return 1;
        }
        arg += 2;
    } else if (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
               cmd->argv[arg][1] >= '0' && cmd->argv[arg][1] <= '9') {
        if (parse_number(cmd->argv[arg] + 1, &count) < 0) return 1;
        ++arg;
    }

    if (arg == cmd->argc) {
        /* 修复: 裸 tail 从键盘控制台读永不 EOF, 会永久挂起 */
        if (!cmd->pipe_input && !cmd->input) {
            swrite(2, "tail: no input (usage: tail [-n N] FILE, or pipe into tail)\n");
            return 1;
        }
        int result = tail_seekable(0, count);
        return result < 0 ? tail_streaming(0, count) : result;
    }
    int status = 0;
    for (int file = arg; file < cmd->argc; ++file) {
        int fd = open(cmd->argv[file], O_RDONLY);
        if (fd < 0) {
            swrite(2, "tail: cannot open: ");
            swrite(2, cmd->argv[file]);
            swrite(2, "\n");
            status = 1;
            continue;
        }
        if (tail_seekable(fd, count) != 0) status = 1;
        close(fd);
    }
    return status;
}

static int word_count_fd(int fd, uint32_t* lines, uint32_t* words,
                         uint32_t* bytes) {
    uint8_t buffer[256];
    int in_word = 0;
    for (;;) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) return -1;
        *bytes += (uint32_t)n;
        for (int i = 0; i < n; ++i) {
            char c = (char)buffer[i];
            if (c == '\n') ++*lines;
            if (is_space(c)) {
                in_word = 0;
            } else if (!in_word) {
                ++*words;
                in_word = 1;
            }
        }
    }
    return 0;
}

static int utility_wc(const command_t* cmd) {
    int show_lines = 0;
    int show_words = 0;
    int show_bytes = 0;
    int arg = 1;
    while (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
           cmd->argv[arg][1]) {
        for (int i = 1; cmd->argv[arg][i]; ++i) {
            if (cmd->argv[arg][i] == 'l') show_lines = 1;
            else if (cmd->argv[arg][i] == 'w') show_words = 1;
            else if (cmd->argv[arg][i] == 'c') show_bytes = 1;
            else {
                swrite(2, "wc: usage: wc [-lwc] [file...]\n");
                return 1;
            }
        }
        ++arg;
    }
    if (!show_lines && !show_words && !show_bytes)
        show_lines = show_words = show_bytes = 1;

    int files = cmd->argc - arg;
    uint32_t total_lines = 0;
    uint32_t total_words = 0;
    uint32_t total_bytes = 0;
    int status = 0;
    int processed = 0;
    for (int file = arg; file < (files ? cmd->argc : 0); ++file) {
        int fd = open(cmd->argv[file], O_RDONLY);
        if (fd < 0) {
            swrite(2, "wc: cannot open: ");
            swrite(2, cmd->argv[file]);
            swrite(2, "\n");
            status = 1;
            continue;
        }
        uint32_t lines = 0, words = 0, bytes = 0;
        if (word_count_fd(fd, &lines, &words, &bytes) < 0) status = 1;
        close(fd);
        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
        if (show_lines) { swrite_dec(1, lines); swrite(1, " "); }
        if (show_words) { swrite_dec(1, words); swrite(1, " "); }
        if (show_bytes) { swrite_dec(1, bytes); swrite(1, " "); }
        swrite(1, cmd->argv[file]);
        swrite(1, "\n");
        ++processed;
    }
    if (!files) {
        /* 修复: 裸 wc 从键盘控制台读永不 EOF, 会永久挂起 */
        if (!cmd->pipe_input && !cmd->input) {
            swrite(2, "wc: no input (usage: wc [-lwc] FILE, or pipe into wc)\n");
            return 1;
        }
        if (word_count_fd(0, &total_lines, &total_words, &total_bytes) < 0)
            return 1;
        processed = 1;
    }
    if (files > 1 && processed > 1) {
        if (show_lines) { swrite_dec(1, total_lines); swrite(1, " "); }
        if (show_words) { swrite_dec(1, total_words); swrite(1, " "); }
        if (show_bytes) { swrite_dec(1, total_bytes); swrite(1, " "); }
        swrite(1, "total\n");
    } else if (!files) {
        if (show_lines) { swrite_dec(1, total_lines); swrite(1, " "); }
        if (show_words) { swrite_dec(1, total_words); swrite(1, " "); }
        if (show_bytes) { swrite_dec(1, total_bytes); }
        swrite(1, "\n");
    }
    return status;
}

static char lower_char(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int contains_text(const char* text, const char* needle, int ignore_case) {
    if (!*needle) return 1;
    for (int i = 0; text[i]; ++i) {
        int j = 0;
        while (needle[j]) {
            char a = text[i + j];
            char b = needle[j];
            if (ignore_case) {
                a = lower_char(a);
                b = lower_char(b);
            }
            if (!a || a != b) break;
            ++j;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int grep_fd(int fd, const char* pattern, const char* label,
                   int show_line_numbers, int ignore_case, int* matched) {
    char line[LINE_MAX];
    int line_number = 0;
    while (read_line_fd(fd, line, sizeof(line))) {
        ++line_number;
        if (!contains_text(line, pattern, ignore_case)) continue;
        *matched = 1;
        if (label) {
            swrite(1, label);
            swrite(1, ":");
        }
        if (show_line_numbers) {
            swrite_dec(1, (uint32_t)line_number);
            swrite(1, ":");
        }
        swrite(1, line);
        swrite(1, "\n");
    }
    return 0;
}

static int utility_grep(const command_t* cmd) {
    int show_line_numbers = 0;
    int ignore_case = 0;
    int arg = 1;
    while (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
           cmd->argv[arg][1]) {
        for (int i = 1; cmd->argv[arg][i]; ++i) {
            if (cmd->argv[arg][i] == 'n') show_line_numbers = 1;
            else if (cmd->argv[arg][i] == 'i') ignore_case = 1;
            else {
                swrite(2, "grep: usage: grep [-in] pattern [file...]\n");
                return 2;
            }
        }
        ++arg;
    }
    if (arg >= cmd->argc) {
        swrite(2, "grep: missing pattern\n");
        return 2;
    }
    const char* pattern = cmd->argv[arg++];
    int matched = 0;
    int files = cmd->argc - arg;
    if (!files) {
        /* 修复: 裸 grep 从键盘控制台读永不 EOF, 会永久挂起 */
        if (!cmd->pipe_input && !cmd->input) {
            swrite(2, "grep: no input (usage: grep [-in] PATTERN FILE, or pipe into grep)\n");
            return 2;
        }
        grep_fd(0, pattern, 0, show_line_numbers, ignore_case, &matched);
        return matched ? 0 : 1;
    }
    int status = 0;
    for (; arg < cmd->argc; ++arg) {
        int fd = open(cmd->argv[arg], O_RDONLY);
        if (fd < 0) {
            swrite(2, "grep: cannot open: ");
            swrite(2, cmd->argv[arg]);
            swrite(2, "\n");
            status = 2;
            continue;
        }
        grep_fd(fd, pattern, files > 1 ? cmd->argv[arg] : 0,
                show_line_numbers, ignore_case, &matched);
        close(fd);
    }
    if (status) return status;
    return matched ? 0 : 1;
}

static int utility_printf(const command_t* cmd) {
    if (cmd->argc < 2) {
        swrite(2, "printf: missing format\n");
        return 1;
    }
    const char* format = cmd->argv[1];
    int argument = 2;
    for (int i = 0; format[i]; ++i) {
        char c = format[i];
        if (c == '\\' && format[i + 1]) {
            c = format[++i];
            if (c == 'n') swrite_char(1, '\n');
            else if (c == 'r') swrite_char(1, '\r');
            else if (c == 't') swrite_char(1, '\t');
            else if (c == '0') swrite_char(1, '\0');
            else swrite_char(1, c);
            continue;
        }
        if (c != '%' || !format[i + 1]) {
            swrite_char(1, c);
            continue;
        }
        c = format[++i];
        if (c == '%') {
            swrite_char(1, '%');
            continue;
        }
        const char* value = argument < cmd->argc ?
                            cmd->argv[argument] : "";
        if (argument < cmd->argc) ++argument;
        if (c == 's') {
            swrite(1, value);
        } else if (c == 'c') {
            swrite_char(1, value[0]);
        } else if (c == 'd' || c == 'u') {
            int number = 0;
            parse_signed_number(value, &number);
            if (c == 'd' && number < 0) {
                swrite_char(1, '-');
                number = -number;
            }
            swrite_dec(1, (uint32_t)number);
        } else if (c == 'x') {
            int number = 0;
            parse_signed_number(value, &number);
            swrite_hex(1, (uint32_t)number, 8);
        } else {
            swrite_char(1, '%');
            swrite_char(1, c);
        }
    }
    return 0;
}

static int test_expression(char* const* argv, int count) {
    if (count > 0 && scmp(argv[0], "!") == 0) {
        int result = test_expression(argv + 1, count - 1);
        return result == 0 ? 1 : result == 1 ? 0 : 2;
    }
    if (count == 0) return 1;
    if (count == 1) return argv[0][0] ? 0 : 1;
    if (count == 2) {
        user_stat_t info;
        if (scmp(argv[0], "-e") == 0) return stat(argv[1], &info) < 0;
        if (scmp(argv[0], "-f") == 0)
            return stat(argv[1], &info) < 0 || info.type != 1;
        if (scmp(argv[0], "-d") == 0)
            return stat(argv[1], &info) < 0 || info.type != 2;
        if (scmp(argv[0], "-z") == 0) return argv[1][0] ? 1 : 0;
        if (scmp(argv[0], "-n") == 0) return argv[1][0] ? 0 : 1;
        return 2;
    }
    if (count != 3) return 2;
    if (scmp(argv[1], "=") == 0) return scmp(argv[0], argv[2]) ? 1 : 0;
    if (scmp(argv[1], "!=") == 0) return scmp(argv[0], argv[2]) ? 0 : 1;
    int left, right;
    if (parse_signed_number(argv[0], &left) < 0 ||
        parse_signed_number(argv[2], &right) < 0)
        return 2;
    if (scmp(argv[1], "-eq") == 0) return left == right ? 0 : 1;
    if (scmp(argv[1], "-ne") == 0) return left != right ? 0 : 1;
    if (scmp(argv[1], "-lt") == 0) return left < right ? 0 : 1;
    if (scmp(argv[1], "-le") == 0) return left <= right ? 0 : 1;
    if (scmp(argv[1], "-gt") == 0) return left > right ? 0 : 1;
    if (scmp(argv[1], "-ge") == 0) return left >= right ? 0 : 1;
    return 2;
}

static int utility_test(const command_t* cmd) {
    int first = 1;
    int last = cmd->argc;
    if (scmp(basename(cmd->argv[0]), "[") == 0) {
        if (last <= first || scmp(cmd->argv[last - 1], "]") != 0) {
            swrite(2, "[: missing ]\n");
            return 2;
        }
        --last;
    }
    return test_expression(&cmd->argv[first], last - first);
}

static int utility_read(const command_t* cmd) {
    if (cmd->argc != 2 || !env_valid_name(cmd->argv[1], slen(cmd->argv[1]))) {
        swrite(2, "read: usage: read NAME\n");
        return 2;
    }
    char value[ENV_TEXT_MAX];
    int len = 0;
    int got = 0;
    int fd = 0;
    if (cmd->input) {
        fd = open(cmd->input, O_RDONLY);
        if (fd < 0) {
            swrite(2, "read: cannot open input\n");
            return 1;
        }
    }
    for (;;) {
        char c;
        int n = read_byte(fd, &c);
        if (n <= 0) break;
        got = 1;
        if (c == '\n') break;
        if (c != '\r' && len < (int)sizeof(value) - 1)
            value[len++] = c;
    }
    if (fd != 0) close(fd);
    value[len] = '\0';
    /* 修复: 从文件/管道读取时不输出换行, 避免污染脚本输出 */
    if (!cmd->input) swrite(1, "\n");
    if (!got && !len) return 1;
    return env_put(cmd->argv[1], slen(cmd->argv[1]), value, 0) < 0 ? 1 : 0;
}

static int builtin_known(const char* name) {
    for (int i = 0; shell_builtin_names[i]; ++i)
        if (scmp(name, shell_builtin_names[i]) == 0) return 1;
    return 0;
}

static int utility_which(const command_t* cmd, int verbose) {
    if (cmd->argc < 2) {
        swrite(2, "which: missing command\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i) {
        const char* name = cmd->argv[i];
        if (builtin_known(name)) {
            if (verbose) {
                swrite(1, name);
                swrite(1, ": shell builtin\n");
            } else {
                swrite(1, name);
                swrite(1, "\n");
            }
            continue;
        }
        char path[LINE_MAX];
        const char* candidate = name;
        /* 修复: 含 '/' 的名称按路径直接检查 (which ./hello),
         * 只有纯命令名才拼 /bin 前缀 */
        int has_slash = 0;
        for (int k = 0; name[k]; ++k)
            if (name[k] == '/') { has_slash = 1; break; }
        if (!has_slash) {
            if (join_path(path, sizeof(path), "/bin", name) < 0) {
                status = 1;
                continue;
            }
            candidate = path;
        }
        user_stat_t info;
        if (stat(candidate, &info) == 0 && info.type == 1) {
            swrite(1, candidate);
            swrite(1, "\n");
        } else {
            status = 1;
        }
    }
    return status;
}

static int utility_sleep(const command_t* cmd) {
    int seconds;
    if (cmd->argc != 2 || parse_number(cmd->argv[1], &seconds) < 0) {
        swrite(2, "sleep: usage: sleep SECONDS\n");
        return 1;
    }
    /* 修复: seconds*18 在 uint32 中回绕 (sleep 8 年变几天);
     * 超出范围时饱和为最大滴答数 */
    uint32_t ticks;
    if (seconds > 238609294) ticks = 0xFFFFFFFFu;
    else ticks = (uint32_t)seconds * 18u;
    sleep(ticks);
    return 0;
}

static int utility_uptime(void) {
    uint32_t tick_count = ticks();
    swrite(1, "uptime: ");
    swrite_dec(1, tick_count / 18u);
    swrite(1, "s (");
    swrite_dec(1, tick_count);
    swrite(1, " ticks)\n");
    return 0;
}

static int hexdump_one(const char* path) {
    int fd = path ? open(path, O_RDONLY) : 0;
    if (fd < 0) return 1;
    uint8_t buffer[16];
    uint32_t offset = 0;
    for (;;) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        swrite_hex(1, offset, 8);
        swrite(1, "  ");
        for (int i = 0; i < 16; ++i) {
            if (i < n) swrite_hex(1, buffer[i], 2);
            else swrite(1, "  ");
            swrite(1, " ");
        }
        swrite(1, " |");
        for (int i = 0; i < n; ++i) {
            char c = (char)buffer[i];
            swrite_char(1, c >= 32 && c < 127 ? c : '.');
        }
        swrite(1, "|\n");
        offset += (uint32_t)n;
    }
    if (path && fd != 0) close(fd);
    return 0;
}

static int utility_hexdump(const command_t* cmd) {
    if (cmd->argc == 1) {
        /* 修复: 裸 hexdump 从键盘控制台读永不 EOF (与 tail/wc/grep 同类挂起) */
        if (!cmd->pipe_input && !cmd->input) {
            swrite(2, "hexdump: no input (usage: hexdump FILE, or pipe into hexdump)\n");
            return 1;
        }
        return hexdump_one(0);
    }
    int status = 0;
    for (int i = 1; i < cmd->argc; ++i)
        if (hexdump_one(cmd->argv[i])) status = 1;
    return status;
}

static int utility_mkdir(const command_t* cmd) {
    int parents = 0;
    int arg = 1;
    if (arg < cmd->argc && scmp(cmd->argv[arg], "-p") == 0) {
        parents = 1;
        ++arg;
    }
    if (arg >= cmd->argc) {
        swrite(2, "mkdir: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = arg; i < cmd->argc; ++i) {
        if (!parents) {
            if (mkdir(cmd->argv[i]) < 0) status = 1;
            continue;
        }
        char path[LINE_MAX];
        scopy(path, cmd->argv[i], sizeof(path));
        int len = slen(path);
        while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
        for (int j = 1; j < len; ++j) {
            if (path[j] != '/') continue;
            path[j] = '\0';
            mkdir(path);
            path[j] = '/';
        }
        user_stat_t info;
        if (stat(path, &info) == 0) {
            if (info.type != 2) status = 1;
        } else if (mkdir(path) < 0) {
            status = 1;
        }
    }
    return status;
}

static int remove_tree(const char* path, int recursive, int force) {
    user_stat_t info;
    if (stat(path, &info) < 0) return force ? 0 : 1;
    if (info.type != 2 || !recursive) return remove(path) < 0 ? 1 : 0;
    /* 修复: 除了字面量检查, 再按 inode 与 "/" 比对 —
     * "rm -rf /shdir/.." 这类路径由内核解析后指向根目录,
     * 必须拦住, 否则整个文件系统会被清空 */
    user_stat_t root_info;
    int is_root = 0;
    if (stat("/", &root_info) == 0 && root_info.inum == info.inum)
        is_root = 1;
    if (is_root || scmp(path, ".") == 0 || scmp(path, "..") == 0) {
        swrite(2, "rm: refusing to remove root or current directory\n");
        return 1;
    }
    for (;;) {
        user_dirent_t entry;
        int found = 0;
        for (uint32_t index = 0; ; ++index) {
            int got = readdir(path, index, &entry);
            if (got <= 0) break;
            if (!entry.inum || scmp(entry.name, ".") == 0 ||
                scmp(entry.name, "..") == 0) continue;
            found = 1;
            break;
        }
        if (!found) break;
        char child[LINE_MAX];
        if (join_path(child, sizeof(child), path, entry.name) < 0 ||
            remove_tree(child, 1, force))
            return 1;
    }
    return remove(path) < 0 ? 1 : 0;
}

static int utility_rm(const command_t* cmd) {
    int recursive = 0;
    int force = 0;
    int arg = 1;
    while (arg < cmd->argc && cmd->argv[arg][0] == '-' &&
           cmd->argv[arg][1]) {
        for (int i = 1; cmd->argv[arg][i]; ++i) {
            if (cmd->argv[arg][i] == 'r' || cmd->argv[arg][i] == 'R')
                recursive = 1;
            else if (cmd->argv[arg][i] == 'f') force = 1;
            else {
                swrite(2, "rm: usage: rm [-rf] path...\n");
                return 2;
            }
        }
        ++arg;
    }
    if (arg >= cmd->argc) {
        swrite(2, "rm: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = arg; i < cmd->argc; ++i)
        if (remove_tree(cmd->argv[i], recursive, force)) status = 1;
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
    swrite_dec(1, info.size);
    swrite(1, " nlink=");
    swrite_dec(1, info.nlink);
    swrite(1, " inode=");
    swrite_dec(1, info.inum);
    swrite(1, "\n");
    return 0;
}

static int execute_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    if (scmp(name, "echo") == 0) return utility_echo(cmd);
    if (scmp(name, "pwd") == 0) return utility_pwd();
    if (scmp(name, "ls") == 0) return utility_ls(cmd);
    if (scmp(name, "cat") == 0) return utility_cat(cmd);
    if (scmp(name, "touch") == 0) return utility_touch(cmd);
    if (scmp(name, "cp") == 0) return utility_cp(cmd);
    if (scmp(name, "head") == 0) return utility_head(cmd);
    if (scmp(name, "tail") == 0) return utility_tail(cmd);
    if (scmp(name, "wc") == 0) return utility_wc(cmd);
    if (scmp(name, "grep") == 0) return utility_grep(cmd);
    if (scmp(name, "printf") == 0) return utility_printf(cmd);
    if (scmp(name, "test") == 0) return utility_test(cmd);
    if (scmp(name, "[") == 0) return utility_test(cmd);
    if (scmp(name, "read") == 0) return utility_read(cmd);
    if (scmp(name, "sleep") == 0) return utility_sleep(cmd);
    if (scmp(name, "uptime") == 0) return utility_uptime();
    if (scmp(name, "hexdump") == 0) return utility_hexdump(cmd);
    if (scmp(name, "which") == 0) return utility_which(cmd, 0);
    if (scmp(name, "type") == 0) return utility_which(cmd, 1);
    if (scmp(name, "clear") == 0) {
        /* 修复: [SH] clear 标记从内核 SYS_CLEAR 移到命令层;
         * 先输出标记再清屏, 串口测试可见且屏幕保持干净 */
        swrite(1, "[SH] clear\n\r");
        clear_screen();
        return 0;
    }
    if (scmp(name, "source") == 0 || scmp(name, ".") == 0) {
        if (cmd->argc != 2) {
            swrite(2, "source: usage: source FILE\n");
            return 2;
        }
        return execute_script_file(cmd->argv[1]);
    }
    if (scmp(name, "mkdir") == 0) return utility_mkdir(cmd);
    if (scmp(name, "rm") == 0) return utility_rm(cmd);
    if (scmp(name, "mv") == 0) return utility_mv(cmd);
    if (scmp(name, "ln") == 0) return utility_ln(cmd);
    if (scmp(name, "truncate") == 0) return utility_truncate(cmd);
    if (scmp(name, "stat") == 0) return utility_stat(cmd);
    if (scmp(name, "env") == 0) {
        for (int i = 0; i < shell_env_count; ++i)
            if (shell_env_exported[i]) {
                swrite(1, shell_env_storage[i]);
                swrite(1, "\n");
            }
        return 0;
    }
    if (scmp(name, "set") == 0) {
        for (int i = 0; i < shell_env_count; ++i) {
            swrite(1, shell_env_storage[i]);
            swrite(1, "\n");
        }
        return 0;
    }
    if (scmp(name, "export") == 0) {
        for (int i = 1; i < cmd->argc; ++i) {
            if (is_assignment_word(cmd->argv[i])) {
                if (env_set_word(cmd->argv[i], 1) < 0) return 1;
            } else {
                int len = slen(cmd->argv[i]);
                if (!env_valid_name(cmd->argv[i], len)) return 1;
                int index = env_find(cmd->argv[i], len);
                if (index < 0 && env_put(cmd->argv[i], len, "", 1) < 0)
                    return 1;
                else if (index >= 0)
                    shell_env_exported[index] = 1;
            }
        }
        return 0;
    }
    if (scmp(name, "unset") == 0) {
        for (int i = 1; i < cmd->argc; ++i) {
            int len = slen(cmd->argv[i]);
            if (!env_valid_name(cmd->argv[i], len) ||
                env_unset(cmd->argv[i], len) < 0)
                return 1;
        }
        return 0;
    }
    if (scmp(name, "true") == 0) return 0;
    if (scmp(name, "false") == 0) return 1;
    if (scmp(name, "history") == 0) {
        if (cmd->argc == 2 && scmp(cmd->argv[1], "-c") == 0) {
            history_count = 0;
            history_index = 0;
            int fd = open(HISTORY_FILE, O_CREAT | O_WRONLY | O_TRUNC);
            if (fd < 0) return 1;
            close(fd);
            return 0;
        }
        int start = 0;
        if (cmd->argc > 1) {
            int recent;
            if (parse_number(cmd->argv[1], &recent) < 0 || recent <= 0)
                return 1;
            start = history_count - recent;
            if (start < 0) start = 0;
        }
        for (int i = start; i < history_count; ++i) {
            putdec((uint32_t)(i + 1));
            swrite(1, " ");
            swrite(1, history[i]);
            swrite(1, "\n");
        }
        return 0;
    }
    if (scmp(name, "cd") == 0) {
        char current[LINE_MAX];
        if (!getcwd(current, sizeof(current))) current[0] = '\0';
        const char* path = cmd->argc > 1 ? cmd->argv[1] : "/";
        int show_path = 0;
        if (scmp(path, "-") == 0) {
            path = previous_cwd;
            show_path = 1;
        }
        char target[LINE_MAX];
        scopy(target, path, sizeof(target));
        if (chdir(target) < 0) {
            swrite(2, "cd: no such directory\n");
            return 1;
        }
        if (current[0]) scopy(previous_cwd, current, sizeof(previous_cwd));
        if (show_path) {
            swrite(1, target);
            swrite(1, "\n");
        }
        return 0;
    }
    if (scmp(name, "help") == 0) {
        swrite(1, "shell: cd exit jobs fg bg kill wait history read source export unset set help\n");
        swrite(1, "files: echo pwd ls cat touch cp mkdir rm mv ln truncate stat hexdump\n");
        swrite(1, "text: head tail wc grep printf test [ which type clear sleep uptime env\n");
        swrite(1, "kernel: exit -> OS shell; then cc edit vi asm debug tetris mario font\n");
        swrite(1, "syntax: 'quotes' \"$VAR\" a\\ b; cmd [> out] [2> err] [2>&1] [| cmd] [&]\n");
        return 0;
    }
    return -1;
}

static void exec_external(const command_t* cmd) {
    char* envp[9];
    int envc = 0;
    for (int i = 0; i < shell_env_count && envc < 8; ++i)
        if (shell_env_exported[i]) envp[envc++] = shell_env_storage[i];
    envp[envc] = 0;
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

/* 修复: 父内置命令在 shell 进程内执行, 不会走 setup_redirections,
 * 导致 history/read/source 等的输出重定向被静默忽略。
 * 这里手动应用输出/错误重定向, 执行后恢复 fd 1/2。
 * (输入重定向由各内置命令自行处理: read 读 cmd->input 等) */
static int run_parent_builtin(const command_t* cmd) {
    int saved_out = -1;
    int saved_err = -1;
    if (cmd->output) {
        uint32_t flags = O_CREAT | O_WRONLY |
                         (cmd->append ? O_APPEND : O_TRUNC);
        saved_out = dup(1);
        int fd = open(cmd->output, flags);
        if (fd < 0 || dup2(fd, 1) < 0) {
            swrite(2, "sh: redirection failed\n");
            if (saved_out >= 0) { dup2(saved_out, 1); close(saved_out); }
            return 1;
        }
        if (fd != 1) close(fd);
    }
    if (cmd->stderr_to_stdout) {
        saved_err = dup(2);
        if (dup2(1, 2) < 0) saved_err = -1;
    } else if (cmd->error_output) {
        uint32_t flags = O_CREAT | O_WRONLY |
                         (cmd->error_append ? O_APPEND : O_TRUNC);
        saved_err = dup(2);
        int fd = open(cmd->error_output, flags);
        if (fd < 0 || dup2(fd, 2) < 0) {
            swrite(2, "sh: redirection failed\n");
            if (saved_err >= 0) { dup2(saved_err, 2); close(saved_err); }
            saved_err = -1;
        } else if (fd != 2) {
            close(fd);
        }
    }
    int result = execute_builtin(cmd);
    if (result < 0) {
        exec_external(cmd);
        result = 127;
    }
    if (saved_out >= 0) { dup2(saved_out, 1); close(saved_out); }
    if (saved_err >= 0) { dup2(saved_err, 2); close(saved_err); }
    return result;
}

static int is_parent_builtin(const command_t* cmd) {
    const char* name = basename(cmd->argv[0]);
    return scmp(name, "cd") == 0 || scmp(name, "exit") == 0 ||
           scmp(name, "jobs") == 0 || scmp(name, "fg") == 0 ||
           scmp(name, "bg") == 0 || scmp(name, "kill") == 0 ||
           scmp(name, "wait") == 0 || scmp(name, "export") == 0 ||
           scmp(name, "unset") == 0 || scmp(name, "set") == 0 ||
           scmp(name, "history") == 0 || scmp(name, "read") == 0 ||
           scmp(name, "source") == 0 || scmp(name, ".") == 0;
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
            int code = last_status;
            if (cmds[0].argc > 1)
                parse_number(cmds[0].argv[1], &code);
            shell_exit_status = code & 0xFF;
            for (int i = 0; i < JOB_MAX; ++i)
                if (jobs[i].used) kill(-jobs[i].pgid, SIGTERM);
            return -1;
        }
        if (scmp(name, "jobs") == 0) return builtin_jobs();
        if (scmp(name, "fg") == 0) return builtin_fg(&cmds[0]);
        if (scmp(name, "bg") == 0) return builtin_bg(&cmds[0]);
        if (scmp(name, "kill") == 0) return builtin_kill(&cmds[0]);
        if (scmp(name, "wait") == 0) return builtin_wait();
        return run_parent_builtin(&cmds[0]);
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
        /* 修复: 标记管道输入, 供 head/tail/wc/grep/hexdump
         * 区分"管道 stdin"与"键盘控制台" */
        cmds[i].pipe_input = (i > 0);
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

static void edit_prompt(void) {
    if (continuation_prompt) swrite(1, "> ");
    else shell_prompt();
}

static int history_add(const char* line) {
    if (!line || !*line) return 0;
    if (history_count && scmp(history[history_count - 1], line) == 0) return 0;
    if (history_count == HISTORY_MAX) {
        for (int i = 1; i < HISTORY_MAX; ++i)
            scopy(history[i - 1], history[i], LINE_MAX);
        --history_count;
    }
    scopy(history[history_count++], line, LINE_MAX);
    history_index = history_count;
    return 1;
}

static void history_store(const char* line) {
    if (history_loading || !line || !*line) return;
    user_stat_t info;
    int fd;
    if (stat(HISTORY_FILE, &info) == 0 && info.size > HISTORY_LIMIT) {
        fd = open(HISTORY_FILE, O_CREAT | O_WRONLY | O_TRUNC);
        if (fd < 0) return;
        for (int i = 0; i < history_count; ++i) {
            write_all(fd, history[i], (uint32_t)slen(history[i]));
            write_all(fd, "\n", 1);
        }
    } else {
        fd = open(HISTORY_FILE, O_CREAT | O_WRONLY | O_APPEND);
        if (fd < 0) return;
        write_all(fd, line, (uint32_t)slen(line));
        write_all(fd, "\n", 1);
    }
    close(fd);
}

static void history_record(const char* line) {
    if (history_add(line)) history_store(line);
}

static void history_load(void) {
    int fd = open(HISTORY_FILE, O_RDONLY);
    if (fd < 0) return;
    char part[LINE_MAX];
    int part_len = 0;
    char buf[128];
    history_loading = 1;
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                part[part_len] = '\0';
                if (part_len && part[part_len - 1] == '\r')
                    part[--part_len] = '\0';
                history_add(part);
                part_len = 0;
            } else if (part_len < LINE_MAX - 1) {
                part[part_len++] = c;
            }
        }
    }
    if (part_len) {
        part[part_len] = '\0';
        if (part[part_len - 1] == '\r') part[part_len - 1] = '\0';
        history_add(part);
    }
    history_loading = 0;
    history_index = history_count;
    close(fd);
}

static void redraw_edit_line(const char* line, int len, int cursor,
                             int old_len) {
    int clear = len > old_len ? len : old_len;
    swrite(1, "\r");
    edit_prompt();
    write_all(1, line, (uint32_t)len);
    for (int i = len; i < clear; ++i) swrite(1, " ");
    for (int i = cursor; i < clear; ++i) swrite(1, "\b");
}

static void line_insert(char* line, int* len, int* cursor, char c) {
    if (*len >= LINE_MAX - 1) return;
    for (int i = *len; i > *cursor; --i) line[i] = line[i - 1];
    line[(*cursor)++] = c;
    ++*len;
    line[*len] = '\0';
}

static void line_insert_text(char* line, int* len, int* cursor,
                             const char* text, int count) {
    for (int i = 0; i < count; ++i)
        line_insert(line, len, cursor, text[i]);
}

static void line_delete_at(char* line, int* len, int cursor) {
    if (cursor < 0 || cursor >= *len) return;
    for (int i = cursor; i + 1 < *len; ++i)
        line[i] = line[i + 1];
    --*len;
    line[*len] = '\0';
}

static int find_substring(const char* text, const char* needle) {
    if (!*needle) return 1;
    for (int i = 0; text[i]; ++i) {
        int j = 0;
        while (needle[j] && text[i + j] == needle[j]) ++j;
        if (!needle[j]) return 1;
    }
    return 0;
}

static char history_draft[LINE_MAX];
static int history_draft_valid;

static int history_navigate(char* line, int* len, int* cursor, int up,
                            int old_len) {
    if (!history_count) return 0;
    if (up) {
        if (history_index == history_count && !history_draft_valid) {
            scopy(history_draft, line, LINE_MAX);
            history_draft_valid = 1;
        }
        if (history_index > 0) --history_index;
    } else {
        if (history_index < history_count) ++history_index;
    }
    if (history_index >= history_count && history_draft_valid) {
        scopy(line, history_draft, LINE_MAX);
        history_draft_valid = 0;
    } else if (history_index >= history_count) {
        line[0] = '\0';
    } else {
        scopy(line, history[history_index], LINE_MAX);
    }
    *len = slen(line);
    *cursor = *len;
    redraw_edit_line(line, *len, *cursor, old_len);
    return 1;
}

static int history_search_previous(const char* query, int before) {
    for (int i = before - 1; i >= 0; --i)
        if (find_substring(history[i], query)) return i;
    return -1;
}

static int search_redraw(const char* line, const char* query,
                         int old_shown) {
    const char* label = "(reverse-i-search)`";
    int shown = slen(label) + slen(query) + 3 + slen(line);
    int clear = shown > old_shown ? shown : old_shown;
    swrite(1, "\r");
    swrite(1, label);
    swrite(1, query);
    swrite(1, "': ");
    swrite(1, line);
    for (int i = shown; i < clear; ++i) swrite(1, " ");
    for (int i = shown; i < clear; ++i) swrite(1, "\b");
    return shown;
}

static char completion_storage[2048];
static char* completion_names[WORD_MAX];
static unsigned char completion_dirs[WORD_MAX];
static int completion_count;
static int completion_pos;

static int starts_with(const char* text, const char* prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static void completion_add(const char* name, const char* prefix, int is_dir) {
    if (!starts_with(name, prefix) || completion_count >= WORD_MAX) return;
    for (int i = 0; i < completion_count; ++i)
        if (scmp(completion_names[i], name) == 0) return;
    int len = slen(name);
    if (completion_pos + len + 1 > (int)sizeof(completion_storage)) return;
    char* stored = completion_storage + completion_pos;
    scopy(stored, name, sizeof(completion_storage) - completion_pos);
    completion_pos += len + 1;
    completion_names[completion_count] = stored;
    completion_dirs[completion_count++] = (unsigned char)is_dir;
}

static void completion_add_dir(const char* dir, const char* prefix,
                               int files_only) {
    if (!dir || !*dir) return;
    user_dirent_t entry;
    for (uint32_t idx = 0; ; ++idx) {
        int got = readdir(dir, idx, &entry);
        if (got <= 0) break;
        if (!entry.inum || scmp(entry.name, ".") == 0 ||
            scmp(entry.name, "..") == 0)
            continue;
        if (prefix[0] != '.' && entry.name[0] == '.') continue;
        if (files_only && entry.type == 2) continue;
        completion_add(entry.name, prefix, entry.type == 2);
    }
}

static void completion_list(char* line, int len, int* cursor) {
    swrite(1, "\n");
    for (int i = 0; i < completion_count; ++i) {
        swrite(1, completion_names[i]);
        if (completion_dirs[i]) swrite(1, "/");
        swrite(1, "  ");
    }
    swrite(1, "\n");
    edit_prompt();
    write_all(1, line, (uint32_t)len);
    for (int i = *cursor; i < len; ++i) swrite(1, "\b");
}

static void complete_line(char* line, int* len, int* cursor) {
    int start = 0;
    for (int i = 0; i < *cursor; ++i)
        if (is_space(line[i])) start = i + 1;
    char token[LINE_MAX];
    int token_len = *cursor - start;
    for (int i = 0; i < token_len; ++i) token[i] = line[start + i];
    token[token_len] = '\0';

    const char* base = token;
    char dir[LINE_MAX];
    int slash = -1;
    for (int i = 0; token[i]; ++i)
        if (token[i] == '/') slash = i;
    if (slash >= 0) {
        base = token + slash + 1;
        if (!slash) {
            dir[0] = '/';
            dir[1] = '\0';
        } else {
            for (int i = 0; i < slash; ++i) dir[i] = token[i];
            dir[slash] = '\0';
        }
    } else {
        dir[0] = '.';
        dir[1] = '\0';
    }

    completion_count = 0;
    completion_pos = 0;
    int command_position = start == 0;
    if (command_position && slash < 0) {
        for (int i = 0; shell_builtin_names[i]; ++i)
            completion_add(shell_builtin_names[i], base, 0);
        completion_add_dir("/bin", base, 0);
    } else if (!command_position || slash >= 0) {
        completion_add_dir(dir, base, 0);
    }
    if (!completion_count) {
        swrite(1, "\a");
        return;
    }

    int common = slen(completion_names[0]);
    for (int i = 1; i < completion_count; ++i) {
        int j = 0;
        while (j < common && completion_names[0][j] == completion_names[i][j])
            ++j;
        common = j;
    }
    int base_len = slen(base);
    if (common > base_len) {
        int add = common - base_len;
        if (*len + add >= LINE_MAX) return;
        int old_len = *len;
        line_insert_text(line, len, cursor, completion_names[0] + base_len,
                         add);
        redraw_edit_line(line, *len, *cursor, old_len);
    }
    if (completion_count == 1) {
        char suffix = completion_dirs[0] ? '/' : ' ';
        if (*len < LINE_MAX - 1) {
            int old_len = *len;
            line_insert(line, len, cursor, suffix);
            redraw_edit_line(line, *len, *cursor, old_len);
        }
    } else if (common <= base_len) {
        completion_list(line, *len, cursor);
    }
}

static int execute_segment(char* segment) {
    char* tokens[WORD_MAX];
    unsigned char globs[WORD_MAX];
    int nwords = tokenize(segment, tokens, WORD_MAX, globs);
    if (nwords < 0) {
        swrite(2, "sh: syntax error\n");
        return 2;
    }
    char* words[WORD_MAX];
    if (expand_words(tokens, nwords, globs, words, &nwords) < 0) {
        swrite(2, "sh: expansion failed\n");
        return 2;
    }

    int assignments = 0;
    while (assignments < nwords && is_assignment_word(words[assignments]))
        ++assignments;
    if (assignments == nwords) {
        for (int i = 0; i < assignments; ++i)
            if (env_set_word(words[i], 0) < 0) {
                swrite(2, "sh: invalid assignment\n");
                return 1;
            }
        last_status = 0;
        return 0;
    }

    char saved_env[ENV_MAX][ENV_TEXT_MAX];
    unsigned char saved_exports[ENV_MAX];
    int saved_count = 0;
    if (assignments) {
        env_snapshot(saved_env, saved_exports, &saved_count);
        for (int i = 0; i < assignments; ++i)
            if (env_set_word(words[i], 1) < 0) {
                env_restore(saved_env, saved_exports, saved_count);
                swrite(2, "sh: invalid assignment\n");
                return 1;
            }
    }

    command_t cmds[PIPE_MAX];
    int count = 0;
    int background = 0;
    int parsed = parse_commands(words + assignments, nwords - assignments,
                                cmds, &count, &background);
    if (parsed < 0) {
        if (assignments) env_restore(saved_env, saved_exports, saved_count);
        swrite(2, "sh: syntax error\n");
        return 2;
    }
    if (!parsed) {
        if (assignments) env_restore(saved_env, saved_exports, saved_count);
        return 0;
    }
    char command_text[JOB_TEXT];
    scopy(command_text, segment, sizeof(command_text));
    int status = execute_pipeline(cmds, count, background, command_text);
    if (assignments) env_restore(saved_env, saved_exports, saved_count);
    last_status = status < 0 ? 0 : status;
    return status;
}

static int capture_command(const char* command, char* out, int cap) {
    int fds[2];
    if (cap <= 0 || pipe(fds) < 0) return -1;
    int pid = fork();
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], 1) < 0) exit(1);
        close(fds[1]);
        char script[LINE_MAX];
        scopy(script, command, sizeof(script));
        int status = execute_line(script);
        exit(status < 0 ? 0 : status);
    }
    close(fds[1]);
    int used = 0;
    char buf[64];
    for (;;) {
        int n = read(fds[0], buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n && used < cap - 1; ++i)
            out[used++] = buf[i];
    }
    close(fds[0]);
    while (used && out[used - 1] == '\n') --used;
    out[used] = '\0';
    if (pid < 0) return -1;
    int status = 0;
    waitpid(pid, &status);
    return WEXITSTATUS(status);
}

static char* expand_command_substitutions(char* line) {
    int out = 0;
    char quote = 0;
    int escaped = 0;
    for (int i = 0; line[i] && out < SUBST_MAX - 1; ++i) {
        char c = line[i];
        if (escaped) {
            substitution_storage[out++] = c;
            escaped = 0;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            substitution_storage[out++] = c;
            escaped = 1;
            continue;
        }
        if (c == '\'' || c == '"') {
            if (!quote) quote = c;
            else if (quote == c) quote = 0;
            substitution_storage[out++] = c;
            continue;
        }
        if (c != '$' || line[i + 1] != '(' || quote == '\'') {
            substitution_storage[out++] = c;
            continue;
        }

        int depth = 1;
        int end = i + 2;
        char inner_quote = 0;
        int inner_escape = 0;
        for (; line[end] && depth; ++end) {
            char d = line[end];
            if (inner_escape) {
                inner_escape = 0;
                continue;
            }
            if (d == '\\' && inner_quote != '\'') {
                inner_escape = 1;
                continue;
            }
            if (d == '\'' || d == '"') {
                if (!inner_quote) inner_quote = d;
                else if (inner_quote == d) inner_quote = 0;
                continue;
            }
            if (!inner_quote && d == '(') ++depth;
            else if (!inner_quote && d == ')') --depth;
        }
        if (depth) {
            substitution_storage[out++] = c;
            continue;
        }
        end--;
        char inner[LINE_MAX];
        int inner_len = end - (i + 2);
        if (inner_len >= (int)sizeof(inner)) inner_len = sizeof(inner) - 1;
        for (int j = 0; j < inner_len; ++j)
            inner[j] = line[i + 2 + j];
        inner[inner_len] = '\0';
        char captured[SUBST_MAX];
        capture_command(inner, captured, sizeof(captured));
        for (int j = 0; captured[j] && out < SUBST_MAX - 1; ++j) {
            char d = captured[j];
            if (!quote && (is_space(d))) {
                substitution_storage[out++] = FIELD_SEP;
            } else if (!quote && (d == '*' || d == '?')) {
                substitution_storage[out++] = d;
            } else {
                if (out + 1 >= SUBST_MAX) break;
                substitution_storage[out++] = SUBST_LITERAL;
                substitution_storage[out++] = d;
            }
        }
        i = end;
    }
    substitution_storage[out] = '\0';
    return substitution_storage;
}

static int execute_line_raw(char* line) {
    char* start = line;
    char quote = 0;
    int escaped = 0;
    for (char* p = line; ; ++p) {
        char c = *p;
        if (c == SUBST_LITERAL && p[1]) {
            ++p;
            continue;
        }
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

static int execute_line(char* line) {
    return execute_line_raw(expand_command_substitutions(line));
}

static int input_incomplete(const char* line) {
    char quote = 0;
    int escaped = 0;
    int subst_depth = 0;
    for (int i = 0; line[i]; ++i) {
        char c = line[i];
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
        if (quote == '\'') continue;
        if (!subst_depth && c == '$' && line[i + 1] == '(') {
            subst_depth = 1;
            ++i;
        } else if (subst_depth && c == '(') {
            ++subst_depth;
        } else if (subst_depth && c == ')') {
            --subst_depth;
        }
    }
    return quote || escaped || subst_depth;
}

static int execute_script_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        swrite(2, "sh: cannot open script: ");
        swrite(2, path);
        swrite(2, "\n");
        return 127;
    }
    char line[LINE_MAX];
    int len = 0;
    char buf[64];
    int last = 0;
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n') {
                line[len] = '\0';
                int start = 0;
                while (is_space(line[start])) ++start;
                if (line[start] && line[start] != '#') {
                    int status = execute_line(line + start);
                    if (status < 0) {
                        close(fd);
                        return shell_exit_status;
                    }
                    last = status;
                }
                len = 0;
            } else if (len < LINE_MAX - 1) {
                line[len++] = c;
            }
        }
    }
    if (len) {
        line[len] = '\0';
        int start = 0;
        while (is_space(line[start])) ++start;
        if (line[start] && line[start] != '#')
            last = execute_line(line + start);
    }
    close(fd);
    return last;
}

static int shell_loop(void) {
    char line[LINE_MAX];
    int len = 0;
    int cursor = 0;
    int search_mode = 0;
    int search_match = -1;
    int search_shown = 0;
    char search_query[64];
    int search_len = 0;
    shell_prompt();
    for (;;) {
        char c;
        int n = read_byte(0, &c);

        if (search_mode) {
            if (c == '\x18' && n > 0) {
                int before = search_match >= 0 ?
                             search_match : history_count;
                search_match = history_search_previous(search_query, before);
                if (search_match >= 0) {
                    scopy(line, history[search_match], LINE_MAX);
                    len = slen(line);
                    cursor = len;
                }
                search_shown = search_redraw(line, search_query, search_shown);
                continue;
            }
            if ((c == '\n' || c == '\r') && n > 0) {
                search_mode = 0;
                if (search_match < 0 && history_draft_valid)
                    scopy(line, history_draft, LINE_MAX);
                else
                    history_index = search_match >= 0 ?
                                    search_match + 1 : history_count;
                len = slen(line);
                cursor = len;
                swrite(1, "\n");
                edit_prompt();
                write_all(1, line, (uint32_t)len);
                for (int i = cursor; i < len; ++i) swrite(1, "\b");
                continue;
            }
            if ((c == 27 || c == 3) && n > 0) {
                search_mode = 0;
                if (history_draft_valid) {
                    scopy(line, history_draft, LINE_MAX);
                    history_draft_valid = 0;
                }
                len = slen(line);
                cursor = len;
                history_index = history_count;
                swrite(1, "\n");
                edit_prompt();
                write_all(1, line, (uint32_t)len);
                for (int i = cursor; i < len; ++i) swrite(1, "\b");
                continue;
            }
            if (c == '\b' && n > 0) {
                if (search_len) --search_len;
                search_query[search_len] = '\0';
                search_match = history_search_previous(search_query,
                                                       history_count);
                if (search_match >= 0) {
                    scopy(line, history[search_match], LINE_MAX);
                    len = slen(line);
                    cursor = len;
                }
                search_shown = search_redraw(line, search_query, search_shown);
                continue;
            }
            if (n > 0 && (uint8_t)c >= 32 && search_len < 63) {
                search_query[search_len++] = c;
                search_query[search_len] = '\0';
                search_match = history_search_previous(search_query,
                                                       history_count);
                if (search_match >= 0) {
                    scopy(line, history[search_match], LINE_MAX);
                    len = slen(line);
                    cursor = len;
                }
                search_shown = search_redraw(line, search_query, search_shown);
            }
            continue;
        }

        if (n <= 0) {
            if (!len) {
                swrite(1, "\n");
                return 0;
            }
        } else if (c == '\n' || c == '\r') {
            line[len] = '\0';
            if (input_incomplete(line)) {
                swrite(1, "\n");
                if (len && line[len - 1] == '\\') {
                    --len;
                    line[len] = '\0';
                } else if (len < LINE_MAX - 1) {
                    line[len++] = ' ';
                    line[len] = '\0';
                }
                cursor = len;
                continuation_prompt = 1;
                swrite(1, "> ");
                continue;
            }
            swrite(1, "\n");
        } else {
            int old_len = len;
            if (c == '\b') {
                if (cursor) {
                    --cursor;
                    line_delete_at(line, &len, cursor);
                    redraw_edit_line(line, len, cursor, old_len);
                }
                history_index = history_count;
                history_draft_valid = 0;
                continue;
            } else if (c == '\t') {
                complete_line(line, &len, &cursor);
                history_index = history_count;
                history_draft_valid = 0;
                continue;
            } else if (c == '\x11') {
                history_navigate(line, &len, &cursor, 1, old_len);
                continue;
            } else if (c == '\x12') {
                history_navigate(line, &len, &cursor, 0, old_len);
                continue;
            } else if (c == '\x13') {
                if (cursor) --cursor;
                redraw_edit_line(line, len, cursor, old_len);
                continue;
            } else if (c == '\x14') {
                if (cursor < len) ++cursor;
                redraw_edit_line(line, len, cursor, old_len);
                continue;
            } else if (c == '\x15') {
                cursor = 0;
                redraw_edit_line(line, len, cursor, old_len);
                continue;
            } else if (c == '\x16') {
                cursor = len;
                redraw_edit_line(line, len, cursor, old_len);
                continue;
            } else if (c == '\x17') {
                line_delete_at(line, &len, cursor);
                redraw_edit_line(line, len, cursor, old_len);
                history_index = history_count;
                history_draft_valid = 0;
                continue;
            } else if (c == '\x18') {
                if (!history_count) {
                    swrite(1, "\a");
                    continue;
                }
                if (!history_draft_valid) {
                    scopy(history_draft, line, LINE_MAX);
                    history_draft_valid = 1;
                }
                search_mode = 1;
                search_match = history_count;
                search_len = 0;
                search_query[0] = '\0';
                search_shown = search_redraw(line, search_query, old_len);
                continue;
            } else if ((uint8_t)c >= 32) {
                line_insert(line, &len, &cursor, c);
                redraw_edit_line(line, len, cursor, old_len);
                history_index = history_count;
                history_draft_valid = 0;
                continue;
            } else {
                continue;
            }
        }

        line[len] = '\0';
        len = 0;
        cursor = 0;
        history_index = history_count;
        history_draft_valid = 0;
        continuation_prompt = 0;
        reap_background();
        if (*line) history_record(line);
        if (execute_line(line) < 0) return shell_exit_status;
        shell_prompt();
    }
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    shell_env_count = 0;
    for (int i = 0; envp && envp[i] && shell_env_count < ENV_MAX; ++i)
        env_set_word(envp[i], 1);
    if (!shell_env_count) {
        env_set_word(default_home, 1);
        env_set_word(default_path, 1);
        env_set_word(default_newos, 1);
    }
    const char* name = argc > 0 && argv[0] ? basename(argv[0]) : "sh";
    command_t cmd;
    cmd.argc = argc;
    for (int i = 0; i < argc && i < ARGS_MAX; ++i) cmd.argv[i] = argv[i];
    if (argc < ARGS_MAX) cmd.argv[argc] = 0;
    cmd.input = 0;
    cmd.output = 0;
    cmd.append = 0;

    if (scmp(name, "sh") == 0) {
        if (argc > 2 && scmp(argv[1], "-c") == 0) {
            char command[LINE_MAX];
            scopy(command, argv[2], sizeof(command));
            int status = execute_line(command);
            exit(status < 0 ? shell_exit_status : last_status);
        }
        if (argc > 1) exit(execute_script_file(argv[1]));
        history_load();
        exit(shell_loop());
    }
    int status = execute_builtin(&cmd);
    if (status >= 0) exit(status);
    exec_external(&cmd);
    exit(127);
}
