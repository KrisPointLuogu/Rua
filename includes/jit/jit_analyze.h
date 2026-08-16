/* easy/jit_analyze.h — JIT 分析：vreg 工具、变量表、可 JIT 判定与帧布局
 *
 * 在代码生成前，对函数做一次静态分析：
 *   - 变量 → vreg 映射（形参 + 局部变量）；
 *   - 表达式临时 vreg 计数（与代码生成同序，保证两者一致）；
 *   - 可 JIT 判定（节点白名单、引用检查、闭包传递由调用方完成）；
 *   - 帧布局（callee-saved 保存、溢出槽、caller-saved 快照槽、对齐）。
 */

#ifndef RUA_JIT_ANALYZE_H
#define RUA_JIT_ANALYZE_H

#include "../ast.h"
#include "../runtime.h"
#include "jit_emit.h"

/* ==================== 变量表与函数计划 ==================== */

/**
 * 变量 → vreg 表项。
 * @param name 变量名
 * @param vreg 分配的 vreg 号
 */
typedef struct {
    const char* name;
    int vreg;
} JitVar;

/**
 * 单个函数的编译计划（由 jit_plan_function 产出，代码生成依据它）。
 *
 * @param jittable     该函数是否可 JIT
 * @param max_vreg     用到的最大 vreg 号
 * @param spill_count  溢出槽个数（v14+）
 * @param needs_call   是否含调用/打印/除模（决定是否分配 caller-saved 快照槽）
 * @param callee_saved 需要 push 的 callee-saved 物理寄存器（按下标标记）
 * @param npushed_callee 需要 push 的 callee-saved 个数
 * @param pushed_bytes 序言压栈字节（8 旧rbp + 8×npushed_callee）
 * @param spill_bytes  溢出槽总字节
 * @param save_bytes   caller-saved 快照槽总字节
 * @param frame_bytes  对齐后的帧大小（sub rsp 的量）
 * @param save_off     物理寄存器 → rbp 偏移（未保存为 0）
 * @param vars / var_count / var_cap 变量表
 * @param param_count  形参个数
 * @param callees / ncallees 调用的函数下标（闭包传递用）
 */
typedef struct {
    int jittable;
    const char* reject_reason; /* 不可 JIT 的具体原因（jittable==0 时有效） */
    int max_vreg;
    int spill_count;
    int needs_call;
    int callee_saved[16];
    int npushed_callee;
    int pushed_bytes;
    int spill_bytes;
    int save_bytes;
    int frame_bytes;
    int save_off[16];
    JitVar* vars;
    int var_count, var_cap;
    int param_count;
    int callees[64];
    int ncallees;
} FnPlan;

/* ==================== vreg 工具（配合 FnPlan） ==================== */

/**
 * vreg → 物理寄存器号；溢出（≥ JIT_SPILL_VREG）返回 -1。
 * @param vreg 虚拟寄存器号
 * @return 物理寄存器号，溢出返回 -1
 */
static int jit_phys(int vreg)
{ return vreg < JIT_SPILL_VREG ? JIT_VREG_TO_PHYS[vreg] : -1; }

/**
 * 溢出槽相对 rbp 的偏移（vreg ≥ JIT_SPILL_VREG 时有效）。
 * @param p    函数计划
 * @param vreg 溢出的 vreg 号
 * @return 相对 rbp 的负偏移
 */
static int jit_spill_off(const FnPlan* p, int vreg)
{ return -(p->pushed_bytes + (vreg - JIT_SPILL_VREG) * 8); }

/**
 * 把 vreg 的值加载到寄存器 dst。
 * vreg 溢出则从栈槽读；寄存器 vreg 直接从其物理寄存器取（目标相同则跳过）。
 *
 * @param jit  编译状态
 * @param p    函数计划
 * @param dst  目标寄存器
 * @param vreg 源 vreg
 */
static void jit_load_reg(Jit* jit, const FnPlan* p, int dst, int vreg)
{
    if (vreg >= JIT_SPILL_VREG) {
        jit_emit_load_rbp(jit, dst, jit_spill_off(p, vreg));
    } else {
        int phys = jit_phys(vreg);
        if (phys != dst) jit_emit_mov(jit, dst, phys);
    }
}

/**
 * 把寄存器 src 的值存入 vreg。
 * @param jit  编译状态
 * @param p    函数计划
 * @param vreg 目标 vreg
 * @param src  源寄存器
 */
static void jit_store_reg(Jit* jit, const FnPlan* p, int vreg, int src)
{
    if (vreg >= JIT_SPILL_VREG) {
        jit_emit_store_rbp(jit, jit_spill_off(p, vreg), src);
    } else {
        int phys = jit_phys(vreg);
        if (phys != src) jit_emit_mov(jit, phys, src);
    }
}

/**
 * 调用参数装载：把 vreg 的值装入 ABI 参数寄存器。
 * caller-saved 源一律从快照槽读，避免装载顺序互相覆盖（也用于打印实参）。
 *
 * @param jit  编译状态
 * @param p    函数计划
 * @param dst  目标参数寄存器
 * @param vreg 源 vreg
 */
static void jit_load_arg_reg(Jit* jit, const FnPlan* p, int dst, int vreg)
{
    if (vreg >= JIT_SPILL_VREG) {
        jit_emit_load_rbp(jit, dst, jit_spill_off(p, vreg));
    } else {
        int phys = jit_phys(vreg);
        if (p->save_off[phys] != 0) {
            jit_emit_load_rbp(jit, dst, p->save_off[phys]);
        } else if (phys != dst) {
            jit_emit_mov(jit, dst, phys);
        }
    }
}

/**
 * 把全部 caller-saved 寄存器存入帧内快照槽（跨调用保存）。
 * @param jit 编译状态
 * @param p   函数计划
 */
static void jit_save_callersaved(Jit* jit, const FnPlan* p)
{
    for (int i = 0; i < JIT_SAVE_COUNT; i++) {
        int reg = JIT_SAVE_REGS[i];
        jit_emit_store_rbp(jit, p->save_off[reg], reg);
    }
}

/**
 * 从帧内快照槽恢复全部 caller-saved 寄存器。
 * @param jit 编译状态
 * @param p   函数计划
 */
static void jit_restore_callersaved(Jit* jit, const FnPlan* p)
{
    for (int i = JIT_SAVE_COUNT - 1; i >= 0; i--) {
        int reg = JIT_SAVE_REGS[i];
        jit_emit_load_rbp(jit, reg, p->save_off[reg]);
    }
}

/* ==================== 分析：varmap / 计数 / 可 JIT 判定 ====================
 */

/**
 * 在变量表中按名查找 vreg。
 * @param p    函数计划
 * @param name 变量名
 * @return vreg；未找到返回 -1
 */
static int jit_var_find(const FnPlan* p, const char* name)
{
    for (int i = 0; i < p->var_count; i++) {
        if (!strcmp(p->vars[i].name, name)) return p->vars[i].vreg;
    }
    return -1;
}

/**
 * 往变量表追加一项（满时扩容）。
 * @param p    函数计划
 * @param name 变量名
 * @param vreg 分配的 vreg
 */
static void jit_var_add(FnPlan* p, const char* name, int vreg)
{
    if (p->var_count >= p->var_cap) {
        p->var_cap = p->var_cap ? p->var_cap * 2 : 16;
        p->vars = (JitVar*)realloc(p->vars, p->var_cap * sizeof(JitVar));
    }
    p->vars[p->var_count].name = name;
    p->vars[p->var_count].vreg = vreg;
    p->var_count++;
}

/* 计数遍历：与代码生成同序，得到临时/局部 vreg 数量与可 JIT 判定 */
static void jit_count_expr(const Node* n, int* next, int* ok);
static void jit_count_stmt(const Node* n, int* next, int* ok);

/**
 * 表达式计数：推进 next，并对不支持的类型置 ok=0。
 *
 * @param n    表达式节点
 * @param next 计数器（与代码生成的 vreg 分配同序）
 * @param ok   合法性标志（被置 0 表示不可 JIT）
 */
static void jit_count_expr(const Node* n, int* next, int* ok)
{
    if (!*ok) return;
    switch (n->kind) {
    case N_NUM:
    case N_IDENT: break;
    case N_STR:
        /* 字符串只允许作为喵叫实参（N_CALL 分支特判），其余场合不可 JIT */
        *ok = 0;
        break;
    case N_BINARY:
        *next += 2;
        jit_count_expr(n->left, next, ok);
        jit_count_expr(n->right, next, ok);
        break;
    case N_CALL:
        *next += n->nargs;
        for (int i = 0; i < n->nargs; i++) {
            if (n->args[i]->kind == N_STR && !strcmp(n->str, "喵叫")) continue;
            jit_count_expr(n->args[i], next, ok);
            if (!*ok) return;
        }
        break;
    default: *ok = 0; break;
    }
}

/**
 * 语句计数：推进 next，检查可 JIT 的语句类型。
 *
 * @param n    语句节点
 * @param next 计数器
 * @param ok   合法性标志
 */
static void jit_count_stmt(const Node* n, int* next, int* ok)
{
    if (!*ok) return;
    switch (n->kind) {
    case N_VAR:
        *next += 1;
        jit_count_expr(n->right, next, ok);
        break;
    case N_ASSIGN:
        if (n->left->kind != N_IDENT) {
            *ok = 0;
            return;
        }
        *next += 1;
        jit_count_expr(n->right, next, ok);
        break;
    case N_RETURN:
        *next += 1;
        if (n->left) jit_count_expr(n->left, next, ok);
        break;
    case N_IF:
        *next += 1;
        jit_count_expr(n->left, next, ok);
        jit_count_stmt(n->body, next, ok);
        if (n->elseB) jit_count_stmt(n->elseB, next, ok);
        break;
    case N_WHILE:
        *next += 1;
        jit_count_expr(n->left, next, ok);
        jit_count_stmt(n->body, next, ok);
        break;
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            jit_count_stmt(n->stmts[i], next, ok);
            if (!*ok) return;
        }
        break;
    default:
        *next += 1;
        jit_count_expr(n, next, ok);
        break;
    }
}

/**
 * 登记局部变量名 → vreg（与 count/codegen 相同的遍历顺序与计数器推进）。
 *
 * @param n    语句节点
 * @param next 计数器（与 jit_count_* 同规则推进）
 * @param p    函数计划（变量写入 p->vars）
 */
static void jit_build_vars(const Node* n, int* next, FnPlan* p)
{
    switch (n->kind) {
    case N_VAR:
        jit_var_add(p, n->str, p->param_count + 1 + *next);
        *next += 1;
        {
            int ok = 1;
            jit_count_expr(n->right, next, &ok);
        }
        break;
    case N_ASSIGN:
        *next += 1;
        {
            int ok = 1;
            jit_count_expr(n->right, next, &ok);
        }
        break;
    case N_RETURN:
        *next += 1;
        if (n->left) {
            int ok = 1;
            jit_count_expr(n->left, next, &ok);
        }
        break;
    case N_IF:
        *next += 1;
        {
            int ok = 1;
            jit_count_expr(n->left, next, &ok);
        }
        jit_build_vars(n->body, next, p);
        if (n->elseB) jit_build_vars(n->elseB, next, p);
        break;
    case N_WHILE:
        *next += 1;
        {
            int ok = 1;
            jit_count_expr(n->left, next, &ok);
        }
        jit_build_vars(n->body, next, p);
        break;
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            jit_build_vars(n->stmts[i], next, p);
        }
        break;
    default:
        *next += 1;
        {
            int ok = 1;
            jit_count_expr(n, next, &ok);
        }
        break;
    }
}

/**
 * 扫描函数是否含调用/打印/除模（决定是否分配 caller-saved 快照槽）。
 * @param n     节点
 * @param needs 输出：发现调用/打印/除模时置 1
 */
static void jit_scan_side_effects(const Node* n, int* needs)
{
    switch (n->kind) {
    case N_CALL: *needs = 1; break;
    case N_BINARY:
        if (n->op == TK_SLASH || n->op == TK_PERCENT) *needs = 1;
        jit_scan_side_effects(n->left, needs);
        jit_scan_side_effects(n->right, needs);
        break;
    case N_VAR:
    case N_ASSIGN:
        jit_scan_side_effects(n->right, needs);
        if (n->kind == N_ASSIGN) jit_scan_side_effects(n->left, needs);
        break;
    case N_RETURN:
        if (n->left) jit_scan_side_effects(n->left, needs);
        break;
    case N_IF:
        jit_scan_side_effects(n->left, needs);
        jit_scan_side_effects(n->body, needs);
        if (n->elseB) jit_scan_side_effects(n->elseB, needs);
        break;
    case N_WHILE:
        jit_scan_side_effects(n->left, needs);
        jit_scan_side_effects(n->body, needs);
        break;
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            jit_scan_side_effects(n->stmts[i], needs);
        }
        break;
    default: break;
    }
}

/**
 * 统计被调用的函数下标（非喵叫），供闭包传递判定使用。
 * @param n     节点
 * @param table 函数表
 * @param count 函数个数
 * @param p     函数计划（写入 p->callees）
 */
static void jit_scan_callees(const Node* n, Fn** table, int count, FnPlan* p)
{
    switch (n->kind) {
    case N_CALL:
        if (strcmp(n->str, "喵叫")) {
            for (int i = 0; i < count; i++) {
                if (!strcmp(table[i]->name, n->str)) {
                    p->callees[p->ncallees++] = i;
                    break;
                }
            }
        }
        for (int i = 0; i < n->nargs; i++) {
            jit_scan_callees(n->args[i], table, count, p);
        }
        break;
    case N_BINARY:
        jit_scan_callees(n->left, table, count, p);
        jit_scan_callees(n->right, table, count, p);
        break;
    case N_VAR:
    case N_ASSIGN:
        jit_scan_callees(n->right, table, count, p);
        if (n->kind == N_ASSIGN) jit_scan_callees(n->left, table, count, p);
        break;
    case N_RETURN:
        if (n->left) jit_scan_callees(n->left, table, count, p);
        break;
    case N_IF:
        jit_scan_callees(n->left, table, count, p);
        jit_scan_callees(n->body, table, count, p);
        if (n->elseB) jit_scan_callees(n->elseB, table, count, p);
        break;
    case N_WHILE:
        jit_scan_callees(n->left, table, count, p);
        jit_scan_callees(n->body, table, count, p);
        break;
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            jit_scan_callees(n->stmts[i], table, count, p);
        }
        break;
    default: break;
    }
}

/**
 * 引用检查：函数体内所有 N_IDENT 都必须能在变量表找到（形参/局部变量）。
 * 引用全局变量等未声明标识符 → 返回 0（不可 JIT）。
 *
 * @param n 节点
 * @param p 函数计划（变量表须已构建）
 * @return 1 全部可解析，0 有非局部引用
 */
static int jit_check_idents(const Node* n, FnPlan* p)
{
    switch (n->kind) {
    case N_IDENT: return jit_var_find(p, n->str) >= 0;
    case N_NUM:
    case N_STR: return 1;
    case N_BINARY:
        return jit_check_idents(n->left, p) && jit_check_idents(n->right, p);
    case N_CALL:
        for (int i = 0; i < n->nargs; i++) {
            if (!jit_check_idents(n->args[i], p)) return 0;
        }
        return 1;
    case N_VAR: return jit_check_idents(n->right, p);
    case N_ASSIGN:
        return jit_check_idents(n->left, p) && jit_check_idents(n->right, p);
    case N_RETURN: return !n->left || jit_check_idents(n->left, p);
    case N_IF:
        return jit_check_idents(n->left, p) && jit_check_idents(n->body, p)
               && (!n->elseB || jit_check_idents(n->elseB, p));
    case N_WHILE:
        return jit_check_idents(n->left, p) && jit_check_idents(n->body, p);
    case N_BLOCK:
    case N_PROG:
        for (int i = 0; i < n->nstmts; i++) {
            if (!jit_check_idents(n->stmts[i], p)) return 0;
        }
        return 1;
    default: return 0;
    }
}

/**
 * 规划一个函数：可 JIT 判定 + 变量表 + 帧布局计算。
 *
 * @param fn    函数记录
 * @param table 函数表
 * @param count 函数个数
 * @param p     输出计划（调用前无需初始化，内部 memset）
 */
static void jit_plan_function(const Fn* fn, Fn** table, int count, FnPlan* p)
{
    memset(p, 0, sizeof(FnPlan));
    p->param_count = fn->nparams;
    p->jittable = 1;

    if (fn->nparams > JIT_MAX_PARAMS) {
        p->jittable = 0;
        p->reject_reason = "参数个数超过平台上限";
        return;
    }

    /* 预置形参 vreg（v1..vP） */
    for (int i = 0; i < fn->nparams; i++) {
        jit_var_add(p, fn->params[i], i + 1);
    }

    /* 登记局部变量（与代码生成同序推进计数器） */
    int next = 0;
    jit_build_vars(fn->body, &next, p);

    /* 计数 + 合法性扫描 */
    int count_next = 0, ok = 1;
    jit_count_stmt(fn->body, &count_next, &ok);
    if (!ok) {
        p->jittable = 0;
        p->reject_reason = "含不支持的节点类型或字符串值";
        return;
    }
    int max_vreg = count_next > 0 ? fn->nparams + count_next : fn->nparams;
    p->max_vreg = max_vreg;

    /* 引用检查：所有 N_IDENT 必须是形参/局部变量 */
    if (!jit_check_idents(fn->body, p)) {
        p->jittable = 0;
        p->reject_reason = "引用了非局部变量（全局变量）";
        return;
    }

    /* 需要的 vreg 对应的 callee-saved 物理寄存器 */
    for (int v = 1; v <= max_vreg; v++) {
        int phys = jit_phys(v);
        for (int k = 0; k < JIT_CALLEE_SAVED_COUNT; k++) {
            if (phys == JIT_CALLEE_SAVED[k]) p->callee_saved[phys] = 1;
        }
    }
    p->npushed_callee = 0;
    for (int k = 0; k < JIT_CALLEE_SAVED_COUNT; k++) {
        if (p->callee_saved[JIT_CALLEE_SAVED[k]]) p->npushed_callee++;
    }
    p->pushed_bytes = 8 + p->npushed_callee * 8;
    p->spill_count
        = max_vreg >= JIT_SPILL_VREG ? max_vreg - (JIT_SPILL_VREG - 1) : 0;
    p->spill_bytes = p->spill_count * 8;

    /* caller-saved 保存槽 */
    jit_scan_side_effects(fn->body, &p->needs_call);
    if (p->needs_call) {
        p->save_bytes = JIT_SAVE_COUNT * 8;
        for (int i = 0; i < JIT_SAVE_COUNT; i++) {
            p->save_off[JIT_SAVE_REGS[i]]
                = -(p->pushed_bytes + p->spill_bytes + i * 8);
        }
    }
    p->frame_bytes = p->spill_bytes + p->save_bytes + JIT_SHADOW;
    /* SysV：入口 RSP ≡ 8(mod16)，内部 CALL 前需 ≡0 → (pushed+frame) ≡ 8(mod16)
     */
    while ((p->pushed_bytes + p->frame_bytes) % 16 != 8) p->frame_bytes += 8;

    jit_scan_callees(fn->body, table, count, p);
}

#endif /* RUA_JIT_ANALYZE_H */
