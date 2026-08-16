/* easy/debug.h — 调试输出与计时
 *
 * 只有在编译时定义 DEBUG 宏，这里的调试工具才生效：
 *   cc easy/rua.c -o rua -DDEBUG
 * 不定义 DEBUG 时，所有宏展开为空，不影响正常逻辑与性能。
 *
 * 提供：
 *   - DBG_PRINT(...)     与 printf 同格式的调试输出，带 [调试] 前缀；
 *   - DBG_TIMED(label, stmt)  计时一段代码（语句/表达式）并打印耗时；
 *   - DebugTimer / debug_timer_start() / debug_timer_ms()
 *     底层的计时类型与函数（始终存在，配合计时使用）。
 */

#ifndef RUA_DEBUG_H
#define RUA_DEBUG_H

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
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
 * 输出一条调试信息（与 printf 同格式）。
 * 输出到 stderr，自动带 "[调试] " 前缀并换行。
 */
#define DBG_PRINT(...)                \
    do                                \
    {                                 \
        fprintf(stderr, "[调试] ");   \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0)

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
        DBG_PRINT("%s：%.3f ms", label, debug_timer_ms(_dbg_timer)); \
    } while (0)

/**
 * 以十六进制逐字节输出一段数据（如 JIT 机器码）。
 *
 * @param label 说明文字
 * @param data  数据指针
 * @param len   字节数
 */
#define DBG_DUMP_HEX(label, data, len)                                       \
    do                                                                       \
    {                                                                        \
        const unsigned char *_dbg_d = (const unsigned char *)(data);         \
        size_t _dbg_n = (size_t)(len);                                       \
        fprintf(stderr, "[调试] %s（%zu 字节）:\n", label, _dbg_n);          \
        for (size_t _dbg_i = 0; _dbg_i < _dbg_n; _dbg_i += 16)               \
        {                                                                    \
            fprintf(stderr, "[调试]   %04zx  ", _dbg_i);                     \
            for (size_t _dbg_j = 0; _dbg_j < 16 && _dbg_i + _dbg_j < _dbg_n; \
                 _dbg_j++)                                                   \
            {                                                                \
                fprintf(stderr, "%02x ", _dbg_d[_dbg_i + _dbg_j]);           \
            }                                                                \
            fprintf(stderr, "\n");                                           \
        }                                                                    \
    } while (0)

#else /* 未定义 DEBUG：一切调试工具为空 */

#define DBG_PRINT(...) \
    do                 \
    {                  \
    } while (0)
#define DBG_TIMED(label, stmt) \
    do                         \
    {                          \
        stmt;                  \
    } while (0)
#define DBG_DUMP_HEX(label, data, len) \
    do                                 \
    {                                  \
    } while (0)

#endif /* DEBUG */

#endif /* RUA_DEBUG_H */
