/* includes/debug_process.h — 调试输出与计时（封装为函数，printk 风格）
 *
 * 只有在编译时定义 DEBUG 宏，这里的调试工具才生效：
 *   cc rua.c -o rua -DDEBUG
 * 不定义 DEBUG 时，所有函数展开为空实现，不影响正常逻辑与性能。
 *
 * 提供：
 *   - debug_print(fmt, ...)     与 printf 同格式的调试输出，带 [调试] 前缀；
 *                               模块标签（如 "[JIT] "）由调用点写进 fmt。
 *   - debug_dump_hex(...)       十六进制逐字节转储（函数版）。
 *   - debug_dump_tokens(...)    词法分析 Token 转储（[LANG]）。
 *   - debug_print_stmts(...)    顶层语句列表转储（[LANG]）。
 *   - debug_print_fns(...)      注册函数表转储（[LANG]）。
 *   - debug_dump_callee_saved() JIT 帧 callee-saved 寄存器列表（[JIT]）。
 *   - debug_dump_calls()        JIT 函数调用列表（[JIT]）。
 *   - debug_log_jit_invoke()    JIT 调用轨迹（[JIT]）。
 *   - DebugTimer / debug_timer_start() / debug_timer_us() / debug_timer_ms()
 *     底层的计时类型与函数（始终存在，配合 DBG_TIMED 使用）。
 *   - DBG_TIMED(label, stmt)    计时一段代码（语句/表达式）并打印耗时（宏）。
 */

#ifndef RUA_DEBUG_PROCESS_H
#define RUA_DEBUG_PROCESS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "runtime.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* 标记可能未被引用的 static 函数（如 debug_dump_hex），抑制 -Wunused-function */
#if defined(__GNUC__) || defined(__clang__)
#define RUA_MAYBE_UNUSED __attribute__((unused))
#else
#define RUA_MAYBE_UNUSED
#endif

#ifdef OPTIMIZATION
#include "jit/jit_analyze.h"
#include "jit/jit_base.h"
#endif

#ifdef DEBUG

#ifdef _WIN32
/**
 * 计时器：记录开始时刻（Windows 高精度计数器）。
 */
typedef struct
{
    LARGE_INTEGER start;
} DebugTimer;

/**
 * 开始计时（Windows 用 QueryPerformanceCounter）。
 * @return 记录当前时刻的计时器
 */
static DebugTimer debug_timer_start(void)
{
    DebugTimer t;
    QueryPerformanceCounter(&t.start);
    return t;
}

/**
 * 计算从开始计时到现在经过的微秒数。
 * @param t 由 debug_timer_start() 得到的计时器
 * @return 经过的微秒数（double，可达纳秒级分辨率）
 */
static double debug_timer_us(DebugTimer t)
{
    LARGE_INTEGER freq, now;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)(now.QuadPart - t.start.QuadPart) * 1000000.0 / (double)freq.QuadPart;
}
#else
/**
 * 计时器：记录开始时刻（POSIX 单调时钟，纳秒分辨率）。
 */
typedef struct
{
    struct timespec start;
} DebugTimer;

/**
 * 开始计时（Linux/macOS 用 clock_gettime 单调时钟）。
 * @return 记录当前时刻的计时器
 */
static DebugTimer debug_timer_start(void)
{
    DebugTimer t;
    clock_gettime(CLOCK_MONOTONIC, &t.start);
    return t;
}

/**
 * 计算从开始计时到现在经过的微秒数。
 * @param t 由 debug_timer_start() 得到的计时器
 * @return 经过的微秒数（double，纳秒换算，微秒级精确）
 */
static double debug_timer_us(DebugTimer t)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double sec = (double)(now.tv_sec - t.start.tv_sec);
    double nsec = (double)(now.tv_nsec - t.start.tv_nsec);
    return sec * 1000000.0 + nsec / 1000.0;
}
#endif

/**
 * 计算从开始计时到现在经过的毫秒数（由微秒换算，保留微秒级精度）。
 * @param t 由 debug_timer_start() 得到的计时器
 * @return 经过的毫秒数（double）
 */
static double debug_timer_ms(DebugTimer t)
{
    return debug_timer_us(t) / 1000.0;
}

/**
 * 输出一条调试信息（与 printf 同格式，printk 风格）。
 * 输出到 stderr，自动带 "[调试] " 前缀并换行；模块标签写进 fmt，
 * 例如 debug_print("[JIT] 函数[%d] → 可 JIT", i)。
 * @param fmt 与 printf 同格式的格式串
 */
static void debug_print(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[调试] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/**
 * 以十六进制逐字节输出一段数据（如 JIT 机器码）。
 * @param label 说明文字
 * @param data  数据指针
 * @param len   字节数
 */
static RUA_MAYBE_UNUSED void debug_dump_hex(const char *label, const void *data, size_t len)
{
    const unsigned char *d = (const unsigned char *)data;
    fprintf(stderr, "[调试] %s（%zu 字节）:\n", label, len);
    for (size_t i = 0; i < len; i += 16)
    {
        fprintf(stderr, "[调试]   %04zx  ", i);
        for (size_t j = 0; j < 16 && i + j < len; j++)
            fprintf(stderr, "%02x ", d[i + j]);
        fprintf(stderr, "\n");
    }
}

/**
 * 转储词法分析产生的全部 Token（词法分析阶段）。
 * @param toks  Token 数组
 * @param count Token 个数（含结束标记）
 */
static void debug_dump_tokens(const Token *toks, int count)
{
    debug_print("[LANG] 词法分析：%d 个 Token（含结束标记）", count);
    for (int i = 0; i < count; i++)
    {
        Token t = toks[i];
        if (t.kind == TK_NUM)
        {
            debug_print("  Token[%02d] 行%-3d %-10s 值=%lld", i, t.line,
                        token_kind_name(t.kind), (long long)t.num);
        }
        else if (t.kind == TK_STR)
        {
            debug_print("  Token[%02d] 行%-3d %-10s 文本=\"%s\"", i, t.line,
                        token_kind_name(t.kind), t.text);
        }
        else if (t.text)
        {
            debug_print("  Token[%02d] 行%-3d %-10s 文本=%s", i, t.line,
                        token_kind_name(t.kind), t.text);
        }
        else
        {
            debug_print("  Token[%02d] 行%-3d %-10s", i, t.line,
                        token_kind_name(t.kind));
        }
    }
}

/**
 * 转储顶层语句列表（语法分析后）。
 * @param prog 程序节点（顶层语句列表）
 */
static void debug_print_stmts(const Node *prog)
{
    debug_print("[LANG] 顶层语句（%d 条）：", prog->nstmts);
    for (int i = 0; i < prog->nstmts; i++)
    {
        debug_print("  [%02d] %s", i, node_kind_name(prog->stmts[i]->kind));
    }
}

/**
 * 转储注册函数表（语法分析后）。
 * @param table 函数表（Fn* 数组）
 * @param count 函数个数
 */
static void debug_print_fns(Fn **table, int count)
{
    debug_print("[LANG] 注册函数（%d 个）：", count);
    for (int i = 0; i < count; i++)
    {
        Fn *f = table[i];
        fprintf(stderr, "[调试]   [%02d] %s(参数", i, f->name);
        for (int j = 0; j < f->nparams; j++)
            fprintf(stderr, " %s", f->params[j]);
        fprintf(stderr, " )\n");
    }
}

#ifdef OPTIMIZATION
/**
 * 转储 JIT 编译计划的 callee-saved 寄存器保存列表。
 * @param p 函数编译计划
 */
static void debug_dump_callee_saved(const FnPlan *p)
{
    fprintf(stderr, "[调试]   callee-saved 保存:");
    for (int k = 0; k < JIT_CALLEE_SAVED_COUNT; k++)
    {
        int reg = JIT_CALLEE_SAVED[k];
        if (p->callee_saved[reg])
        {
            fprintf(stderr, " %s", jit_reg_name(reg));
        }
    }
    fprintf(stderr, "\n");
}

/**
 * 转储 JIT 编译计划的函数调用列表。
 * @param p     函数编译计划
 * @param table 函数表（Fn* 数组）
 */
static void debug_dump_calls(const FnPlan *p, Fn **table)
{
    if (p->ncallees > 0)
    {
        fprintf(stderr, "[调试]   调用:");
        for (int k = 0; k < p->ncallees; k++)
        {
            int j = p->callees[k];
            fprintf(stderr, " [%d]%s", j, table[j]->name);
        }
        fprintf(stderr, "\n");
    }
}
#endif /* OPTIMIZATION */

#ifdef OPTIMIZATION
/**
 * 输出一次 JIT 调用轨迹（解释器 → JIT 机器码）。
 * @param f     被调用的函数记录
 * @param a     实参数组（int64）
 * @param nargs 实参个数
 * @param r     返回值（int64）
 */
static void debug_log_jit_invoke(const Fn *f, const int64_t *a, int nargs,
                                 int64_t r)
{
    fprintf(stderr, "[调试] JIT 调用 %s(", f->name);
    for (int i = 0; i < nargs; i++)
    {
        if (i)
            fprintf(stderr, ", ");
        fprintf(stderr, "%lld", (long long)a[i]);
    }
    fprintf(stderr, ") → %lld\n", (long long)r);
}
#endif /* OPTIMIZATION */

/**
 * 计时一段代码并打印耗时（微秒）。
 *
 * 用法示例：
 *   DBG_TIMED("编译", prog = compile(src));
 *   DBG_TIMED("运行", exec(prog, &ctx));
 *
 * @param label 耗时说明文字
 * @param stmt  要执行的语句或表达式（整个原样放入 do{}while(0) 内）
 */
#define DBG_TIMED(label, stmt)                                       \
    do                                                               \
    {                                                                \
        DebugTimer _dbg_timer = debug_timer_start();                 \
        stmt;                                                        \
        debug_print("%s：%.3f ms", label, debug_timer_ms(_dbg_timer)); \
    } while (0)

#else /* 未定义 DEBUG：一切调试工具为空实现 */

static void debug_print(const char *fmt, ...)
{
    (void)fmt;
}
static RUA_MAYBE_UNUSED void debug_dump_hex(const char *label, const void *data, size_t len)
{
    (void)label;
    (void)data;
    (void)len;
}
static void debug_dump_tokens(const Token *toks, int count)
{
    (void)toks;
    (void)count;
}
static void debug_print_stmts(const Node *prog)
{
    (void)prog;
}
static void debug_print_fns(Fn **table, int count)
{
    (void)table;
    (void)count;
}
#ifdef OPTIMIZATION
static void debug_dump_callee_saved(const FnPlan *p)
{
    (void)p;
}
static void debug_dump_calls(const FnPlan *p, Fn **table)
{
    (void)p;
    (void)table;
}
static void debug_log_jit_invoke(const Fn *f, const int64_t *a, int nargs,
                                 int64_t r)
{
    (void)f;
    (void)a;
    (void)nargs;
    (void)r;
}
#endif /* OPTIMIZATION */


#define DBG_TIMED(label, stmt) \
    do                         \
    {                          \
        stmt;                  \
    } while (0)

#endif /* DEBUG */

#endif /* RUA_DEBUG_PROCESS_H */
