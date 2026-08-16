/* easy/jit_debug.h — JIT 机器码反汇编输出（仅 DEBUG）
 *
 * 由于指令全部由我们自己发射、指令集有限，这里做一个针对性的小反汇编器：
 * 只解码 easy/jit_emit.h 可能产生的指令，输出 Intel 风格汇编。
 * 未识别字节按 .byte 输出。
 */

#ifndef RUA_JIT_DEBUG_H
#define RUA_JIT_DEBUG_H

#include "../debug.h"
#include "jit_base.h"

#ifdef DEBUG

/**
 * 返回条件码对应的 jcc 助记符。
 * @param cc 条件码（JCC_*）
 * @return 如 "je"、"jne"
 */
static const char* jit_asm_jcc(int cc)
{
    static const char* n[16]
        = { "jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
            "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg" };
    return (cc >= 0 && cc < 16) ? n[cc] : "j??";
}

/**
 * 返回条件码对应的 setcc 助记符。
 * @param cc 条件码（JCC_*）
 * @return 如 "sete"、"setne"
 */
static const char* jit_asm_setcc(int cc)
{
    static const char* n[16] = { "seto", "setno", "setb",  "setae",
                                 "sete", "setne", "setbe", "seta",
                                 "sets", "setns", "setp",  "setnp",
                                 "setl", "setge", "setle", "setg" };
    return (cc >= 0 && cc < 16) ? n[cc] : "set??";
}

/**
 * 把内存操作数 [base+disp] 格式化成文本。
 * @param buf  输出缓冲
 * @param n    缓冲大小
 * @param base 基址寄存器
 * @param disp 位移
 */
static void jit_asm_mem(char* buf, size_t n, int base, int32_t disp)
{
    if (disp == 0) {
        snprintf(buf, n, "[%s]", jit_reg_name(base));
    } else if (disp > 0) {
        snprintf(buf, n, "[%s+0x%x]", jit_reg_name(base), (unsigned)disp);
    } else {
        snprintf(buf, n, "[%s-0x%x]", jit_reg_name(base), (unsigned)(-disp));
    }
}

/**
 * 反汇编一段机器码并输出到 stderr。
 *
 * @param label 说明文字（如函数名）
 * @param code  机器码起始
 * @param len   字节数
 */
static void jit_dump_disasm(const char* label, const uint8_t* code, size_t len)
{
    fprintf(stderr, "[调试] %s 汇编:\n", label);
    size_t pos = 0;
    while (pos < len) {
        size_t start = pos;
        /* REX 前缀 */
        int rex = 0;
        while (pos < len && (code[pos] & 0xF0) == 0x40) rex = code[pos++];
        int R = (rex >> 2) & 1, B = rex & 1;
        if (pos >= len) break;
        uint8_t op = code[pos];
        int mod = 0, oreg = 0, erm = 0;
        int32_t disp = 0;
        char mne[24] = ".byte";
        char op1[48] = "", op2[48] = "";
        /* 长度含 REX 前缀（pos - start）+ 操作码 1 字节 */
        int ilen = (int)(pos - start) + 1;
        int ok = 1;

#define ASM_MODRM()                                                            \
    do {                                                                       \
        if (pos + 1 >= len) {                                                  \
            ok = 0;                                                            \
        } else {                                                               \
            uint8_t mr = code[++pos];                                          \
            ilen++;                                                            \
            mod = mr >> 6;                                                     \
            oreg = (mr >> 3) & 7;                                              \
            erm = mr & 7;                                                      \
            if (mod == 2) {                                                    \
                if (pos + 4 >= len) {                                          \
                    ok = 0;                                                    \
                } else {                                                       \
                    memcpy(&disp, code + pos + 1, 4);                          \
                    pos += 4;                                                  \
                    ilen += 4;                                                 \
                }                                                              \
            } else if (mod == 1) {                                             \
                if (pos + 1 >= len) {                                          \
                    ok = 0;                                                    \
                } else {                                                       \
                    disp = (int8_t)code[++pos];                                \
                    ilen += 1;                                                 \
                }                                                              \
            }                                                                  \
        }                                                                      \
    } while (0)

#define ASM_OPERAND(buf, use_mem)                                              \
    do {                                                                       \
        if (mod == 3) {                                                        \
            snprintf(buf, sizeof(buf), "%s", jit_reg_name(erm | (B << 3)));    \
        } else {                                                               \
            jit_asm_mem(buf, sizeof(buf), erm | (B << 3), disp);               \
            (void)(use_mem);                                                   \
        }                                                                      \
    } while (0)

#define ASM_BYTES()                                                            \
    do {                                                                       \
        fprintf(stderr, "[调试]   %04zx  ", start);                            \
        for (size_t b = start; b < start + (size_t)ilen && b < len; b++) {     \
            fprintf(stderr, "%02x ", code[b]);                                 \
        }                                                                      \
        for (size_t b = (size_t)ilen; b < 8; b++) fprintf(stderr, "   ");      \
        fprintf(stderr, "%-8s %s %s\n", mne, op1, op2);                        \
    } while (0)

        if (op >= 0x50 && op <= 0x57) { /* push r */
            snprintf(mne, sizeof(mne), "push");
            snprintf(op1, sizeof(op1), "%s",
                     jit_reg_name((op - 0x50) | (B << 3)));
        } else if (op >= 0x58 && op <= 0x5F) { /* pop r */
            snprintf(mne, sizeof(mne), "pop");
            snprintf(op1, sizeof(op1), "%s",
                     jit_reg_name((op - 0x58) | (B << 3)));
        } else if (op == 0x8B) { /* mov r64, r/m64 */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "mov");
            snprintf(op1, sizeof(op1), "%s", jit_reg_name(oreg | (R << 3)));
            ASM_OPERAND(op2, 1);
        } else if (op == 0x89) { /* mov r/m64, r64 */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "mov");
            ASM_OPERAND(op1, 1);
            snprintf(op2, sizeof(op2), "%s", jit_reg_name(oreg | (R << 3)));
        } else if (op == 0xC7) { /* mov r/m64, imm32 */
            ASM_MODRM();
            if (ok && pos + 4 < len) {
                int32_t imm;
                memcpy(&imm, code + pos + 1, 4);
                pos += 4;
                ilen += 4;
                snprintf(mne, sizeof(mne), "mov");
                ASM_OPERAND(op1, 1);
                snprintf(op2, sizeof(op2), "0x%x", (unsigned)imm);
            }
        } else if (op >= 0xB8 && op <= 0xBF) { /* movabs r64, imm64 */
            if (pos + 8 < len) {
                int64_t imm;
                memcpy(&imm, code + pos + 1, 8);
                pos += 8;
                ilen += 8;
                snprintf(mne, sizeof(mne), "movabs");
                snprintf(op1, sizeof(op1), "%s",
                         jit_reg_name((op - 0xB8) | (B << 3)));
                snprintf(op2, sizeof(op2), "0x%llx", (unsigned long long)imm);
            }
        } else if (op == 0x03 || op == 0x2B || op == 0x3B) {
            ASM_MODRM(); /* add/sub/cmp r64, r/m64 */
            snprintf(mne, sizeof(mne),
                     op == 0x03 ? "add" :
                     op == 0x2B ? "sub" :
                                  "cmp");
            snprintf(op1, sizeof(op1), "%s", jit_reg_name(oreg | (R << 3)));
            ASM_OPERAND(op2, 1);
        } else if (op == 0x85) { /* test r64, r/m64 */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "test");
            snprintf(op1, sizeof(op1), "%s", jit_reg_name(oreg | (R << 3)));
            ASM_OPERAND(op2, 1);
        } else if (op == 0x31) { /* xor r32, r/m32（仅 xor eax,eax） */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "xor");
            snprintf(op1, sizeof(op1), "eax");
            snprintf(op2, sizeof(op2), "eax");
        } else if (op == 0x99) { /* cqo（REX.W 前缀） */
            snprintf(mne, sizeof(mne), "cqo");
        } else if (op == 0xF7) { /* idiv r/m64（/7） */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "idiv");
            ASM_OPERAND(op1, 1);
        } else if (op == 0x81) { /* add/sub r/m64, imm32（/0 或 /5） */
            ASM_MODRM();
            if (ok && pos + 4 < len) {
                int32_t imm;
                memcpy(&imm, code + pos + 1, 4);
                pos += 4;
                ilen += 4;
                snprintf(mne, sizeof(mne), oreg == 0 ? "add" : "sub");
                ASM_OPERAND(op1, 1);
                snprintf(op2, sizeof(op2), "0x%x", (unsigned)imm);
            }
        } else if (op == 0xFF) { /* call r/m64（/2） */
            ASM_MODRM();
            snprintf(mne, sizeof(mne), "call");
            ASM_OPERAND(op1, 1);
        } else if (op == 0xE9) { /* jmp rel32 */
            if (pos + 4 < len) {
                int32_t rel;
                memcpy(&rel, code + pos + 1, 4);
                pos += 4;
                ilen += 4;
                snprintf(mne, sizeof(mne), "jmp");
                snprintf(op1, sizeof(op1), "0x%llx",
                         (unsigned long long)(start + ilen + rel));
            }
        } else if (op == 0xC3) { /* ret */
            snprintf(mne, sizeof(mne), "ret");
        } else if (op == 0x90) { /* nop */
            snprintf(mne, sizeof(mne), "nop");
        } else if (op == 0x0F) {
            if (pos + 1 >= len) {
                ok = 0;
            } else {
                uint8_t op2b = code[++pos];
                ilen++;
                if (op2b >= 0x80 && op2b <= 0x8F) { /* jcc rel32 */
                    if (pos + 4 >= len) {
                        ok = 0;
                    } else {
                        int32_t rel;
                        memcpy(&rel, code + pos + 1, 4);
                        pos += 4;
                        ilen += 4;
                        snprintf(mne, sizeof(mne), "%s",
                                 jit_asm_jcc(op2b & 0xF));
                        snprintf(op1, sizeof(op1), "0x%llx",
                                 (unsigned long long)(start + ilen + rel));
                    }
                } else if (op2b >= 0x90 && op2b <= 0x9F) { /* setcc r/m8 */
                    ASM_MODRM();
                    snprintf(mne, sizeof(mne), "%s", jit_asm_setcc(op2b & 0xF));
                    snprintf(op1, sizeof(op1), "al");
                } else if (op2b == 0xB6) { /* movzx r32, r/m8（仅 eax,al） */
                    ASM_MODRM();
                    snprintf(mne, sizeof(mne), "movzx");
                    snprintf(op1, sizeof(op1), "eax");
                    snprintf(op2, sizeof(op2), "al");
                } else if (op2b == 0xAF) { /* imul r64, r/m64 */
                    ASM_MODRM();
                    snprintf(mne, sizeof(mne), "imul");
                    snprintf(op1, sizeof(op1), "%s",
                             jit_reg_name(oreg | (R << 3)));
                    ASM_OPERAND(op2, 1);
                } else {
                    ok = 0;
                }
            }
        } else {
            ok = 0;
        }

        if (!ok) {
            /* 无法识别的指令：整条按 .byte 输出 */
            ilen = 1;
            snprintf(mne, sizeof(mne), ".byte");
            snprintf(op1, sizeof(op1), "0x%02x", code[start]);
        }
        ASM_BYTES();
        pos = start + (size_t)ilen;

#undef ASM_MODRM
#undef ASM_OPERAND
#undef ASM_BYTES
    }
}

#endif /* DEBUG */

#endif /* RUA_JIT_DEBUG_H */
