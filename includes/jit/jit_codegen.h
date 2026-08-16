/* easy/jit_codegen.h — JIT 代码生成：表达式/语句 → x86-64 机器码
 *
 * 依赖 jit_analyze.h（vreg 工具与 FnPlan）和
 * jit_base.h（打印辅助、字面量数据区）。 所有函数
 * `static`、指针传参；表达式统一"求值结果放进 dst vreg"。
 */

#ifndef RUA_JIT_CODEGEN_H
#define RUA_JIT_CODEGEN_H

#include "jit_analyze.h"

/* ==================== 二元运算 ==================== */

/**
 * 把二元运算结果写入 dst vreg。
 *
 * 用 RAX 作累加器（v0 从不作为操作数临时寄存器，故 RAX 空闲）；
 * 第二个操作数可直读寄存器或帧内内存（溢出时）。
 * `/` `%` 走 cqo+idiv，比较走 cmp+setcc。
 *
 * @param jit 编译状态
 * @param p   函数计划
 * @param op  运算符（TokenKind：TK_PLUS/TK_SLASH/...）
 * @param lt  左操作数 vreg
 * @param rt  右操作数 vreg
 * @param dst 结果 vreg
 */
static void jit_emit_binop(Jit* jit, const FnPlan* p, int op, int lt, int rt,
                           int dst)
{
    switch (op) {
    case TK_PLUS:
    case TK_MINUS:
    case TK_STAR: {
        jit_load_reg(jit, p, REG_RAX, lt);
        if (rt >= JIT_SPILL_VREG) {
            int off = jit_spill_off(p, rt);
            if (op == TK_PLUS)
                jit_emit_add_mem(jit, REG_RAX, off);
            else if (op == TK_MINUS)
                jit_emit_sub_mem(jit, REG_RAX, off);
            else
                jit_emit_imul_mem(jit, REG_RAX, off);
        } else {
            int r = jit_phys(rt);
            if (op == TK_PLUS)
                jit_emit_add(jit, REG_RAX, r);
            else if (op == TK_MINUS)
                jit_emit_sub(jit, REG_RAX, r);
            else
                jit_emit_imul(jit, REG_RAX, r);
        }
        jit_store_reg(jit, p, dst, REG_RAX);
        break;
    }
    case TK_SLASH:
    case TK_PERCENT: {
        /* idiv 会破坏 RAX/RDX：先快照 caller-saved（RDX 在其中），算完恢复 */
        jit_save_callersaved(jit, p);
        jit_load_reg(jit, p, REG_RAX, lt);
        jit_emit_cqo(jit);
        if (rt >= JIT_SPILL_VREG) {
            jit_emit_idiv_mem(jit, jit_spill_off(p, rt));
        } else {
            jit_emit_idiv_reg(jit, jit_phys(rt));
        }
        if (op == TK_PERCENT) jit_emit_mov(jit, REG_RAX, REG_RDX);
        jit_restore_callersaved(jit, p);
        jit_store_reg(jit, p, dst, REG_RAX);
        break;
    }
    case TK_EQEQ:
    case TK_NEQ:
    case TK_LT:
    case TK_LE:
    case TK_GT:
    case TK_GE: {
        int cc = op == TK_EQEQ ? JCC_E :
                 op == TK_NEQ  ? JCC_NE :
                 op == TK_LT   ? JCC_L :
                 op == TK_LE   ? JCC_LE :
                 op == TK_GT   ? JCC_G :
                                 JCC_GE;
        jit_load_reg(jit, p, REG_RAX, lt);
        if (rt >= JIT_SPILL_VREG) {
            jit_emit_cmp_mem(jit, REG_RAX, jit_spill_off(p, rt));
        } else {
            int r = jit_phys(rt);
            if (r != REG_RAX) jit_emit_cmp(jit, REG_RAX, r);
        }
        jit_emit_setcc_al(jit, cc);
        jit_emit_movzx_al(jit);
        jit_store_reg(jit, p, dst, REG_RAX);
        break;
    }
    default: error_at("JIT 不支持的运算符", 0); break;
    }
}

/* ==================== 函数调用 / 喵叫打印 ==================== */

/**
 * 发射一次调用：用户函数走 g_jit_table 间接调用；喵叫 走 jit_print/jit_itoa。
 *
 * 用户函数：快照 caller-saved → 装 ABI 参数寄存器（caller-saved 源从快照槽读）
 * → 表间接 call → 恢复 → 结果从 RAX 存入 dst。
 * 喵叫：先快照，逐实参打印（整数先 itoa 到
 * jit_scratch），非末参空格、末参换行。
 *
 * @param jit      编译状态
 * @param p        函数计划
 * @param n        N_CALL 节点
 * @param table    函数表
 * @param count    函数个数
 * @param arg_vreg 实参 vreg 数组（长度 = n->nargs）
 * @param dst      结果 vreg
 */
static void jit_emit_call(Jit* jit, const FnPlan* p, const Node* n, Fn** table,
                          int count, const int* arg_vreg, int dst)
{
    if (!strcmp(n->str, "喵叫")) {
        /* 打印：先快照
         * caller-saved（含实参临时值），实参从快照槽读取，避免被先前的打印调用破坏
         */
        jit_save_callersaved(jit, p);
        for (int i = 0; i < n->nargs; i++) {
            if (n->args[i]->kind == N_STR) {
                int64_t addr = jit_lit_addr(jit, n->args[i]->str);
                jit_emit_mov64(jit, REG_RDI, addr);
            } else {
                jit_load_arg_reg(jit, p, REG_RDI, arg_vreg[i]);
                jit_emit_mov64(jit, REG_RSI, (int64_t)jit_scratch);
                jit_emit_mov64(jit, REG_RAX, (int64_t)jit_itoa);
                jit_emit_call_reg(jit, REG_RAX);
                jit_emit_mov64(jit, REG_RDI, (int64_t)jit_scratch);
            }
            int64_t endl = (i == n->nargs - 1) ? (int64_t)JIT_ENDL_NL :
                                                 (int64_t)JIT_ENDL_SP;
            jit_emit_mov64(jit, REG_RSI, endl);
            jit_emit_mov64(jit, REG_RAX, (int64_t)jit_print);
            jit_emit_call_reg(jit, REG_RAX);
            jit_restore_callersaved(jit, p);
        }
        jit_emit_xor_eax(jit);
        jit_store_reg(jit, p, dst, REG_RAX);
        return;
    }

    int fidx = -1;
    for (int i = 0; i < count; i++) {
        if (!strcmp(table[i]->name, n->str)) {
            fidx = i;
            break;
        }
    }
    if (fidx < 0 || n->nargs > JIT_ARG_COUNT)
        error_at("JIT 调用目标无效", n->line);

    jit_save_callersaved(jit, p);
    for (int i = 0; i < n->nargs; i++) {
        jit_load_arg_reg(jit, p, JIT_ARG_REGS[i], arg_vreg[i]);
    }
    jit_emit_mov64(jit, REG_RAX, jit->table_addr);
    jit_emit_load_rax_idx(jit, fidx * 8);
    jit_emit_call_reg(jit, REG_RAX);
    jit_restore_callersaved(jit, p);
    jit_store_reg(jit, p, dst, REG_RAX);
}

/* ==================== 表达式 ==================== */

/**
 * 求值表达式到 dst vreg（统一入口）。
 *
 * 节点映射：
 *   N_NUM     → mov dst, imm；
 *   N_IDENT   → mov dst, 变量vreg（查变量表）；
 *   N_BINARY  → 两个操作数各占一个临时 vreg，再交给 jit_emit_binop；
 *   N_CALL    → 实参各占一个临时 vreg，字符串字面量直接嵌地址，再交给
 * jit_emit_call； N_STR     → 仅作打印实参，由 N_CALL 分支处理。
 *
 * @param jit        编译状态
 * @param p          函数计划
 * @param n          表达式节点
 * @param table      函数表
 * @param count      函数个数
 * @param self_index 当前函数下标（暂未使用，保留接口一致）
 * @param next       临时/局部 vreg 计数器（与 count/build 同序推进）
 * @param dst        结果 vreg
 */
static void jit_compile_expr(Jit* jit, const FnPlan* p, const Node* n,
                             Fn** table, int count, int self_index, int* next,
                             int dst)
{
    switch (n->kind) {
    case N_NUM: {
        int64_t v = n->num;
        int dstreg = jit_phys(dst) < 0 ? REG_RAX : jit_phys(dst);
        if ((int64_t)(int32_t)v == v) {
            jit_emit_mov32(jit, dstreg, (int32_t)v);
        } else {
            jit_emit_mov64(jit, dstreg, v);
        }
        if (jit_phys(dst) < 0)
            jit_emit_store_rbp(jit, jit_spill_off(p, dst), dstreg);
        break;
    }
    case N_IDENT: {
        int vreg = jit_var_find(p, n->str);
        if (vreg < 0) error_at("JIT 变量解析失败", n->line);
        jit_load_reg(jit, p, jit_phys(dst) < 0 ? REG_RAX : jit_phys(dst), vreg);
        if (jit_phys(dst) < 0)
            jit_emit_store_rbp(jit, jit_spill_off(p, dst), REG_RAX);
        break;
    }
    case N_BINARY: {
        int lt = *next + p->param_count + 1;
        *next += 1;
        int rt = *next + p->param_count + 1;
        *next += 1;
        jit_compile_expr(jit, p, n->left, table, count, self_index, next, lt);
        jit_compile_expr(jit, p, n->right, table, count, self_index, next, rt);
        jit_emit_binop(jit, p, n->op, lt, rt, dst);
        break;
    }
    case N_CALL: {
        if (n->nargs > JIT_ARG_COUNT) error_at("JIT 实参过多", n->line);
        int arg_vreg[6];
        for (int i = 0; i < n->nargs; i++) {
            arg_vreg[i] = *next + p->param_count + 1;
            *next += 1;
            if (n->args[i]->kind == N_STR) {
                int64_t addr = jit_lit_addr(jit, n->args[i]->str);
                int dstreg = jit_phys(arg_vreg[i]) < 0 ? REG_RAX :
                                                         jit_phys(arg_vreg[i]);
                jit_emit_mov64(jit, dstreg, addr);
                if (jit_phys(arg_vreg[i]) < 0) {
                    jit_emit_store_rbp(jit, jit_spill_off(p, arg_vreg[i]),
                                       dstreg);
                }
            } else {
                jit_compile_expr(jit, p, n->args[i], table, count, self_index,
                                 next, arg_vreg[i]);
            }
        }
        jit_emit_call(jit, p, n, table, count, arg_vreg, dst);
        break;
    }
    case N_STR:
        /* 字符串只作为打印实参出现（由 N_CALL 处理） */
        break;
    default: error_at("JIT 不支持的表达式节点", n->line); break;
    }
}

/* ==================== 语句 ==================== */

/**
 * 语句代码生成。
 *
 * 节点映射：
 *   N_VAR/N_ASSIGN → 初始化表达式直接求值进目标变量 vreg；
 *   N_RETURN       → 求值到临时 vreg → RAX → 跳尾声；
 *   N_IF           → 条件求值 → test → je else → then → jmp done → else →
 * done； N_WHILE        → loop: 条件 → test → je done → body → jmp loop →
 * done； N_BLOCK/N_PROG → 逐语句生成； 其它           →
 * 按表达式语句求值后丢弃。
 *
 * @param jit        编译状态
 * @param p          函数计划
 * @param n          语句节点
 * @param table      函数表
 * @param count      函数个数
 * @param self_index 当前函数下标
 * @param next       临时/局部 vreg 计数器
 * @param epilogue   尾声标签 id（N_RETURN 跳到此处）
 */
static void jit_compile_stmt(Jit* jit, const FnPlan* p, const Node* n,
                             Fn** table, int count, int self_index, int* next,
                             int64_t epilogue)
{
    switch (n->kind) {
    case N_VAR: {
        int vreg = jit_var_find(p, n->str);
        if (vreg < 0) error_at("JIT 变量分配失败", n->line);
        *next += 1;
        jit_compile_expr(jit, p, n->right, table, count, self_index, next,
                         vreg);
        break;
    }
    case N_ASSIGN: {
        int vreg = jit_var_find(p, n->left->str);
        if (vreg < 0) error_at("JIT 赋值目标未找到", n->line);
        *next += 1;
        jit_compile_expr(jit, p, n->right, table, count, self_index, next,
                         vreg);
        break;
    }
    case N_RETURN: {
        int t = *next + p->param_count + 1;
        *next += 1;
        if (n->left) {
            jit_compile_expr(jit, p, n->left, table, count, self_index, next,
                             t);
            jit_load_reg(jit, p, REG_RAX, t);
        }
        jit_emit_jmp(jit, epilogue);
        break;
    }
    case N_IF: {
        int c = *next + p->param_count + 1;
        *next += 1;
        jit_compile_expr(jit, p, n->left, table, count, self_index, next, c);
        jit_load_reg(jit, p, REG_RAX, c);
        jit_emit_test(jit, REG_RAX);
        int64_t else_lab = jit_new_label(jit);
        int64_t done_lab = jit_new_label(jit);
        jit_emit_jcc(jit, else_lab, JCC_E);
        jit_compile_stmt(jit, p, n->body, table, count, self_index, next,
                         epilogue);
        jit_emit_jmp(jit, done_lab);
        jit_bind_label(jit, else_lab);
        if (n->elseB) {
            jit_compile_stmt(jit, p, n->elseB, table, count, self_index, next,
                             epilogue);
        }
        jit_bind_label(jit, done_lab);
        break;
    }
    case N_WHILE: {
        int64_t loop_lab = jit_new_label(jit);
        int64_t done_lab = jit_new_label(jit);
        jit_bind_label(jit, loop_lab);
        int c = *next + p->param_count + 1;
        *next += 1;
        jit_compile_expr(jit, p, n->left, table, count, self_index, next, c);
        jit_load_reg(jit, p, REG_RAX, c);
        jit_emit_test(jit, REG_RAX);
        jit_emit_jcc(jit, done_lab, JCC_E);
        jit_compile_stmt(jit, p, n->body, table, count, self_index, next,
                         epilogue);
        jit_emit_jmp(jit, loop_lab);
        jit_bind_label(jit, done_lab);
        break;
    }
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            jit_compile_stmt(jit, p, n->stmts[i], table, count, self_index,
                             next, epilogue);
        }
        break;
    default: {
        int t = *next + p->param_count + 1;
        *next += 1;
        jit_compile_expr(jit, p, n, table, count, self_index, next, t);
        break;
    }
    }
}

/* ==================== 函数整体 ==================== */

/**
 * 编译单个函数，返回机器码在代码缓冲中的偏移。
 *
 * 序言：push rbp → mov rbp,rsp → push callee-saved → sub rsp,frame → 清 RAX
 *       → 形参从 ABI 寄存器拷到各自 vreg；
 * 函数体：jit_compile_stmt 生成语句；
 * 尾声：加回 frame → 逆序 pop callee-saved → pop rbp → ret。
 *
 * @param jit        编译状态
 * @param p          函数计划
 * @param fn         函数记录
 * @param table      函数表
 * @param count      函数个数
 * @param self_index 当前函数下标
 * @return 机器码入口相对代码缓冲基址的偏移
 */
static int64_t jit_emit_function(Jit* jit, const FnPlan* p, const Fn* fn,
                                 Fn** table, int count, int self_index)
{
    jit->patch_count = 0;
    jit->label_count = 0;

    int64_t func_start = (int64_t)jit->code_pos;

    /* 序言 */
    jit_emit_push(jit, REG_RBP);
    jit_emit_mov(jit, REG_RBP, REG_RSP);
    for (int k = 0; k < JIT_CALLEE_SAVED_COUNT; k++) {
        if (p->callee_saved[JIT_CALLEE_SAVED[k]]) {
            jit_emit_push(jit, JIT_CALLEE_SAVED[k]);
        }
    }
    if (p->frame_bytes > 0) jit_emit_sub_rsp(jit, p->frame_bytes);
    jit_emit_xor_eax(jit);

    /* 参数拷贝：ABI 参数寄存器 → 形参 vreg */
    for (int i = 0; i < fn->nparams; i++) {
        int phys = jit_phys(i + 1);
        if (phys != JIT_ARG_REGS[i]) jit_emit_mov(jit, phys, JIT_ARG_REGS[i]);
    }

    /* 函数体 */
    int next = 0;
    int64_t epilogue = jit_new_label(jit);
    jit_compile_stmt(jit, p, fn->body, table, count, self_index, &next,
                     epilogue);

    /* 尾声 */
    jit_bind_label(jit, epilogue);
    if (p->frame_bytes > 0) jit_emit_add_rsp(jit, p->frame_bytes);
    for (int k = JIT_CALLEE_SAVED_COUNT - 1; k >= 0; k--) {
        if (p->callee_saved[JIT_CALLEE_SAVED[k]]) {
            jit_emit_pop(jit, JIT_CALLEE_SAVED[k]);
        }
    }
    jit_emit_pop(jit, REG_RBP);
    jit_emit_ret(jit);

    return func_start;
}

#endif /* RUA_JIT_CODEGEN_H */
