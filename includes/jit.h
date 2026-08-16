/* easy/jit.h — JIT 编译器入口（汇总头）
 *
 * 把可 JIT 的纯整数用户函数直接从 AST 编译成 x86-64 机器码：
 *   - 固定 vreg→物理寄存器映射（v0=RAX，形参/局部/临时按序占 v1..，v14+
 * 溢出到栈）；
 *   - 函数按平台原生整数调用约定生成，解释器可当普通函数指针直接调用；
 *   - 两遍编译：先给函数表占位，再逐个发射机器码（保证递归/互调正确）；
 *   - 函数内支持 喵叫 打印（整数 + 字符串字面量实参，经 jit_print/jit_itoa）。
 *
 * 分文件（头文件即实现，无构建系统，仅 OPTIMIZATION 时由 rua.c 引入）：
 *   jit_base.h     基础：寄存器/平台常量、打印辅助、可执行内存、Jit
 * 状态、字面量数据区 jit_emit.h     指令发射器与标签回填 jit_analyze.h  vreg
 * 工具、变量表、可 JIT 判定与帧布局 jit_codegen.h  表达式/语句 → 机器码 jit.h
 * 入口：两遍编译（jit_compile_all）与解释器分发（jit_invoke）
 *
 * 设计文档见 easy/JIT.md。
 */

#ifndef RUA_JIT_H
#define RUA_JIT_H

#include "debug.h"
#include "jit/jit_codegen.h"
#include "jit/jit_debug.h"

/* ==================== 函数表与两遍编译 ==================== */

/* 机器码函数入口表：按下标存地址，JIT 代码经它间接调用（递归/互调正确） */
static int64_t g_jit_table[256];

/**
 * 编译全部可 JIT 函数（两遍：先占表，再逐个发射）。
 *
 * 第一遍给每个可 JIT 函数在 g_jit_table 里占位（非零哨兵），
 * 使编译期引用目标已有槽位；第二遍逐个生成机器码并写回地址。
 * 结果写回 table[i]->jittable / table[i]->jit_addr。
 *
 * @param table 函数表（Fn** 数组）
 * @param count 函数个数
 */
static void jit_compile_all(Fn **table, int count)
{
    if (count > 256)
        count = 256;
    memset(g_jit_table, 0, sizeof(g_jit_table));

    Jit jit;
    memset(&jit, 0, sizeof(jit));
    jit.code_cap = 1 << 20;
    jit.data_cap = 1 << 20;
    jit.code = jit_alloc_exec(jit.code_cap);
    jit.data = (uint8_t *)malloc(jit.data_cap);
    jit.table_addr = (int64_t)g_jit_table;
    if (!jit.code)
    {
        DBG_PRINT("JIT 代码内存分配失败");
        return;
    }

    FnPlan *plans = (FnPlan *)calloc(count, sizeof(FnPlan));
    for (int i = 0; i < count; i++)
    {
        jit_plan_function(table[i], table, count, &plans[i]);
        if (plans[i].jittable)
        {
            DBG_PRINT("函数[%d] %s → 可 JIT", i, table[i]->name);
        }
        else
        {
            DBG_PRINT("函数[%d] %s → 不可 JIT（%s）", i, table[i]->name,
                      plans[i].reject_reason ? plans[i].reject_reason : "未知原因");
        }
    }

    /* 闭包传递：调用不可 JIT 函数 → 自己也不可 JIT */
    int changed = 1;
    while (changed)
    {
        changed = 0;
        for (int i = 0; i < count; i++)
        {
            if (!plans[i].jittable)
                continue;
            for (int k = 0; k < plans[i].ncallees; k++)
            {
                int j = plans[i].callees[k];
                if (j < 0 || j >= count || !plans[j].jittable)
                {
                    plans[i].jittable = 0;
                    plans[i].reject_reason = "调用了不可 JIT 的函数";
                    DBG_PRINT("函数[%d] %s → 因调用[%d] %s 改为不可 JIT", i,
                              table[i]->name, j,
                              (j >= 0 && j < count) ? table[j]->name : "?");
                    changed = 1;
                    break;
                }
            }
        }
    }

    /* 第一遍：占位，使递归/互调在编译期可引用 */
    for (int i = 0; i < count; i++)
    {
        if (plans[i].jittable)
            g_jit_table[i] = 1;
    }

    /* 第二遍：实际编译 */
    for (int i = 0; i < count; i++)
    {
        if (!plans[i].jittable)
        {
            table[i]->jittable = 0;
            continue;
        }
        int64_t offset = jit_emit_function(&jit, &plans[i], table[i], table, count, i);
        int64_t addr = (int64_t)(jit.code + offset);
        g_jit_table[i] = addr;
        table[i]->jittable = 1;
        table[i]->jit_addr = (void *)addr;
#ifdef DEBUG
        {
            size_t size = (size_t)(jit.code_pos - offset);
            DBG_PRINT("编译函数[%d] %s → 地址=%p 机器码=%zu 字节", i,
                      table[i]->name, (void *)addr, size);
            DBG_PRINT(
                "  vreg：最多 v%d（参数 %d 个，临时/局部 %d 个，溢出槽 %d 个）",
                plans[i].max_vreg, plans[i].param_count,
                plans[i].max_vreg - plans[i].param_count, plans[i].spill_count);
            DBG_PRINT("  帧：pushed=%d 字节，frame=%d 字节，%s 快照槽 %d 字节",
                      plans[i].pushed_bytes, plans[i].frame_bytes,
                      plans[i].needs_call ? "含" : "无", plans[i].save_bytes);
            fprintf(stderr, "[调试]   callee-saved 保存:");
            for (int k = 0; k < JIT_CALLEE_SAVED_COUNT; k++)
            {
                int reg = JIT_CALLEE_SAVED[k];
                if (plans[i].callee_saved[reg])
                {
                    fprintf(stderr, " %s", jit_reg_name(reg));
                }
            }
            fprintf(stderr, "\n");
            for (int k = 0; k < plans[i].var_count; k++)
            {
                DBG_PRINT("  变量 %s → v%d", plans[i].vars[k].name,
                          plans[i].vars[k].vreg);
            }
            if (plans[i].ncallees > 0)
            {
                fprintf(stderr, "[调试]   调用:");
                for (int k = 0; k < plans[i].ncallees; k++)
                {
                    int j = plans[i].callees[k];
                    fprintf(stderr, " [%d]%s", j, table[j]->name);
                }
                fprintf(stderr, "\n");
            }
            jit_dump_disasm("  机器码", jit.code + offset, size);
        }
#endif
    }

    for (int i = 0; i < count; i++)
    {
        if (!plans[i].jittable)
        {
            DBG_PRINT("JIT 函数[%d] %s → 解释执行", i, table[i]->name);
        }
    }
    DBG_PRINT("JIT 完成：总机器码 %lld 字节，字符串数据 %lld 字节",
              (long long)jit.code_pos, (long long)jit.data_pos);
}

/* ==================== 解释器 → JIT 分发 ==================== */

/**
 * 调用一个已 JIT 的函数（由 exec_call 调用）。
 * 把 Value 实参按 num 字段取出，按平台 ABI 调用机器码函数。
 *
 * @param f     函数记录（jittable 须为 1）
 * @param args  实参 Value 数组
 * @param nargs 实参个数
 * @return 函数返回值（int64）
 */
static int64_t jit_invoke(const Fn *f, const Value *args, int nargs)
{
    JitFn fn = (JitFn)f->jit_addr;
    int64_t a[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < nargs && i < 6; i++)
        a[i] = args[i].num;
    int64_t r = fn(a[0], a[1], a[2], a[3], a[4], a[5]);
#ifdef DEBUG
    fprintf(stderr, "[调试] JIT 调用 %s(", f->name);
    for (int i = 0; i < nargs; i++)
    {
        if (i)
            fprintf(stderr, ", ");
        fprintf(stderr, "%lld", (long long)a[i]);
    }
    fprintf(stderr, ") → %lld\n", (long long)r);
#endif
    return r;
}

#endif /* RUA_JIT_H */
