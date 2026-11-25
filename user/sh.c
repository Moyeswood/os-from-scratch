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
#define ENV_MAX 32   /* 32: 交互 shell 一行行 set 变量 + for/read 循环变量
                        * 会累计占满 16 张表, for/while-read 就静默失败 */
#define ENV_TEXT_MAX 128
#define ENV_NAME_MAX 48
#define SUBST_MAX 1024
#define SUBST_LITERAL '\x1e'
#define COPY_DEPTH_MAX 12
#define TAIL_RING_MAX 16

/* ---- M34 完善版: 全行控制流引擎 ---- */
#define CF_SCAN_MAX   256         /* 一行控制流最多 token */
#define CF_ARM        8           /* if/elif 分支上限 */
#define CF_FOR_WORDS  16          /* for 词表上限 */
#define CF_CASE_PAT   4           /* case 单支 pattern 上限 (| 分隔) */
#define CF_CASE_ARM   8           /* case 分支上限 */
#define CF_ITER_LIMIT 1000000     /* 循环迭代上限, 防死循环卡死 shell */
/* 控制流节点池: 静态 BSS, 一行 (含嵌套 $() 展开) 的 AST 全部建在这里,
 * 不碰用户堆 —— 旧实现每个节点都 calloc, 循环体反复重建时 64KB 用户堆
 * 迅速耗尽, calloc 返回 NULL, 节点写入变成写 0x0 → #PF 杀 shell。
 * 每行 cf_execute_line 重置池游标, 只在本行内递增; 嵌套 $() 在 fork
 * 子进程内重建, COW 隔离池页, 不会踩掉父进程仍在执行的 AST。
 * user/linker.ld 的 .image 哨兵会把 BSS 也填进 flat binary;
 * 池规模因此还受 boot 载入窗口 (448KB) 和可用物理页约束。 */
#define CF_NODE_POOL    128

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

/* 控制流 node 类型 */
enum cf_type {
    CF_CMD,     /* 流水线: span 指向源文本, 执行时重新 tokenize + expand */
    CF_LIST,    /* a; b 顺序列表 (右结合): a=前段, b=后段 */
    CF_AND,     /* a && b */
    CF_OR,      /* a || b */
    CF_NOT,     /* ! 操作数 (可包住分组/块) */
    CF_IF,      /* if / elif* / else / fi */
    CF_WHILE,   /* while cond; do body; done */
    CF_FOR,     /* for VAR in words; do body; done */
    CF_CASE     /* case WORD in PATTERN) cmds ;; ... esac */
};

/* 控制流词法器 token 类型: 保留词 (then/do/...) 仍是 T_WORD, 由解析器
 * 按上下文 (停止掩码) 决定块边界 —— 普通位置它们是普通参数 (echo in),
 * 块终止位置才当保留字。`!` 是普通词字符 (test ! -f 不被拆坏),
 * 流水线首词恰好是 ! 时由解析器识别为取反。 */
enum cf_tok {
    T_WORD = 0, T_AND, T_OR, T_PIPE, T_SEMI,
    T_LP, T_RP, T_AMP, T_EOF
};

typedef struct {
    int off;      /* 词在源文本中的偏移 */
    int len;      /* 词长度 */
    int type;     /* cf_tok */
} cf_token_t;

/* 解析上下文停止词掩码: 命中且裸写的词在当前块边界单独成 token,
 * 不并进前一个流水线的参数。顶层 (掩码 0) 它们都是普通词。 */
#define STOP_THEN   0x01
#define STOP_DO     0x02
#define STOP_DONE   0x04
#define STOP_FI     0x08
#define STOP_ELIF   0x10
#define STOP_ELSE   0x20
#define STOP_CASEEND 0x40  /* case 支主体里裸 "esac" 终止流水线 (仅 case 体传入该位) */
#define STOP_IFBODY   (STOP_FI | STOP_ELIF | STOP_ELSE | STOP_DONE)
#define STOP_FORBODY  (STOP_DONE)

typedef struct cf_node cf_node_t;
struct cf_node {
    enum cf_type type;
    cf_node_t *a;         /* 左 / 条件 / 前段 / for 的 body 之外 */
    cf_node_t *b;         /* 右 / 主体 / 后段 */
    int off;             /* CF_CMD 源文本 span */
    int len;
    int background;      /* CF_CMD 尾随 & (整段后台) */
    int negated;         /* CF_CMD: 由 ! 前缀取反 */
    int kw;              /* CF_CMD 关键字角色: 0=普通, 1=break, 2=continue */
    /* CF_FOR */
    char var[32];
    int nwords;
    int w_off[CF_FOR_WORDS];
    int w_len[CF_FOR_WORDS];
    /* CF_IF */
    int narms;
    cf_node_t *conds[CF_ARM];
    cf_node_t *bodies[CF_ARM];
    cf_node_t *else_body;
    /* CF_WHILE / CF_FOR: "done < file" 文件名源 span, 执行时展开 */
    int input_off;
    int input_len;        /* 0 = 无循环级输入重定向 */
    /* CF_CASE: 分支数与每支 pattern (源 span, | 分隔). 复用 narms 记分支数,
     * 复用 bodies[i] 存各支主体; 下方只存 pattern 列表。subject 用 off/len。 */
    int c_patn[CF_CASE_ARM];
    int c_pato[CF_CASE_ARM][CF_CASE_PAT];
    int c_patl[CF_CASE_ARM][CF_CASE_PAT];
};

/* break/continue 通过执行返回值向上传播 (负值哨兵), 只被循环消费;
 * 顶层裸 break/continue (false; break) 由 cf_execute_line 归一为 0。 */
#define CF_SIG_BREAK    (-200)
#define CF_SIG_CONTINUE (-201)
static int cf_in_loop;      /* 当前所在循环深度, >0 时 break/continue 才合法 */

/* 控制流解析器上下文 */
typedef struct {
    const char* text;
    cf_token_t* toks;
    int n;
    int i;
    int err;
} cf_parser;

/* 控制流节点池 (BSS): 每行执行前清零重建 (cf_execute_line)。
 * 嵌套 $() 在 fork 出的子进程里各自重建, COW 各写各的池页副本,
 * 不会踩父进程还没执行完的 AST。 */
static cf_node_t cf_node_pool[CF_NODE_POOL];
static int cf_pool_used;


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
/* Positional parameters $1..$9, ${10} via indirection */
#define PARAM_MAX 10
static char param_storage[PARAM_MAX][LINE_MAX];
static int param_count;

/* Control-flow state */
static int cf_block_incomplete(const char* line);
static int cf_execute_line(const char* text);
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

/* ------------------------------------------------------------------ */
/* M34 完善版: 全行控制流引擎 (if/elif/else, while, for, break,       */
/* continue, &&, ||, !, ;, 分组 ())                                    */
/*                                                                     */
/* 旧实现的问题 (本次重写的原因):                                      */
/*  1. tokenize 把 && 拆成两个 & token, 解析器按 "&&" 整词匹配永远     */
/*     失败 → &&/|| 实际是死代码, 命令直接掉进 parse_commands 报       */
/*     syntax error;                                                   */
/*  2. execute_line_raw 先按 ; 切段, if/while/for 永远无法作为整体     */
/*     到达解析器;                                                     */
/*  3. for 循环里 '\\0' 多字符字面量 (写入 \ 和 0 两个字符), 变量     */
/*     名/词值永不终结; while 循环体一遇非零状态就整个退出。          */
/* 新设计:                                                              */
/*  - cf_scan: 对整行 ($() 展开后的文本) 词法分析成控制流 token, 保留  */
/*    每个词在源文本中的偏移;                                          */
/*  - 递归下降: list → or(→) → and(→) → 命令位置关键字 if/while/for/  */
/*    break/continue 或 流水线 span;                                   */
/*  - 流水线 = 源文本 span: 执行时对该 span 重新 tokenize + expand +   */
/*    parse_commands + execute_pipeline, 完整保留 引号/重定向/管道/    */
/*    变量展开 语义;                                                   */
/*  - break/continue 以负值哨兵向上传播, 只被最内层循环消费;          */
/*  - 循环有 CF_ITER_LIMIT 迭代上限, 防止 while true 卡死 shell。     */
/* ------------------------------------------------------------------ */

static const char* cf_current_line;   /* cf_execute_line 执行期的源文本 */

static void cf_err(cf_parser* p);

static cf_node_t* cf_alloc(cf_parser* p) {
    cf_node_t* n;
    if (cf_pool_used >= CF_NODE_POOL) {
        if (p) cf_err(p);
        return 0;
    }
    n = &cf_node_pool[cf_pool_used++];
    n->type = 0;
    n->a = 0;
    n->b = 0;
    n->off = 0;
    n->len = 0;
    n->background = 0;
    n->negated = 0;
    n->kw = 0;
    n->nwords = 0;
    n->narms = 0;
    n->else_body = 0;
    n->input_off = 0;
    n->input_len = 0;
    n->var[0] = '\0';
    /* c_* 数组按字面量 0 初值使用, 逐支填充, 无需整体清零 */
    return n;
}

/* 解析器统一走静态池 (BSS, 每行重置): 循环体内 while/for 的 cond/body
 * 零堆分配。池耗尽时报语法错误, 绝不让分配失败造成 NULL 解引用。 */
static cf_node_t* cf_node_new(void) {
    return cf_alloc(0);
}

/* 池节点不回收 (cf_free 空操作); 池在每行 cf_execute_line 开头重置,
 * 嵌套 $() 在 fork 出的子进程里各自重建自己的池页副本 (COW)。 */
static void cf_free(cf_node_t* node) {
    (void)node;
}

static cf_token_t* cf_peek(cf_parser* p) {
    return p->i < p->n ? &p->toks[p->i] : 0;
}

static void cf_err(cf_parser* p) {
    p->err = 1;
}

static int cf_is_bare_word(const char* text, int off, int len) {
    if (len <= 0) return 0;
    for (int i = 0; i < len; ++i) {
        char c = text[off + i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_';
        if (!ok) return 0;
    }
    return 1;
}

/* 限界词比较: text+off 在行中间处并非 NUL 终结, 而 scmp 是整串比较 ——
 * 从词中点起调 scmp 会越过词尾读进后文, 永远非 0 (只有行尾最后词才
 * 偶然命中)。关键词判定必须只比 [off,len) 内的字节。 */
static int cf_word_eq(const char* text, int off, int len, const char* kw) {
    if (slen(kw) != len) return 0;
    for (int i = 0; i < len; ++i)
        if (text[off + i] != kw[i]) return 0;
    return 1;
}

static int cf_kw(const char* text, int off, int len, const char* kw) {
    if (!cf_is_bare_word(text, off, len)) return 0;
    return cf_word_eq(text, off, len, kw);
}

/* 把词法 token 的 [off,len) 精确拷进 dst (NUL 终结)。
 * 不能直接 scopy 整行: 词边界处并非 NUL 终结, 无界拷贝会把
 * 行内后文一起灌进去 (for 变量名 / while 输入文件名被拖长 →
 * 变成非法名/非法路径, 静默失败)。 */
static void cf_copy_word(char* dst, int cap, const char* text, int off, int len) {
    if (len > cap - 1) len = cap - 1;
    for (int i = 0; i < len; ++i) dst[i] = text[off + i];
    dst[len] = '\0';
}

/* 块终止词掩码查询: 裸词命中则返回对应 STOP 位 (解析器按上下文掩码判停)。
 * "in" 不进掩码 —— 它只在 for 词表这个固定位置当关键字, 由 cf_parse_for 处理。 */
static int cf_stop_mask(const char* text, int off, int len) {
    struct { const char* s; int m; } stops[] = {
        { "then", STOP_THEN }, { "do", STOP_DO }, { "done", STOP_DONE },
        { "fi", STOP_FI }, { "elif", STOP_ELIF }, { "else", STOP_ELSE },
        { "esac", STOP_CASEEND },
        { 0, 0 }
    };
    if (!cf_is_bare_word(text, off, len)) return 0;
    for (int i = 0; stops[i].s; ++i)
        if (cf_word_eq(text, off, len, stops[i].s))
            return stops[i].m;
    return 0;
}

/* 位置 i 处是否为控制流运算符; 是则 type + 前进长度, 否则 0 (词字符)。
 * 边界规则: & ( ) 仅在前一字符是 空白/;/行首 时是运算符 (2>&1 / a(b) 保持词);
 * ; | && || 永远是运算符; & 后跟 > 保持词 (2>&1), 且 & 后跟 & 是 &&。
 * ! 是普通词字符 (test ! -f 不被拆坏): 流水线首词恰好是 ! 时由解析器识别为取反。 */
static int cf_op_at(const char* text, int len, int i, int* adv) {
    char c = text[i];
    int prev = i > 0 ? text[i - 1] : '\0';
    int boundary = (i == 0 || is_space(prev) || prev == ';');
    int next = i + 1 < len ? text[i + 1] : '\0';
    if (c == ';') { *adv = 1; return T_SEMI; }
    if (c == '|') {
        *adv = next == '|' ? 2 : 1;
        return next == '|' ? T_OR : T_PIPE;
    }
    if (c == '&') {
        if (next == '&') { *adv = 2; return T_AND; }
        if (next == '>') { *adv = 1; return 0; }   /* 2>&1 */
        *adv = 1;
        return boundary ? T_AMP : 0;
    }
    if (c == '(') {
        *adv = 1;
        return boundary ? T_LP : 0;
    }
    if (c == ')') {
        *adv = 1;
        return T_RP;   /* ) 始终是分组结束符, 即使紧贴词 (POSIX: ) 是元字符) */
    }
    *adv = 1;
    return 0;
}

static int cf_scan(const char* text, cf_token_t* toks, int max) {
    int len = slen(text);
    int i = 0, n = 0;
    while (i < len) {
        char c = text[i];
        if (is_space(c)) { ++i; continue; }
        int adv;
        int op = (c == SUBST_LITERAL) ? 0 : cf_op_at(text, len, i, &adv);
        if (op) {
            if (n < max) {
                toks[n].off = i;
                toks[n].len = adv;
                toks[n].type = op;
                ++n;
            }
            i += adv;
            continue;
        }
        /* 一个词: 源文本词 (含引号/重定向符/$展开残留), 直到空白或运算符;
         * 整个词记为 T_WORD, 保留 [off,len) span, 执行时重新 tokenize + expand */
        int w = i;
        int qch = 0;
        while (i < len) {
            char d = text[i];
            if (qch) {
                if (d == qch) qch = 0;
                ++i;
                continue;
            }
            if (is_space(d)) break;
            if (d == SUBST_LITERAL) { i += 2; continue; }
            int a;
            if (cf_op_at(text, len, i, &a)) break;
            if (d == '\'' || d == '"') { qch = d; ++i; continue; }
            ++i;
        }
        int wl = i - w;
        if (wl > 0) {
            if (n < max) {
                toks[n].off = w;
                toks[n].len = wl;
                toks[n].type = T_WORD;
                ++n;
            }
        } else {
            ++i;   /* 保险: 不应发生 */
        }
    }
    return n;
}

/* 交互续行判断: if/while/for 块是否未闭合 (对行文本做启发式关键字扫描)。
 * 只数控制流关键字, 不碰括号, 避免与 $( ) 命令替换的括号计数打架。
 * 关键词必须是一个"完整的 shell 词": 前后都得是词边界。否则
 * "P=if-ok" 里的 if 会被误判成 if 关键字, 续行死循环吞掉后面所有命令。 */
/* char 是否为 shell 词边界 (是 → 词在此断开); 其余 (字母/数字/-=/.//等)
 * 都属于某个词的内部, 不算边界。 */
static int cf_word_boundary(char c) {
    if (!c) return 1;
    if (is_space(c)) return 1;
    if (c == ';' || c == '&' || c == '|' || c == '(' ||
        c == ')' || c == '<' || c == '>') return 1;
    if (c == '\'' || c == '"' || c == '\\') return 1;
    return 0;
}

static int cf_block_incomplete(const char* line) {
    int open = 0;
    char quote = 0;
    int escaped = 0;
    for (int i = 0; line[i]; ++i) {
        char c = line[i];
        if (escaped) { escaped = 0; continue; }
        if (c == '\\' && quote != '\'') { escaped = 1; continue; }
        if (c == '\'' || c == '"') {
            if (!quote) quote = c;
            else if (quote == c) quote = 0;
            continue;
        }
        if (quote) continue;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            int j = i;
            while (line[j] && ((line[j] >= 'a' && line[j] <= 'z') ||
                    (line[j] >= 'A' && line[j] <= 'Z') ||
                    (line[j] >= '0' && line[j] <= '9') || line[j] == '_'))
                ++j;
            /* 词内字节匹配 + 两侧必须都是词边界 (否则是 if-ok 这类普通词里的子串) */
            if (cf_is_bare_word(line, i, j - i) &&
                ((i == 0) || cf_word_boundary(line[i - 1])) &&
                cf_word_boundary(line[j])) {
                int wl = j - i;
                if (cf_word_eq(line, i, wl, "if")) ++open;
                else if (cf_word_eq(line, i, wl, "while")) ++open;
                else if (cf_word_eq(line, i, wl, "for")) ++open;
                else if (cf_word_eq(line, i, wl, "case")) ++open;
                else if (cf_word_eq(line, i, wl, "fi")) --open;
                else if (cf_word_eq(line, i, wl, "done")) --open;
                else if (cf_word_eq(line, i, wl, "esac")) --open;
            }
            i = j - 1;
        }
    }
    return open > 0 ? open : 0;
}

static void cf_mark_bg(cf_node_t* node) {
    if (!node) return;
    switch (node->type) {
    case CF_CMD:
        node->background = 1;
        break;
    case CF_AND:
    case CF_OR:
        cf_mark_bg(node->a);
        cf_mark_bg(node->b);
        break;
    default:
        break;
    }
}

/* 期望词: 下一词必须恰为 kw, 成功则吃掉; 失败置 err 返回 0 (必需位置) */
static int cf_expect_word(cf_parser* p, const char* kw) {
    cf_token_t* t = cf_peek(p);
    if (!t || t->type != T_WORD || !cf_kw(p->text, t->off, t->len, kw)) {
        cf_err(p);
        return 0;
    }
    ++p->i;
    return 1;
}

/* 非侵入探测: 下一词是否为 kw (可选分支 elif/else, 不置 err、不前进) */
static int cf_peek_word(cf_parser* p, const char* kw) {
    cf_token_t* t = cf_peek(p);
    return t && t->type == T_WORD &&
           cf_kw(p->text, t->off, t->len, kw);
}

static cf_node_t* cf_parse_pipeline(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_if(cf_parser* p, int neg, int stopmask);
static cf_node_t* cf_parse_while(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_for(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_case(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_and(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_or(cf_parser* p, int stopmask);
static cf_node_t* cf_parse_list(cf_parser* p, int stopmask);

/* 当前流水线段首词是否恰为裸 "!" (取反前缀; 带引号/带参数的 "! " 不受影响) */
static int cf_word_is_bang(cf_parser* p, cf_token_t* t) {
    const char* w = p->text + t->off;
    int wl = t->len;
    return wl == 1 && w[0] == '!';
}

static cf_node_t* cf_wrap_not(cf_parser* p, cf_node_t* n) {
    if (!n) return 0;
    cf_node_t* nn = cf_node_new();
    if (!nn) { cf_err(p); return 0; }
    nn->type = CF_NOT;
    nn->a = n;
    return nn;
}

static cf_node_t* cf_parse_command(cf_parser* p, int stopmask) {
    cf_token_t* t = cf_peek(p);
    if (!t) { cf_err(p); return 0; }
    int neg = 0;
    /* 取反前缀: 流水线首词恰好是裸 "!" (test ! -f 里 ! 不是首词, 不受影响) */
    if (t->type == T_WORD && cf_word_is_bang(p, t)) {
        ++p->i;
        neg = 1;
        t = cf_peek(p);
        if (!t) { cf_err(p); return 0; }
    }
    /* 分组 ( ... ): 内部是完整命令列表; 带 ! 前缀时整块取反 */
    if (t->type == T_LP) {
        ++p->i;
        cf_node_t* inner = cf_parse_list(p, stopmask);
        if (!inner) { cf_err(p); return 0; }
        cf_token_t* t2 = cf_peek(p);
        if (!t2 || t2->type != T_RP) {
            cf_err(p);
            return 0;
        }
        ++p->i;
        return neg ? cf_wrap_not(p, inner) : inner;
    }
    if (cf_is_bare_word(p->text, t->off, t->len)) {
        /* 裸词才当关键字 (带引号的 "if" 是普通词, span 含引号字符)。
         * 词比较走 cf_word_eq: scmp 是整串比较, 行中间的词后还有
         * 后文 (非 NUL 终结), 从词中点起 scmp 永远非 0。 */
        const char* w = p->text + t->off;
        int wl = t->len;
        if (cf_word_eq(w, 0, wl, "if")) return cf_parse_if(p, neg, stopmask);
        if (cf_word_eq(w, 0, wl, "while")) {
            cf_node_t* n = cf_parse_while(p, stopmask);
            if (n && neg) n = cf_wrap_not(p, n);
            return n;
        }
        if (cf_word_eq(w, 0, wl, "for")) {
            cf_node_t* n = cf_parse_for(p, stopmask);
            if (n && neg) n = cf_wrap_not(p, n);
            return n;
        }
        if (cf_word_eq(w, 0, wl, "case")) {
            cf_node_t* n = cf_parse_case(p, stopmask);
            if (n && neg) n = cf_wrap_not(p, n);
            return n;
        }
        if (cf_word_eq(w, 0, wl, "break") || cf_word_eq(w, 0, wl, "continue")) {
            cf_token_t* nx = p->i + 1 < p->n ? &p->toks[p->i + 1] : 0;
            if (!nx || nx->type != T_WORD ||
                (nx->type == T_WORD && cf_stop_mask(p->text, nx->off, nx->len) & stopmask)) {
                ++p->i;
                cf_node_t* n = cf_node_new();
                if (!n) { cf_err(p); return 0; }
                n->type = CF_CMD;
                n->off = t->off;
                n->len = wl;
                n->kw = cf_word_eq(w, 0, wl, "break") ? 1 : 2;
                return n;
            }
        }
    }
    cf_node_t* node = cf_parse_pipeline(p, stopmask);
    if (node) node->negated = neg;
    return node;
}

/* 原子命令: 一段词流 (重定向符 </> 2>&1 是普通词, 含在 span 内),
 * 以 | 分流水线段; 遇控制流运算符 (;/&&/||/&/(/)) 或命中停止掩码的
 * 块终止词收尾。记录源文本 span [off,len], 执行时重新 tokenize + expand。 */
static cf_node_t* cf_parse_pipeline(cf_parser* p, int stopmask) {
    cf_token_t* t0 = cf_peek(p);
    if (!t0 || t0->type != T_WORD) {
        cf_err(p);   /* 流水线必须从词开始 (| 开头 / 空段 / 只有块终止词) */
        return 0;
    }
    cf_node_t* node = cf_node_new();
    if (!node) { cf_err(p); return 0; }
    node->type = CF_CMD;
    node->off = t0->off;
    int end = t0->off + t0->len;
    ++p->i;
    for (;;) {
        cf_token_t* t = cf_peek(p);
        if (!t) break;
        if (t->type == T_WORD || t->type == T_PIPE) {
            /* 下一词是块终止词 (then/done/fi/...) 时, 流水线到此为止,
             * 终止词留给块解析器; 普通词/重定向符继续收进 span */
            cf_token_t* t2 = cf_peek(p);
            if (t2 && t2->type == T_WORD &&
                cf_stop_mask(p->text, t2->off, t2->len) & stopmask)
                break;
            end = t->off + t->len;
            ++p->i;
            continue;
        }
        break;   /* ; && || & ( ) : 本流水线到此为止 */
    }
    node->len = end - node->off;
    return node;
}

static cf_node_t* cf_parse_if(cf_parser* p, int neg, int stopmask) {
    ++p->i;   /* 吃掉 if */
    cf_node_t* node = cf_node_new();
    if (!node) { cf_err(p); return 0; }
    node->type = CF_IF;
    int arms = 0;
    /* if 内部: elif/else/fi/done 是块边界, then/do 是条件/分支的收尾词。
     * 漏了 STOP_THEN, 条件列表会把 "then echo ..." 整个当成流水线词流吞掉。 */
    int sm = stopmask | STOP_IFBODY | STOP_THEN | STOP_DO;
    for (;;) {
        cf_node_t* cond = cf_parse_list(p, sm);
        if (!cond) { cf_err(p); return 0; }
        if (!cf_expect_word(p, "then")) { cf_err(p); return 0; }
        cf_node_t* body = cf_parse_list(p, sm);
        if (!body) { cf_err(p); return 0; }
        if (arms >= CF_ARM) {
            swrite(2, "sh: too many elif branches\n");
            cf_err(p);
            return 0;
        }
        node->conds[arms] = cond;
        node->bodies[arms] = body;
        ++arms;
        if (cf_peek_word(p, "elif")) {
            ++p->i;
            continue;
        }
        if (cf_peek_word(p, "else")) {
            ++p->i;
            cf_node_t* eb = cf_parse_list(p, sm);
            if (!eb) { cf_err(p); return 0; }
            node->else_body = eb;
        }
        if (!cf_expect_word(p, "fi")) { cf_err(p); return 0; }
        break;
    }
    node->narms = arms;
    cf_node_t* result = node;
    if (neg) result = cf_wrap_not(p, node);
    return result;
}

/* while/for 共用的循环尾输入重定向。cf_scan 把 < 当词字符,
 * 因而同时接受 "< file" 和 "<file"。保留文件名原文, 不在解析阶段
 * 展开变量/引号 (前面的赋值可能还没执行)。 */
static int cf_parse_loop_input(cf_parser* p, cf_node_t* node) {
    cf_token_t* t = cf_peek(p);
    if (!t || t->type != T_WORD || p->text[t->off] != '<') return 1;
    ++p->i;
    int off = t->off + 1;
    int len = t->len - 1;
    if (!len) {
        t = cf_peek(p);
        if (!t || t->type != T_WORD) { cf_err(p); return 0; }
        off = t->off;
        len = t->len;
        ++p->i;
    }
    if (!len || p->text[off] == '<' || p->text[off] == '>') {
        cf_err(p);
        return 0;
    }
    node->input_off = off;
    node->input_len = len;
    return 1;
}

static cf_node_t* cf_parse_while(cf_parser* p, int stopmask) {
    ++p->i;   /* 吃掉 while */
    /* 条件读到 ; 为止 (嵌套 while 的条件不会带分号) */
    int sm = stopmask | STOP_DO;
    cf_node_t* cond = cf_parse_list(p, sm);
    if (!cond) { cf_err(p); return 0; }
    if (!cf_expect_word(p, "do")) { cf_err(p); return 0; }
    cf_node_t* body = cf_parse_list(p, stopmask | STOP_DONE);
    if (!body) { cf_err(p); return 0; }
    cf_token_t* done = cf_peek(p);
    if (!done || done->type != T_WORD ||
        !cf_kw(p->text, done->off, done->len, "done")) {
        cf_err(p);
        return 0;
    }
    ++p->i;
    cf_node_t* node = cf_node_new();
    if (!node) { cf_err(p); return 0; }
    node->type = CF_WHILE;
    node->a = cond;
    node->b = body;
    if (!cf_parse_loop_input(p, node)) return 0;
    return node;
}

static cf_node_t* cf_parse_for(cf_parser* p, int stopmask) {
    ++p->i;   /* 吃掉 for */
    cf_token_t* vt = cf_peek(p);
    if (!vt || vt->type != T_WORD ||
        !cf_is_bare_word(p->text, vt->off, vt->len)) {
        cf_err(p);
        return 0;
    }
    ++p->i;
    if (!cf_expect_word(p, "in")) { cf_err(p); return 0; }
    cf_node_t* node = cf_node_new();
    if (!node) { cf_err(p); return 0; }
    node->type = CF_FOR;
    /* 词边界: var 只取循环词本身 (vt->len), 不是从行中间起的一整串 */
    int vcap = vt->len + 1 < (int)sizeof(node->var) ? vt->len + 1 :
               (int)sizeof(node->var);
    scopy(node->var, p->text + vt->off, vcap);
    node->nwords = 0;
    /* 词表读到 "do" 为止 (do 是终止词, 不进词表) */
    for (;;) {
        cf_token_t* t = cf_peek(p);
        if (!t || t->type != T_WORD) break;
        if (cf_kw(p->text, t->off, t->len, "do")) break;
        if (node->nwords >= CF_FOR_WORDS) {
            swrite(2, "sh: too many words in for\n");
            cf_err(p);
            return 0;
        }
        node->w_off[node->nwords] = t->off;
        node->w_len[node->nwords] = t->len;
        ++node->nwords;
        ++p->i;
    }
    /* POSIX: "for x in w1 w2; do" —— 词表后的分号可有可无,
     * 词表读到 ; 停顿时先吃掉分号再要 do */
    if (cf_peek(p) && cf_peek(p)->type == T_SEMI) ++p->i;
    if (!cf_expect_word(p, "do")) { cf_err(p); return 0; }
    cf_node_t* body = cf_parse_list(p, stopmask | STOP_DONE);
    if (!body) { cf_err(p); return 0; }
    cf_token_t* done = cf_peek(p);
    if (!done || done->type != T_WORD ||
        !cf_kw(p->text, done->off, done->len, "done")) {
        cf_err(p);
        return 0;
    }
    ++p->i;
    node->b = body;
    if (!cf_parse_loop_input(p, node)) return 0;
    return node;
}

/* 展开一个源 span 成单值, 写入 out (NUL 终结, 限 LINE_MAX)。
 * 只做变量/引号/转义展开 (tokenize), **不做**文件名 glob 或字段拆分 ——
 * pattern 里的 "*" / "?" 必须原样保留给 glob_match, subject 保持完整值。
 * 结果必须恰为一个词, 否则返回 -1。 */
static int cf_expand_value(int off, int len, char* out) {
    char buf[LINE_MAX];
    cf_copy_word(buf, sizeof(buf), cf_current_line, off, len);
    char* toks[WORD_MAX];
    unsigned char gl[WORD_MAX];
    int nt = tokenize(buf, toks, WORD_MAX, gl);
    if (nt != 1) return -1;
    scopy(out, toks[0], LINE_MAX);
    return 0;
}

/* 单个 case 支的主体: 命令列表, 到 ";;" (双分号) / 裸 "esac" / 行尾为止。
 * 单个 ";" 只是命令分隔 (继续本支), ";;" 或裸 "esac" 才结束本支。
 * 空支 (pat) ;;) 返回 off<0 的空调用列表哨兵。 */
static cf_node_t* cf_parse_case_body(cf_parser* p, int stopmask) {
    cf_node_t* node = 0;
    for (;;) {
        cf_token_t* t = cf_peek(p);
        if (!t) break;
        if (t->type == T_WORD && cf_kw(p->text, t->off, t->len, "esac")) break;
        if (t->type == T_SEMI) {
            /* 支体开头的 ";;" (空支): 吃掉双分号后结束本支 */
            ++p->i;
            cf_token_t* s2 = cf_peek(p);
            if (s2 && s2->type == T_SEMI) ++p->i;
            break;
        }
        cf_node_t* stmt = cf_parse_or(p, stopmask | STOP_CASEEND);
        if (!stmt) { cf_err(p); return 0; }
        if (node) {
            cf_node_t* nn = cf_node_new();
            if (!nn) { cf_err(p); return 0; }
            nn->type = CF_LIST;
            nn->a = node;
            nn->b = stmt;
            node = nn;
        } else {
            node = stmt;
        }
        cf_token_t* s = cf_peek(p);
        if (!s || s->type != T_SEMI) break;      /* 命令后无 ";" → 本支结束 */
        ++p->i;                                   /* 吃掉一个 ";" */
        cf_token_t* s2 = cf_peek(p);
        if (s2 && s2->type == T_SEMI) { ++p->i; break; }   /* ";;" → 双分号结束本支 */
        /* 单 ";": 继续解析下一条命令 */
    }
    if (!node) {
        node = cf_node_new();
        if (!node) { cf_err(p); return 0; }
        node->type = CF_CMD;
        node->off = -1;          /* 空调用列表: cf_execute_node 识别 off<0 → 状态 0 */
        node->len = 0;
    }
    return node;
}

/* case WORD in PATTERN|PATTERN) body ;; PATTERN) body ... esac
 * 主体词与 pattern 都保留 [off,len) 源 span, 执行时展开。pattern 支持
 * "|" 多模式 (扫描器归为 T_PIPE) 与 glob "*" / "?" (复用 glob_match, 不做
 * 文件名展开, 符合 [[ == ]] 直觉)。 */
static cf_node_t* cf_parse_case(cf_parser* p, int stopmask) {
    ++p->i;   /* 吃掉 case */
    cf_token_t* st = cf_peek(p);
    if (!st || st->type != T_WORD) { cf_err(p); return 0; }
    ++p->i;
    if (!cf_expect_word(p, "in")) { cf_err(p); return 0; }
    cf_node_t* node = cf_node_new();
    if (!node) { cf_err(p); return 0; }
    node->type = CF_CASE;
    node->off = st->off;        /* 主体词 span */
    node->len = st->len;
    int arms = 0;
    for (;;) {
        cf_token_t* t = cf_peek(p);
        if (!t || t->type != T_WORD) { cf_err(p); return 0; }
        /* 裸 "esac" → 没有更多支, 收尾 */
        if (cf_kw(p->text, t->off, t->len, "esac")) break;
        int npat = 0;
        for (;;) {
            cf_token_t* pt = cf_peek(p);
            if (!pt || pt->type != T_WORD) { cf_err(p); return 0; }
            if (npat >= CF_CASE_PAT) {
                swrite(2, "sh: too many case patterns\n");
                cf_err(p);
                return 0;
            }
            node->c_pato[arms][npat] = pt->off;
            node->c_patl[arms][npat] = pt->len;
            ++npat;
            ++p->i;
            cf_token_t* nx = cf_peek(p);
            if (nx && nx->type == T_PIPE) { ++p->i; continue; }  /* "|" 下一个 pattern */
            if (nx && nx->type == T_RP) { ++p->i; break; }       /* ")" 结束 pattern */
            cf_err(p);
            return 0;
        }
        node->c_patn[arms] = npat;
        cf_node_t* body = cf_parse_case_body(p, stopmask);
        if (!body) { cf_err(p); return 0; }
        node->bodies[arms] = body;
        ++arms;
        if (arms >= CF_CASE_ARM) {
            swrite(2, "sh: too many case arms\n");
            cf_err(p);
            return 0;
        }
    }
    if (!cf_expect_word(p, "esac")) { cf_err(p); return 0; }
    node->narms = arms;
    return node;
}

static cf_node_t* cf_parse_and(cf_parser* p, int stopmask) {
    cf_node_t* node = cf_parse_command(p, stopmask);
    if (!node) return 0;
    while (p->i < p->n && p->toks[p->i].type == T_AND) {
        ++p->i;
        cf_node_t* right = cf_parse_command(p, stopmask);
        if (!right) { cf_err(p); return 0; }
        cf_node_t* nn = cf_node_new();
        if (!nn) { cf_err(p); return 0; }
        nn->type = CF_AND;
        nn->a = node;
        nn->b = right;
        node = nn;
    }
    return node;
}

static cf_node_t* cf_parse_or(cf_parser* p, int stopmask) {
    cf_node_t* node = cf_parse_and(p, stopmask);
    if (!node) return 0;
    while (p->i < p->n && p->toks[p->i].type == T_OR) {
        ++p->i;
        cf_node_t* right = cf_parse_and(p, stopmask);
        if (!right) { cf_err(p); return 0; }
        cf_node_t* nn = cf_node_new();
        if (!nn) { cf_err(p); return 0; }
        nn->type = CF_OR;
        nn->a = node;
        nn->b = right;
        node = nn;
    }
    return node;
}

/* 命令列表: a; b; c, 以 & 把前一段整体后台化 (a & b → 两段各带 & 语义)。
 * 停止掩码 stopmask 决定块终止词何时收尾本列表。 */
static cf_node_t* cf_parse_list(cf_parser* p, int stopmask) {
    cf_node_t* node = cf_parse_or(p, stopmask);
    if (!node) return 0;
    for (;;) {
        cf_token_t* t = cf_peek(p);
        if (!t) break;
        if (t->type == T_SEMI) {
            ++p->i;
            cf_token_t* t2 = cf_peek(p);
            if (!t2) break;                 /* 行尾 ; */
            /* 下一段以块终止词 (then/done/fi/...) 开头 → 本列表到此为止,
             * 终止词留给上层解析器 (if 的 then / while 的 done ...) */
            if (t2->type == T_WORD &&
                cf_stop_mask(p->text, t2->off, t2->len) & stopmask)
                break;
            cf_node_t* next = cf_parse_or(p, stopmask);
            if (!next) { cf_err(p); return 0; }
            cf_node_t* nn = cf_node_new();
            if (!nn) { cf_err(p); return 0; }
            nn->type = CF_LIST;
            nn->a = node;
            nn->b = next;
            node = nn;
            continue;
        }
        if (t->type == T_AMP) {
            /* "a & b": 把已完成段 a 标记后台; & 后的 b 是新的列表项,
             * 只有 a 的 & 记 bg (整体后台语义: a & 成功则 b 也后台) */
            ++p->i;
            cf_mark_bg(node);
            cf_token_t* t2 = cf_peek(p);
            if (!t2) break;                 /* 行尾 & */
            if (t2->type == T_WORD &&
                cf_stop_mask(p->text, t2->off, t2->len) & stopmask)
                break;
            cf_node_t* next = cf_parse_or(p, stopmask);
            if (!next) { cf_err(p); return 0; }
            cf_node_t* nn = cf_node_new();
            if (!nn) { cf_err(p); return 0; }
            nn->type = CF_LIST;
            nn->a = node;
            nn->b = next;
            node = nn;
            continue;
        }
        break;   /* ; && || & ( ) ! 之外: 块终止词/行尾, 本列表到此为止 */
    }
    return node;
}

/* 对一个源文本 span 重新 tokenize + 展开 + 解析 + 执行。
 * 返回值: >=0 命令状态; -1 shell 退出 (exit 内置)。out_status 同。 */
static int cf_run_pipeline(const char* text, int off, int len,
                          int background, int* out_status) {
    char buf[LINE_MAX];
    if (len > LINE_MAX - 1) len = LINE_MAX - 1;
    for (int i = 0; i < len; ++i) buf[i] = text[off + i];
    buf[len] = '\0';

    char* tokens[WORD_MAX];
    unsigned char globs[WORD_MAX];
    int nwords = tokenize(buf, tokens, WORD_MAX, globs);
    if (nwords < 0) {
        swrite(2, "sh: syntax error\n");
        *out_status = 2;
        return 2;
    }
    char* words[WORD_MAX];
    if (expand_words(tokens, nwords, globs, words, &nwords) < 0) {
        swrite(2, "sh: expansion failed\n");
        *out_status = 2;
        return 2;
    }

    int assignments = 0;
    while (assignments < nwords && is_assignment_word(words[assignments]))
        ++assignments;
    if (assignments == nwords) {
        for (int i = 0; i < assignments; ++i)
            if (env_set_word(words[i], 0) < 0) {
                swrite(2, "sh: invalid assignment\n");
                *out_status = 1;
                return 1;
            }
        last_status = 0;
        *out_status = 0;
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
                *out_status = 1;
                return 1;
            }
    }

    command_t cmds[PIPE_MAX];
    int count = 0;
    int bg_flag = 0;
    int parsed = parse_commands(words + assignments, nwords - assignments,
                                cmds, &count, &bg_flag);
    if (parsed < 0) {
        if (assignments) env_restore(saved_env, saved_exports, saved_count);
        swrite(2, "sh: syntax error\n");
        *out_status = 2;
        return 2;
    }
    if (!count) {
        if (assignments) env_restore(saved_env, saved_exports, saved_count);
        *out_status = 0;
        return 0;
    }
    char command_text[JOB_TEXT];
    scopy(command_text, buf, sizeof(command_text));
    /* 后台流水线的 & 是运算符 token, 不在 span 内; 作业文本要把它补回来
     * (旧 execute_segment 直接拷原文, [JOB] started/stopped/jobs 都打印
     * "spinner &")。bg_flag 是 parse_commands 从词里解析出的尾随 &。 */
    int eff_bg = background || bg_flag;
    if (eff_bg && len < (int)sizeof(command_text) - 3) {
        command_text[len] = ' ';
        command_text[len + 1] = '&';
        command_text[len + 2] = '\0';
    }
    int status = execute_pipeline(cmds, count, eff_bg,
                                  command_text);
    if (assignments) env_restore(saved_env, saved_exports, saved_count);
    if (status < 0) {
        *out_status = -1;
        return -1;
    }
    last_status = status;
    *out_status = status;
    return status;
}

static int cf_execute_node(cf_node_t* node);

/* 输入重定向包住整个循环, 而非每次迭代重新 open。内层循环的 saved_in
 * 与外层文件共享偏移, 所以恢复时既不会回到文件开头, 也不会误回控制台。
 * 独立函数避免每个普通 AST 节点都为展开数组预留栈空间。 */
static int cf_execute_with_input(cf_node_t* node) {
    char buf[LINE_MAX];
    char* tokens[WORD_MAX];
    char* words[WORD_MAX];
    unsigned char globs[WORD_MAX];
    cf_copy_word(buf, sizeof(buf), cf_current_line,
                 node->input_off, node->input_len);
    int count = tokenize(buf, tokens, WORD_MAX, globs);
    if (count != 1 ||
        expand_words(tokens, count, globs, words, &count) < 0 ||
        count != 1 || !words[0][0]) {
        swrite(2, "sh: ambiguous loop input\n");
        last_status = 1;
        return 1;
    }
    /* 必须先保存旧 fd 0, 再覆盖; 否则循环结束后 shell 会在文件 EOF 退出。 */
    int saved_in = dup(0);
    if (saved_in < 0) {
        swrite(2, "sh: cannot save loop input\n");
        last_status = 1;
        return 1;
    }
    int fd = open(words[0], O_RDONLY);
    if (fd < 0) {
        swrite(2, "sh: cannot open loop input: ");
        swrite(2, words[0]);
        swrite(2, "\n");
        close(saved_in);
        last_status = 1;
        return 1;
    }
    int redirected = dup2(fd, 0);
    close(fd);   /* fd 0 已持有文件引用, 循环期间不额外占一个描述符 */
    if (redirected < 0) {
        close(saved_in);
        swrite(2, "sh: cannot redirect loop input\n");
        last_status = 1;
        return 1;
    }
    int status = cf_execute_node(node);
    /* 正常结束 / break / continue / exit 都只走这一处恢复。 */
    if (dup2(saved_in, 0) < 0) {
        swrite(2, "sh: cannot restore loop input\n");
        if (status >= 0) status = 1;
    }
    close(saved_in);
    if (status >= 0) last_status = status;
    return status;
}

static int cf_execute(cf_node_t* node) {
    if (!node) return 0;
    if (node->input_len) return cf_execute_with_input(node);
    return cf_execute_node(node);
}

static int cf_execute_node(cf_node_t* node) {
    switch (node->type) {
    case CF_CMD: {
        if (node->kw == 1 || node->kw == 2) {
            if (!cf_in_loop) {
                swrite(2, "sh: ");
                swrite(2, node->kw == 1 ? "break outside loop\n"
                                        : "continue outside loop\n");
                last_status = 1;
                return 1;
            }
            return node->kw == 1 ? CF_SIG_BREAK : CF_SIG_CONTINUE;
        }
        if (node->off < 0) { last_status = 0; return 0; }   /* 空调用列表 */
        int out_status = 0;
        int status = cf_run_pipeline(cf_current_line, node->off, node->len,
                                     node->background, &out_status);
        if (status >= 0) {
            if (node->negated) status = status ? 0 : 1;
            last_status = status;
        }
        return status;
    }
    case CF_LIST: {
        int s = cf_execute(node->a);
        if (s == CF_SIG_BREAK || s == CF_SIG_CONTINUE) return s;
        if (s < 0) return s;
        return cf_execute(node->b);
    }
    case CF_AND: {
        int l = cf_execute(node->a);
        if (l == CF_SIG_BREAK || l == CF_SIG_CONTINUE) return l;
        if (l < 0) return l;
        if (l != 0) { last_status = l; return l; }
        return cf_execute(node->b);
    }
    case CF_OR: {
        int l = cf_execute(node->a);
        if (l == CF_SIG_BREAK || l == CF_SIG_CONTINUE) return l;
        if (l < 0) return l;
        if (l == 0) { last_status = 0; return 0; }
        return cf_execute(node->b);
    }
    case CF_NOT: {
        /* ! 包分组/块: 子表达式执行后反转退出状态 (短路哨兵/-1 原样上抛) */
        int s = cf_execute(node->a);
        if (s == CF_SIG_BREAK || s == CF_SIG_CONTINUE || s < 0) return s;
        last_status = s ? 0 : 1;
        return last_status;
    }
    case CF_IF: {
        for (int i = 0; i < node->narms; ++i) {
            int c = cf_execute(node->conds[i]);
            if (c == CF_SIG_BREAK || c == CF_SIG_CONTINUE) return c;
            if (c < 0) return c;
            if (c == 0) return cf_execute(node->bodies[i]);
        }
        if (node->else_body) {
            int e = cf_execute(node->else_body);
            return e;
        }
        last_status = 0;
        return 0;
    }
    case CF_WHILE: {
        int abort_status = 0;
        ++cf_in_loop;
        int guard = CF_ITER_LIMIT;
        for (;;) {
            int c = cf_execute(node->a);
            if (c == CF_SIG_BREAK || c == CF_SIG_CONTINUE)
                { abort_status = c; break; }
            if (c < 0) { abort_status = c; break; }
            if (c != 0) break;
            int b = cf_execute(node->b);
            if (--guard <= 0) break;   /* 迭代上限, 防 while true 卡死 */
            if (b == CF_SIG_BREAK) break;
            if (b == CF_SIG_CONTINUE) continue;
            if (b < 0) { abort_status = b; break; }
        }
        --cf_in_loop;
        if (abort_status < 0) { last_status = 0; return abort_status; }
        last_status = 0;
        return 0;
    }
    case CF_CASE: {
        /* 展开主体词为待匹配串 (只做变量/引号, 不做文件名 glob)。 */
        char subject[LINE_MAX];
        if (cf_expand_value(node->off, node->len, subject) < 0) {
            swrite(2, "sh: case: bad subject\n");
            last_status = 1;
            return 1;
        }
        for (int arm = 0; arm < node->narms; ++arm) {
            for (int k = 0; k < node->c_patn[arm]; ++k) {
                char pat[LINE_MAX];
                if (cf_expand_value(node->c_pato[arm][k],
                                    node->c_patl[arm][k], pat) < 0)
                    continue;
                if (glob_match(pat, subject)) {
                    int st = cf_execute(node->bodies[arm]);
                    /* break/continue 或 shell 退出 (子表达式 <0) 原样上抛;
                     * 正常完成 case → 0 (POSIX: case 无 else 分支时未命中状态 0) */
                    if (st == CF_SIG_BREAK || st == CF_SIG_CONTINUE || st < 0)
                        return st;
                    last_status = st ? st : 0;
                    return last_status;
                }
            }
        }
        /* 无支命中: 状态 0 (与 if 无 else 分支一致) */
        last_status = 0;
        return 0;
    }
    case CF_FOR: {
        char saved_env[ENV_MAX][ENV_TEXT_MAX];
        unsigned char saved_exports[ENV_MAX];
        int saved_count = 0;
        env_snapshot(saved_env, saved_exports, &saved_count);
        ++cf_in_loop;
        int guard = CF_ITER_LIMIT;
        int broke = 0;      /* break / 迭代上限: 正常结束, 不当致命错误 */
        int abort_neg = 0;   /* 只有 shell 退出 (子表达式 < 0) 才向上传播 -1 */
        for (int wi = 0; wi < node->nwords && !broke; ++wi) {
            /* 展开该词: 变量/glob/空格分词 */
            char buf[LINE_MAX];
            int wlen = node->w_len[wi];
            if (wlen > LINE_MAX - 1) wlen = LINE_MAX - 1;
            for (int i = 0; i < wlen; ++i)
                buf[i] = cf_current_line[node->w_off[wi] + i];
            buf[wlen] = '\0';
            char* toks[WORD_MAX];
            unsigned char gl[WORD_MAX];
            int nt = tokenize(buf, toks, WORD_MAX, gl);
            char* expanded[WORD_MAX];
            int nf = 0;
            if (nt >= 0) {
                nf = nt;
                if (expand_words(toks, nf, gl, expanded, &nf) < 0) nf = 0;
            }
            if (nf == 0) {
                env_put(node->var, slen(node->var), "", 0);
                int b = cf_execute(node->b);
                if (b == CF_SIG_BREAK) broke = 1;
                else if (b == CF_SIG_CONTINUE) { if (--guard <= 0) broke = 1; }
                else if (b < 0) { abort_neg = 1; broke = 1; }
                else { if (--guard <= 0) broke = 1; }
                continue;
            }
            for (int fi = 0; fi < nf && !broke; ++fi) {
                if (env_put(node->var, slen(node->var), expanded[fi], 0) < 0)
                    swrite(2, "sh: for: variable value too long\n");
                int b = cf_execute(node->b);
                if (b == CF_SIG_BREAK) {
                    broke = 1;
                } else if (b == CF_SIG_CONTINUE) {
                    if (--guard <= 0) broke = 1;
                    else continue;
                } else if (b < 0) {
                    abort_neg = 1;
                    broke = 1;
                } else {
                    if (--guard <= 0) broke = 1;
                }
            }
        }
        env_restore(saved_env, saved_exports, saved_count);
        --cf_in_loop;
        if (abort_neg) { last_status = 0; return -1; }
        last_status = 0;
        return 0;
    }
default:
        return 0;
    }
}

static int cf_execute_line(const char* text) {
    cf_token_t toks[CF_SCAN_MAX];
    /* 每行重建节点池: AST 生命周期只有一行。$() 子进程 fork COW,
     * 子进程内的重建/追加不会碰父进程的池页; 池"只增"是错的 ——
     * 交互 shell 跑 ~20 行就耗尽 128 节点, 之后全部命令报语法错误。 */
    cf_pool_used = 0;
    int n = cf_scan(text, toks, CF_SCAN_MAX);
    cf_parser p;
    p.text = text;
    p.toks = toks;
    p.n = n;
    p.i = 0;
    p.err = 0;
    cf_node_t* root = cf_parse_list(&p, 0);
    int status = 0;
    if (p.err || !root || p.i < p.n || n >= CF_SCAN_MAX) {
        if (root) cf_free(root);
        /* 调试输出: 哪条失败 (bit: 1=err 2=!root 4=残留 8=token满) + 位置 */
        swrite(2, "sh: syntax error flags=");
        swrite_dec(2, (uint32_t)((p.err ? 1 : 0) | (root ? 0 : 2) |
                                 (p.i < p.n ? 4 : 0) |
                                 (n >= CF_SCAN_MAX ? 8 : 0)));
        swrite(2, " pos=");
        swrite_dec(2, (uint32_t)p.i);
        swrite(2, "/");
        swrite_dec(2, (uint32_t)p.n);
        swrite(2, " ntok=");
        swrite_dec(2, (uint32_t)n);
        swrite(2, "\n");
        status = 2;
        last_status = 2;
    } else {
        cf_current_line = text;
        status = cf_execute(root);
        cf_free(root);
        if (status == CF_SIG_BREAK || status == CF_SIG_CONTINUE)
            status = 0;   /* 顶层裸 break/continue: 不当致命错误 */
    }
    return status < 0 ? -1 : status;
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

static int execute_line(char* line) {
    char* text = expand_command_substitutions(line);
    /* 整行交给控制流引擎: 普通命令/流水线也走同一套解析,
     * ; && || ! () if/while/for 一视同仁, 不再有 "先切段再试解析" 的两层 */
    return cf_execute_line(text);
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
    /* if/while/for 块未闭合 (fi/done 缺失) 也要续行:
     * "while true; do" 按一行会报语法错误, 交互 shell 应等下一行 */
    if (cf_block_incomplete(line)) return 1;
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
