/* easy/jit_base.h — JIT 基础：寄存器/平台常量、打印辅助、内存、Jit
 * 状态、字符串数据区
 *
 * 本层只依赖标准库与 utf8.h（error_at），不含指令发射与代码生成。
 */

#ifndef RUA_JIT_BASE_H
#define RUA_JIT_BASE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../utf8.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

/**
 * 物理寄存器号（x86-64 ModRM 编码）。
 * 与 CPU 寄存器一一对应，供发射器直接使用。
 */
typedef enum {
    REG_RAX = 0,
    REG_RCX = 1,
    REG_RDX = 2,
    REG_RBX = 3,
    REG_RSP = 4,
    REG_RBP = 5,
    REG_RSI = 6,
    REG_RDI = 7,
    REG_R8 = 8,
    REG_R9 = 9,
    REG_R10 = 10,
    REG_R11 = 11,
    REG_R12 = 12,
    REG_R13 = 13,
    REG_R14 = 14,
    REG_R15 = 15
} PhysReg;

/**
 * JIT 机器码函数签名：最多 6 个 int64 参数，返回 int64。
 * 解释器把 f->jit_addr 转型为该类型后直接调用。
 */
typedef int64_t (*JitFn)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

#ifdef DEBUG
/**
 * 返回物理寄存器的可读名（仅调试输出用）。
 * @param reg 寄存器号（REG_*）
 * @return 如 "RAX"、"R12"；非法返回 "?"
 */
static const char* jit_reg_name(int reg)
{
    static const char* names[]
        = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
            "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15" };
    return (reg >= 0 && reg < 16) ? names[reg] : "?";
}
#endif /* DEBUG */

/* ==================== 平台相关常量 ==================== */

#ifdef _WIN32
#define JIT_MAX_PARAMS 4 /* Windows 只有 4 个寄存器参数 */
#define JIT_ARG_COUNT 4
#define JIT_SHADOW 32    /* Windows 调用方需预留 32 字节 shadow space */
#define JIT_SAVE_COUNT 6 /* 需要跨调用保存的 caller-saved 寄存器数 */
static const int JIT_ARG_REGS[JIT_ARG_COUNT]
    = { REG_RCX, REG_RDX, REG_R8, REG_R9 };
static const int JIT_SAVE_REGS[JIT_SAVE_COUNT]
    = { REG_RCX, REG_RDX, REG_R8, REG_R9, REG_R10, REG_R11 };
static const int JIT_VREG_TO_PHYS[14]
    = { REG_RAX, REG_RBX, REG_RDI, REG_RSI, REG_R12, REG_R13, REG_R14,
        REG_R15, REG_RCX, REG_RDX, REG_R8,  REG_R9,  REG_R10, REG_R11 };
static const int JIT_CALLEE_SAVED[7]
    = { REG_RBX, REG_RDI, REG_RSI, REG_R12, REG_R13, REG_R14, REG_R15 };
static const int JIT_CALLEE_SAVED_COUNT = 7;
#else
#define JIT_MAX_PARAMS 6 /* SysV 有 6 个寄存器参数 */
#define JIT_ARG_COUNT 6
#define JIT_SHADOW 0
#define JIT_SAVE_COUNT 8
static const int JIT_ARG_REGS[JIT_ARG_COUNT]
    = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
static const int JIT_SAVE_REGS[JIT_SAVE_COUNT]
    = { REG_RSI, REG_RDI, REG_RDX, REG_RCX, REG_R8, REG_R9, REG_R10, REG_R11 };
static const int JIT_VREG_TO_PHYS[14]
    = { REG_RAX, REG_RBX, REG_R12, REG_R13, REG_R14, REG_R15, REG_RSI,
        REG_RDI, REG_RDX, REG_RCX, REG_R8,  REG_R9,  REG_R10, REG_R11 };
static const int JIT_CALLEE_SAVED[7]
    = { REG_RBX, REG_R12, REG_R13, REG_R14, REG_R15 };
static const int JIT_CALLEE_SAVED_COUNT = 5;
#endif

/* 溢出起始 vreg：v14 及之后放入栈槽 */
#define JIT_SPILL_VREG 14

/**
 * 条件码（0F 8x / 0F 9x 指令的低 4 位）。
 * 供 setcc / jcc 发射使用。
 */
enum {
    JCC_O = 0,
    JCC_NO = 1,
    JCC_B = 2,
    JCC_AE = 3,
    JCC_E = 4,
    JCC_NE = 5,
    JCC_BE = 6,
    JCC_A = 7,
    JCC_S = 8,
    JCC_NS = 9,
    JCC_P = 10,
    JCC_NP = 11,
    JCC_L = 12,
    JCC_GE = 13,
    JCC_LE = 14,
    JCC_G = 15
};

/* ==================== 打印辅助（JIT 代码调用） ==================== */

static const char JIT_ENDL_NL[] = "\n";
static const char JIT_ENDL_NONE[] = "";
static char jit_scratch[64];

/**
 * 打印一段文本后接一个结束符。
 * 由 JIT 生成的机器码调用（参数走平台 ABI 寄存器）。
 *
 * @param msg  要打印的文本
 * @param endl 结束符（"\n" 或 ""）
 */
static void jit_print(const char* msg, const char* endl)
{
    fputs(msg, stdout);
    fputs(endl, stdout);
}

/**
 * 把整数格式化成十进制文本（写入 buf）。
 * 由 JIT 生成的机器码调用；与解释器打印数字格式一致。
 *
 * @param val 数值
 * @param buf 输出缓冲
 * @return buf（便于链式使用）
 */
static char* jit_itoa(int64_t val, char* buf)
{
    snprintf(buf, 64, "%lld", (long long)val);
    return buf;
}

/* ==================== 可执行内存（平台抽象） ==================== */

/**
 * 分配可执行内存（Linux mmap / Windows VirtualAlloc，RWX）。
 *
 * @param size 字节数
 * @return 地址，失败返回 NULL
 */
static uint8_t* jit_alloc_exec(size_t size)
{
#ifdef _WIN32
    return (uint8_t*)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                                  PAGE_EXECUTE_READWRITE);
#else
    return (uint8_t*)mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

/* ==================== Jit 编译状态 ==================== */

/**
 * 一次 JIT 编译过程的全部状态。
 *
 * @param code     代码缓冲（可执行），机器码按序写入
 * @param code_pos 当前写入位置（字节偏移）
 * @param code_cap 代码缓冲容量
 * @param data     数据缓冲（只读，存字符串字面量）
 * @param data_pos / data_cap  数据缓冲写入位置与容量
 * @param table_addr g_jit_table 数组地址（JIT 代码间接调用用）
 * @param label_pos 标签 → 代码偏移表；label_count/label_cap 管理其容量
 * @param patch_imm / patch_label 前向跳转回填表（rel32 位置 → 标签 id）
 * @param patch_count / patch_cap 回填表容量
 * @param lit_text / lit_addr 字符串字面量去重表
 * @param lit_count / lit_cap 字面量表容量
 */
typedef struct {
    uint8_t* code;
    size_t code_pos;
    size_t code_cap;
    uint8_t* data;
    size_t data_pos;
    size_t data_cap;
    int64_t table_addr;
    int64_t* label_pos;
    int label_count, label_cap;
    int64_t* patch_imm;
    int64_t* patch_label;
    int patch_count, patch_cap;
    const char** lit_text;
    int64_t* lit_addr;
    int lit_count, lit_cap;
} Jit;

/* ==================== 字符串字面量数据区 ==================== */

/**
 * 把字符串字面量放入数据缓冲，返回其绝对地址（同文本去重）。
 * JIT 代码用 movabs 直接嵌入返回的地址。
 *
 * @param jit  编译状态
 * @param text 以 \0 结尾的字面量
 * @return 数据缓冲内的绝对地址
 */
static int64_t jit_lit_addr(Jit* jit, const char* text)
{
    for (int i = 0; i < jit->lit_count; i++) {
        if (!strcmp(jit->lit_text[i], text)) return jit->lit_addr[i];
    }
    size_t len = strlen(text) + 1;
    if (jit->data_pos + len > jit->data_cap) error_at("JIT 数据缓冲不足", 0);
    int64_t addr = (int64_t)(jit->data + jit->data_pos);
    memcpy(jit->data + jit->data_pos, text, len);
    jit->data_pos += len;
    if (jit->lit_count >= jit->lit_cap) {
        jit->lit_cap = jit->lit_cap ? jit->lit_cap * 2 : 16;
        jit->lit_text = (const char**)realloc(jit->lit_text,
                                              jit->lit_cap * sizeof(char*));
        jit->lit_addr
            = (int64_t*)realloc(jit->lit_addr, jit->lit_cap * sizeof(int64_t));
    }
    jit->lit_text[jit->lit_count] = text;
    jit->lit_addr[jit->lit_count] = addr;
    jit->lit_count++;
    return addr;
}

#endif /* RUA_JIT_BASE_H */
