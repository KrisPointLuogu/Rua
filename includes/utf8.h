/* easy/utf8.h — UTF-8 工具
 *
 * 提供 UTF-8 编码相关的基础工具函数：
 *   - 按字符读取/前进，屏蔽 UTF-8 变长字节的细节；
 *   - 判断某字符是否可作为标识符（含中文字符）；
 *   - 统一的错误上报出口。
 */

#ifndef RUA_UTF8_H
#define RUA_UTF8_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* strdup 跨平台：MSVC 只有 _strdup，MinGW/gcc 原生就有 strdup */
#ifdef _MSC_VER
#define strdup _strdup
#endif

/* 统一用 32 位无符号整数表示一个 Unicode 码点 */
typedef uint32_t Char;

/* ==================== GNU C 扩展：通用安全宏 ==================== */

/**
 * 分支预测提示：likely(x) 告诉编译器分支 x 大概率成立。
 *
 * 基于 __builtin_expect（GNU C 内建函数），配合 -O2 让编译器
 * 优先布局"成立"路径，减少跳转带来的流水线中断。
 *
 * 用法：if (likely(ptr)) { ... }
 * @param x 分支条件表达式
 * @return 原样返回 x 的值
 */
#define likely(x) __builtin_expect(!!(x), 1)

/**
 * 分支预测提示：unlikely(x) 告诉编译器分支 x 大概率不成立。
 *
 * 用于"几乎不会发生"的异常分支（如 NULL 检查、报错判断），
 * 让主路径更紧凑。
 *
 * 用法：if (unlikely(ptr == NULL)) { ... }
 * @param x 分支条件表达式
 * @return 原样返回 x 的值
 */
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * 求两个值中的较小者（无副作用版本）。
 *
 * 用语句表达式（GNU C）把 x/y 各求值一次存入临时变量，避免
 * 标准 C 宏里"实参被多次求值"的副作用；再用 typeof 自动推导类型，
 * 并用 &_a == &_b 做编译期类型一致性检查。
 *
 * 用法：int m = bc_min(a, b);
 * @param x 左值
 * @param y 右值
 * @return 较小者
 */
#define bc_min(x, y)                                     \
    ({                                                   \
        const typeof(x) _bc_a = (x);                     \
        const typeof(y) _bc_b = (y);                     \
        (void)(&_bc_a == &_bc_b);                        \
        _bc_a < _bc_b ? _bc_a : _bc_b;                   \
    })

/**
 * 求两个值中的较大者（无副作用版本）。
 *
 * 同 bc_min，基于语句表达式 + typeof，实参各求值一次。
 *
 * 用法：int m = bc_max(a, b);
 * @param x 左值
 * @param y 右值
 * @return 较大者
 */
#define bc_max(x, y)                                     \
    ({                                                   \
        const typeof(x) _bc_a = (x);                     \
        const typeof(y) _bc_b = (y);                     \
        (void)(&_bc_a == &_bc_b);                        \
        _bc_a > _bc_b ? _bc_a : _bc_b;                   \
    })

/**
 * 查看当前位置的字符（不解码前进）。
 *
 * 输入指针 p 指向一段 UTF-8 编码的字符串；
 * 根据首字节判断该字符占 1~4 字节，将其解码为一个 Unicode 码点返回，
 * 但不会移动 p。
 *
 * @param p 指向 UTF-8 字符流中当前位置的指针
 * @return 当前位置字符对应的 Unicode 码点
 */
static Char utf8_peek(const char *p)
{
    const unsigned char *s = (const unsigned char *)p;
    if (s[0] < 0x80)
        return s[0];
    if ((s[0] & 0xE0) == 0xC0)
        return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    if ((s[0] & 0xF0) == 0xE0)
        return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
}

/**
 * 前进一个 UTF-8 字符。
 *
 * 根据当前位置首字节判断字符长度，把指针 p 越过当前这个字符，
 * 指向下一个字符的起始位置。
 *
 * @param p 指向当前字符起始位置的指针，会被修改为下一个字符的起始位置
 */
static void utf8_advance(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    if (s[0] < 0x80)
    {
        *p += 1;
        return;
    }
    if ((s[0] & 0xE0) == 0xC0)
    {
        *p += 2;
        return;
    }
    if ((s[0] & 0xF0) == 0xE0)
    {
        *p += 3;
        return;
    }
    *p += 4;
}

/**
 * 判断某个字符是否可以用在标识符中。
 *
 * 标识符字符包括：英文字母（大小写）、数字、下划线，以及常用中文字符
 * （Unicode 范围 U+4E00 ~ U+9FA5）。词法分析器用本函数切分标识符/关键字。
 *
 * @param c 要判断的 Unicode 码点
 * @return 1 表示可作为标识符字符，0 表示不可以
 */
static int is_ident_char(Char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (c >= 0x4E00 && c <= 0x9FA5);
}

/* 前置声明：标注 error_at 不会正常返回（必然 exit），方便编译器优化 */
static void error_at(const char *msg, int line) __attribute__((noreturn));

/**
 * 报错并终止程序。
 *
 * 输出包含行号的错误信息到标准错误，然后以退出码 1 结束进程。
 * 全项目所有报错都统一走这里，保证错误信息格式一致。
 *
 * @param msg  错误描述文本
 * @param line 出错源码所在的行号（从 1 开始；为 0 表示未知行）
 */
static void error_at(const char *msg, int line)
{
    fprintf(stderr, "第 %d 行：%s\n", line, msg);
    exit(1);
}

#endif
