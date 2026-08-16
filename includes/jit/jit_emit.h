/* easy/jit_emit.h — JIT 指令发射器与标签回填
 *
 * 把 x86-64 指令逐字节写进 Jit 的代码缓冲。
 * 所有函数都以 `Jit* jit` 打头（指针传参），不出现结构体值参数。
 */

#ifndef RUA_JIT_EMIT_H
#define RUA_JIT_EMIT_H

#include "jit_base.h"

/**
 * 确保代码缓冲还能容纳 n 字节，否则报错。
 * 代码缓冲在 jit_compile_all 里一次性分配，不做动态扩容。
 *
 * @param jit 编译状态
 * @param n   需要的字节数
 */
static void jit_ensure(Jit* jit, size_t n)
{
    if (jit->code_pos + n > jit->code_cap) error_at("JIT 代码缓冲不足", 0);
}

/**
 * 写入 1 字节。
 * @param jit 编译状态
 * @param v   字节值
 */
static void jit_emit8(Jit* jit, uint8_t v)
{
    jit_ensure(jit, 1);
    jit->code[jit->code_pos++] = v;
}

/**
 * 写入 4 字节（小端）。
 * @param jit 编译状态
 * @param v   32 位值
 */
static void jit_emit32(Jit* jit, int32_t v)
{
    jit_ensure(jit, 4);
    memcpy(jit->code + jit->code_pos, &v, 4);
    jit->code_pos += 4;
}

/**
 * 写入 8 字节（小端）。
 * @param jit 编译状态
 * @param v   64 位值
 */
static void jit_emit64(Jit* jit, int64_t v)
{
    jit_ensure(jit, 8);
    memcpy(jit->code + jit->code_pos, &v, 8);
    jit->code_pos += 8;
}

/**
 * 发射 REX 前缀（W/R/X/B 位；全 0 时不发射）。
 *
 * @param jit 编译状态
 * @param w   W 位（64 位操作数）
 * @param r   R 位（扩展 reg 字段）
 * @param x   X 位（扩展 SIB 索引）
 * @param b   B 位（扩展 rm 字段）
 */
static void jit_emit_rex(Jit* jit, int w, int r, int x, int b)
{
    uint8_t rex = 0x40 | (w << 3) | (r << 2) | (x << 1) | b;
    if (rex != 0x40) jit_emit8(jit, rex);
}

/**
 * 发射 ModRM 字节。
 *
 * @param jit  编译状态
 * @param mod  mod 字段（0 内存无偏移 / 1 内存 disp8 / 2 内存 disp32 / 3
 * 寄存器）
 * @param oreg reg 字段（操作码扩展或目标寄存器）
 * @param ereg rm 字段（源寄存器或基址）
 */
static void jit_emit_modrm(Jit* jit, int mod, int oreg, int ereg)
{ jit_emit8(jit, (uint8_t)((mod << 6) | ((oreg & 7) << 3) | (ereg & 7))); }

/**
 * 发射 mov dst, src（r64 ← r64，寄存器间拷贝）。
 * @param jit 编译状态
 * @param dst 目标寄存器
 * @param src 源寄存器
 */
static void jit_emit_mov(Jit* jit, int dst, int src)
{
    if (dst == src) return;
    jit_emit_rex(jit, 1, dst >= 8, 0, src >= 8);
    jit_emit8(jit, 0x8B);
    jit_emit_modrm(jit, 3, dst, src);
}

/**
 * 发射 mov reg, imm32（符号扩展到 64）。
 * 注意必须用 C7 /0 编码；B8+r 配 REX.W 是 movabs imm64，会吞掉后续字节。
 *
 * @param jit 编译状态
 * @param reg 目标寄存器
 * @param imm 32 位立即数
 */
static void jit_emit_mov32(Jit* jit, int reg, int32_t imm)
{
    jit_emit_rex(jit, 1, 0, 0, reg >= 8);
    jit_emit8(jit, 0xC7);
    jit_emit_modrm(jit, 3, 0, reg);
    jit_emit32(jit, imm);
}

/**
 * 发射 mov reg, imm64（movabs）。
 * 用于把绝对地址（函数/表/数据）嵌入机器码。
 *
 * @param jit 编译状态
 * @param reg 目标寄存器
 * @param imm 64 位立即数
 */
static void jit_emit_mov64(Jit* jit, int reg, int64_t imm)
{
    jit_emit_rex(jit, 1, 0, 0, reg >= 8);
    jit_emit8(jit, (uint8_t)(0xB8 | (reg & 7)));
    jit_emit64(jit, imm);
}

/**
 * 发射 mov reg, [rbp+disp32]（读帧内槽位）。
 * @param jit  编译状态
 * @param reg  目标寄存器
 * @param disp 相对 rbp 的偏移
 */
static void jit_emit_load_rbp(Jit* jit, int reg, int32_t disp)
{
    jit_emit_rex(jit, 1, reg >= 8, 0, 0);
    jit_emit8(jit, 0x8B);
    jit_emit_modrm(jit, 2, reg, REG_RBP);
    jit_emit32(jit, disp);
}

/**
 * 发射 mov [rbp+disp32], reg（写帧内槽位）。
 * @param jit  编译状态
 * @param disp 相对 rbp 的偏移
 * @param reg  源寄存器
 */
static void jit_emit_store_rbp(Jit* jit, int32_t disp, int reg)
{
    jit_emit_rex(jit, 1, reg >= 8, 0, 0);
    jit_emit8(jit, 0x89);
    jit_emit_modrm(jit, 2, reg, REG_RBP);
    jit_emit32(jit, disp);
}

/**
 * 发射 mov rax, [rax + disp32]（读函数表项）。
 * @param jit  编译状态
 * @param disp 相对 rax 的偏移（函数下标 × 8）
 */
static void jit_emit_load_rax_idx(Jit* jit, int32_t disp)
{
    jit_emit_rex(jit, 1, 0, 0, 0);
    jit_emit8(jit, 0x8B);
    jit_emit_modrm(jit, 2, REG_RAX, REG_RAX);
    jit_emit32(jit, disp);
}

/**
 * 发射 add/sub/imul dst, src（寄存器间运算）。
 * @param jit 编译状态
 * @param dst 目标寄存器
 * @param src 源寄存器
 */
static void jit_emit_add(Jit* jit, int dst, int src)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, src >= 8);
    jit_emit8(jit, 0x03);
    jit_emit_modrm(jit, 3, dst, src);
}
static void jit_emit_sub(Jit* jit, int dst, int src)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, src >= 8);
    jit_emit8(jit, 0x2B);
    jit_emit_modrm(jit, 3, dst, src);
}
static void jit_emit_imul(Jit* jit, int dst, int src)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, src >= 8);
    jit_emit8(jit, 0x0F);
    jit_emit8(jit, 0xAF);
    jit_emit_modrm(jit, 3, dst, src);
}

/**
 * 发射 add/sub/imul dst, [rbp+disp32]（第二个操作数为帧内内存）。
 * @param jit  编译状态
 * @param dst  目标寄存器
 * @param disp 相对 rbp 的偏移
 */
static void jit_emit_add_mem(Jit* jit, int dst, int32_t disp)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, 0);
    jit_emit8(jit, 0x03);
    jit_emit_modrm(jit, 2, dst, REG_RBP);
    jit_emit32(jit, disp);
}
static void jit_emit_sub_mem(Jit* jit, int dst, int32_t disp)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, 0);
    jit_emit8(jit, 0x2B);
    jit_emit_modrm(jit, 2, dst, REG_RBP);
    jit_emit32(jit, disp);
}
static void jit_emit_imul_mem(Jit* jit, int dst, int32_t disp)
{
    jit_emit_rex(jit, 1, dst >= 8, 0, 0);
    jit_emit8(jit, 0x0F);
    jit_emit8(jit, 0xAF);
    jit_emit_modrm(jit, 2, dst, REG_RBP);
    jit_emit32(jit, disp);
}

/**
 * 发射 cmp a, b（b 可为寄存器；比较 a - b，置标志位）。
 * @param jit 编译状态
 * @param a   左操作数寄存器
 * @param b   右操作数寄存器
 */
static void jit_emit_cmp(Jit* jit, int a, int b)
{
    jit_emit_rex(jit, 1, a >= 8, 0, b >= 8);
    jit_emit8(jit, 0x3B);
    jit_emit_modrm(jit, 3, a, b);
}
static void jit_emit_cmp_mem(Jit* jit, int a, int32_t disp)
{
    jit_emit_rex(jit, 1, a >= 8, 0, 0);
    jit_emit8(jit, 0x3B);
    jit_emit_modrm(jit, 2, a, REG_RBP);
    jit_emit32(jit, disp);
}

/**
 * 发射 test reg, reg（reg & reg，置 ZF，用于判零）。
 * @param jit 编译状态
 * @param reg 被测试的寄存器
 */
static void jit_emit_test(Jit* jit, int reg)
{
    jit_emit_rex(jit, 1, 0, 0, reg >= 8);
    jit_emit8(jit, 0x85);
    jit_emit_modrm(jit, 3, reg, reg);
}

/**
 * 发射 setcc al（把条件码结果写入 AL）。
 * 配合 movzx eax, al 得到 0/1 的完整 64 位值。
 *
 * @param jit 编译状态
 * @param cc  条件码（JCC_*）
 */
static void jit_emit_setcc_al(Jit* jit, int cc)
{
    jit_emit8(jit, 0x0F);
    jit_emit8(jit, (uint8_t)(0x90 | cc));
    jit_emit8(jit, 0xC0);
}

/**
 * 发射 movzx eax, al（把 AL 零扩展到 EAX，同时清 RAX 高 32 位）。
 * @param jit 编译状态
 */
static void jit_emit_movzx_al(Jit* jit)
{
    jit_emit8(jit, 0x0F);
    jit_emit8(jit, 0xB6);
    jit_emit8(jit, 0xC0);
}

/**
 * 发射 cqo（RAX 符号扩展到 RDX:RAX，供 idiv 使用）。
 * @param jit 编译状态
 */
static void jit_emit_cqo(Jit* jit)
{
    jit_emit_rex(jit, 1, 0, 0, 0);
    jit_emit8(jit, 0x99);
}

/**
 * 发射 idiv 寄存器版（商→RAX，余数→RDX）。
 * @param jit 编译状态
 * @param reg 除数寄存器
 */
static void jit_emit_idiv_reg(Jit* jit, int reg)
{
    jit_emit_rex(jit, 1, 0, 0, reg >= 8);
    jit_emit8(jit, 0xF7);
    jit_emit_modrm(jit, 3, 7, reg);
}

/**
 * 发射 idiv [rbp+disp32]（除数在帧内）。
 * @param jit  编译状态
 * @param disp 相对 rbp 的偏移
 */
static void jit_emit_idiv_mem(Jit* jit, int32_t disp)
{
    jit_emit_rex(jit, 1, 0, 0, 0);
    jit_emit8(jit, 0xF7);
    jit_emit_modrm(jit, 2, 7, REG_RBP);
    jit_emit32(jit, disp);
}

/**
 * 发射 push reg。
 * @param jit 编译状态
 * @param reg 寄存器
 */
static void jit_emit_push(Jit* jit, int reg)
{
    jit_emit_rex(jit, 0, 0, 0, reg >= 8);
    jit_emit8(jit, (uint8_t)(0x50 | (reg & 7)));
}

/**
 * 发射 pop reg。
 * @param jit 编译状态
 * @param reg 寄存器
 */
static void jit_emit_pop(Jit* jit, int reg)
{
    jit_emit_rex(jit, 0, 0, 0, reg >= 8);
    jit_emit8(jit, (uint8_t)(0x58 | (reg & 7)));
}

/**
 * 发射 sub rsp, imm32（分配栈帧）。
 * @param jit 编译状态
 * @param imm 帧大小
 */
static void jit_emit_sub_rsp(Jit* jit, int32_t imm)
{
    jit_emit_rex(jit, 1, 0, 0, 0);
    jit_emit8(jit, 0x81);
    jit_emit_modrm(jit, 3, 5, REG_RSP);
    jit_emit32(jit, imm);
}

/**
 * 发射 add rsp, imm32（释放栈帧）。
 * @param jit 编译状态
 * @param imm 帧大小
 */
static void jit_emit_add_rsp(Jit* jit, int32_t imm)
{
    jit_emit_rex(jit, 1, 0, 0, 0);
    jit_emit8(jit, 0x81);
    jit_emit_modrm(jit, 3, 0, REG_RSP);
    jit_emit32(jit, imm);
}

/**
 * 发射 call reg（间接调用）。
 * @param jit 编译状态
 * @param reg 目标地址寄存器
 */
static void jit_emit_call_reg(Jit* jit, int reg)
{
    jit_emit_rex(jit, 0, 0, 0, reg >= 8);
    jit_emit8(jit, 0xFF);
    jit_emit_modrm(jit, 3, 2, reg);
}

/**
 * 发射 xor eax, eax（清 RAX 为 0）。
 * @param jit 编译状态
 */
static void jit_emit_xor_eax(Jit* jit)
{
    jit_emit8(jit, 0x31);
    jit_emit8(jit, 0xC0);
}

/**
 * 发射 ret。
 * @param jit 编译状态
 */
static void jit_emit_ret(Jit* jit) { jit_emit8(jit, 0xC3); }

/* ==================== 标签与回填 ==================== */

/**
 * 新建一个标签，返回其 id。
 * 标签初始未绑定（label_pos = -1）。
 *
 * @param jit 编译状态
 * @return 标签 id（供 bind / jmp / jcc 使用）
 */
static int64_t jit_new_label(Jit* jit)
{
    if (jit->label_count >= jit->label_cap) {
        jit->label_cap = jit->label_cap ? jit->label_cap * 2 : 16;
        jit->label_pos = (int64_t*)realloc(jit->label_pos,
                                           jit->label_cap * sizeof(int64_t));
    }
    jit->label_pos[jit->label_count] = -1;
    return jit->label_count++;
}

/**
 * 把标签绑定到当前代码位置，并回填所有前向跳转。
 * @param jit 编译状态
 * @param id  标签 id
 */
static void jit_bind_label(Jit* jit, int64_t id)
{
    jit->label_pos[id] = (int64_t)jit->code_pos;
    for (int i = 0; i < jit->patch_count;) {
        if (jit->patch_label[i] == id) {
            int64_t off = jit->label_pos[id] - (jit->patch_imm[i] + 4);
            memcpy(jit->code + jit->patch_imm[i], &off, 4);
            jit->patch_label[i] = jit->patch_label[--jit->patch_count];
            jit->patch_imm[i] = jit->patch_imm[jit->patch_count];
        } else {
            i++;
        }
    }
}

/**
 * 发射 jmp rel32 → 标签。
 * 标签已绑定则直接写偏移，否则记录回填。
 *
 * @param jit 编译状态
 * @param id  目标标签 id
 */
static void jit_emit_jmp(Jit* jit, int64_t id)
{
    int64_t p = (int64_t)jit->code_pos;
    jit_emit8(jit, 0xE9);
    jit_emit32(jit, 0);
    int64_t imm = p + 1;
    if (jit->label_pos[id] >= 0) {
        int64_t off = jit->label_pos[id] - (imm + 4);
        memcpy(jit->code + imm, &off, 4);
    } else {
        if (jit->patch_count >= jit->patch_cap) {
            jit->patch_cap = jit->patch_cap ? jit->patch_cap * 2 : 16;
            jit->patch_imm
                = (int64_t*)realloc(jit->patch_imm,
                                    jit->patch_cap * sizeof(int64_t));
            jit->patch_label
                = (int64_t*)realloc(jit->patch_label,
                                    jit->patch_cap * sizeof(int64_t));
        }
        jit->patch_imm[jit->patch_count] = imm;
        jit->patch_label[jit->patch_count] = id;
        jit->patch_count++;
    }
}

/**
 * 发射 jcc rel32 → 标签（条件跳转，如 JCC_E 即 je）。
 * 标签已绑定则直接写偏移，否则记录回填。
 *
 * @param jit 编译状态
 * @param id  目标标签 id
 * @param cc  条件码（JCC_*）
 */
static void jit_emit_jcc(Jit* jit, int64_t id, int cc)
{
    int64_t p = (int64_t)jit->code_pos;
    jit_emit8(jit, 0x0F);
    jit_emit8(jit, (uint8_t)(0x80 | cc));
    jit_emit32(jit, 0);
    int64_t imm = p + 2;
    if (jit->label_pos[id] >= 0) {
        int64_t off = jit->label_pos[id] - (imm + 4);
        memcpy(jit->code + imm, &off, 4);
    } else {
        if (jit->patch_count >= jit->patch_cap) {
            jit->patch_cap = jit->patch_cap ? jit->patch_cap * 2 : 16;
            jit->patch_imm
                = (int64_t*)realloc(jit->patch_imm,
                                    jit->patch_cap * sizeof(int64_t));
            jit->patch_label
                = (int64_t*)realloc(jit->patch_label,
                                    jit->patch_cap * sizeof(int64_t));
        }
        jit->patch_imm[jit->patch_count] = imm;
        jit->patch_label[jit->patch_count] = id;
        jit->patch_count++;
    }
}

#endif /* RUA_JIT_EMIT_H */
