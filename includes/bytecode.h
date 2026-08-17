/* includes/bytecode.h — 静态 AST → 字节码文本（仅 DEBUG）
 *
 * 功能：把编译好的 AST 走一遍静态遍历，翻译成类似 Python dis /
 * Java javap -c 的助记符指令流，输出到 stderr。
 *
 * 设计要点：
 *   - 静态翻译：只按 AST 结构生成指令，不执行、不求值、不做常量折叠；
 *   - 指令号（pc）全局连续递增，注释分隔行不占号；
 *   - 常量表/符号表去重，指令用索引引用；
 *   - 控制流（if/while）生成伪 label（L<指令号>）与跳转指令，
 *     跳转目标在递归返回时静态算出；
 *   - 主程序语句在前，随后是 fn_table 里每个函数展开（MAKE_FUNCTION +
 *     函数体），段首有 "; --- 函数 xxx ---" 注释分隔。
 *
 * 指令集参考（栈式，操作数在括号里）：
 *   LOAD_CONST  <idx>    把常量表第 idx 项压栈（数字或字符串）
 *   LOAD_NAME   <idx>    把符号表第 idx 个变量的值压栈
 *   STORE_NAME  <idx>    弹栈顶，存入符号表第 idx 个变量
 *   NEW_ARRAY   <len>    弹栈顶作广播初始值，建长度 len 的数组压栈
 *   LOAD_INDEX           弹下标、弹基址，把 a[i] 压栈
 *   STORE_INDEX          弹新值、弹下标、弹基址，写 a[i]
 *   ADD/SUB/.../LE       弹右、弹左，运算结果压栈（二元运算助记符）
 *   CALL_FN     <name_idx> <nargs>  弹 nargs 个实参，调用用户函数，压回结果
 *   CALL_BUILTIN <name_idx> <nargs> 同上，但调用内置函数（喵叫）
 *   MAKE_FUNCTION <name_idx> <nparams>  声明一个函数定义（不执行 body）
 *   RETURN_VALUE         弹栈顶作返回值，结束当前函数
 *   JUMP        L<pc>    无条件跳转到指令号 pc
 *   JUMP_IF_FALSE L<pc>  弹栈顶条件，为假则跳转（真则顺序执行）
 *   NOP                  占位（当前未使用）
 *
 * 只有在编译时定义 DEBUG 宏才生效；否则 debug_dump_bytecode 为空实现。
 */

#ifndef RUA_BYTECODE_H
#define RUA_BYTECODE_H

#include <stdio.h>
#include <string.h>

#include "runtime.h"

#ifdef DEBUG

/* 容量上限：指令条数与表项数，超出则静默截断（调试用途足够大） */
#define BC_MAX_INS 4096
#define BC_MAX_TBL 256

/**
 * 一条字节码指令。
 *
 * 指令与注释共用此结构：注释行 is_comment=1，不产生指令号，
 * 只作输出里的分隔说明（如 "; --- 函数 fib ---"）。
 *
 * @param is_comment 1 = 注释分隔行，0 = 真实指令
 * @param pc         指令号（真实指令连续递增；注释沿用上一指令号）
 * @param text       助记符文本（含操作数），注释行则存注释内容
 * @param line       对应源码行号（用于输出 "; 行N"）
 * @param is_jump    1 = 跳转指令（目标后填），0 = 普通指令
 * @param jkind      跳转类型：0 = JUMP，1 = JUMP_IF_FALSE
 * @param target     跳转目标指令号（is_jump 时有效）
 */
typedef struct
{
    int is_comment;
    int pc;
    char text[64];
    int line;
    int is_jump;
    int jkind;
    int target;
} BcIns;

/**
 * 字节码生成上下文。
 *
 * 一边遍历 AST 一边累积三张表与指令流；所有数组定长，
 * 超限时对应追加函数静默忽略（不报错，DEBUG 工具定位足够）。
 *
 * @param ins/nins        指令流（含注释行）与其条数
 * @param pc              当前指令号（真实指令才递增）
 * @param consts_*        常量表：is_str 区分字符串/数字，去重
 * @param nconsts         常量表条数
 * @param names/nnames    符号表：变量名/函数名，去重（指针借用 AST 字符串）
 * @param label_at        被引用为跳转目标的指令号，打印时插 "L<pc>:" 行
 */
typedef struct
{
    BcIns ins[BC_MAX_INS];
    int nins;
    int pc;
    int consts_is_str[BC_MAX_TBL];
    long long consts_num[BC_MAX_TBL];
    const char *consts_str[BC_MAX_TBL];
    int nconsts;
    const char *names[BC_MAX_TBL];
    int nnames;
    int label_at[BC_MAX_INS];
} BcCtx;

/**
 * 追加一条普通指令。
 *
 * @param c    生成上下文
 * @param text 助记符文本（含操作数），如 "LOAD_CONST 0"
 * @param line 对应源码行号
 */
static void bc_ins(BcCtx *c, const char *text, int line)
{
    if (c->nins >= BC_MAX_INS)
        return;
    BcIns *in = &c->ins[c->nins++];
    in->is_comment = 0;
    in->pc = c->pc++;
    snprintf(in->text, sizeof(in->text), "%s", text);
    in->line = line;
    in->is_jump = 0;
    in->jkind = 0;
    in->target = -1;
}

/**
 * 追加一条跳转指令（目标号后填）。
 *
 * @param c    生成上下文
 * @param kind 跳转类型：0 = JUMP，1 = JUMP_IF_FALSE
 * @param line 对应源码行号
 */
static void bc_jump(BcCtx *c, int kind, int line)
{
    if (c->nins >= BC_MAX_INS)
        return;
    BcIns *in = &c->ins[c->nins++];
    in->is_comment = 0;
    in->pc = c->pc++;
    in->text[0] = 0;
    in->line = line;
    in->is_jump = 1;
    in->jkind = kind;
    in->target = -1;
}

/**
 * 追加一条注释分隔行（不占指令号）。
 *
 * @param c    生成上下文
 * @param text 注释内容（如 "; --- 函数 fib ---"）
 */
static void bc_comment(BcCtx *c, const char *text)
{
    if (c->nins >= BC_MAX_INS)
        return;
    BcIns *in = &c->ins[c->nins++];
    in->is_comment = 1;
    in->pc = c->pc;
    snprintf(in->text, sizeof(in->text), "%s", text);
    in->line = 0;
    in->is_jump = 0;
    in->jkind = 0;
    in->target = -1;
}

/**
 * 查找数字常量（去重）。
 *
 * 遍历常量表找相同数值；找不到就追加一项并返回其索引。
 * 表满时返回 0（不报错，调试工具定位足够）。
 *
 * @param c 生成上下文
 * @param n 数字字面量
 * @return 该数字在常量表中的索引
 */
static int bc_const_num(BcCtx *c, long long n)
{
    for (int i = 0; i < c->nconsts; i++)
        if (!c->consts_is_str[i] && c->consts_num[i] == n)
            return i;
    int i = c->nconsts;
    if (i >= BC_MAX_TBL)
        return 0;
    c->consts_is_str[i] = 0;
    c->consts_num[i] = n;
    c->consts_str[i] = NULL;
    c->nconsts++;
    return i;
}

/**
 * 查找字符串常量（去重）。
 *
 * 遍历常量表比较字符串内容；找不到就追加一项并返回其索引。
 * 与 bc_const_num 共用同一张表，用 is_str 区分。
 *
 * @param c 生成上下文
 * @param s 字符串字面量内容
 * @return 该字符串在常量表中的索引
 */
static int bc_const_str(BcCtx *c, const char *s)
{
    for (int i = 0; i < c->nconsts; i++)
        if (c->consts_is_str[i] && strcmp(c->consts_str[i], s) == 0)
            return i;
    int i = c->nconsts;
    if (i >= BC_MAX_TBL)
        return 0;
    c->consts_is_str[i] = 1;
    c->consts_num[i] = 0;
    c->consts_str[i] = s;
    c->nconsts++;
    return i;
}

/**
 * 查找名字（变量名/函数名，去重）。
 *
 * 遍历符号表比较字符串内容；找不到就追加一项并返回其索引。
 * 表项是指针，借用 AST 节点的 str 字段，生命周期随 AST 存活。
 *
 * @param c 生成上下文
 * @param s 变量名或函数名
 * @return 该名字在符号表中的索引
 */
static int bc_name(BcCtx *c, const char *s)
{
    for (int i = 0; i < c->nnames; i++)
        if (strcmp(c->names[i], s) == 0)
            return i;
    int i = c->nnames;
    if (i >= BC_MAX_TBL)
        return 0;
    c->names[i] = s;
    c->nnames++;
    return i;
}

/**
 * 二元运算助记符（TokenKind → 助记符）。
 *
 * 与 runtime.h 的 bin_table 分发的运算符一一对应；
 * 未知 op 返回 "?"（正常不会走到）。
 *
 * @param op 运算符对应的 TokenKind（TK_PLUS/TK_MINUS/...）
 * @return 助记符字符串，如 "ADD"、"LE"
 */
static const char *bc_op_name(int op)
{
    switch (op)
    {
    case TK_PLUS:   return "ADD";
    case TK_MINUS:  return "SUB";
    case TK_STAR:   return "MUL";
    case TK_SLASH:  return "DIV";
    case TK_PERCENT:return "MOD";
    case TK_EQEQ:   return "EQ";
    case TK_NEQ:    return "NE";
    case TK_GT:     return "GT";
    case TK_LT:     return "LT";
    case TK_GE:     return "GE";
    case TK_LE:     return "LE";
    }
    return "?";
}

/**
 * 递归生成一个节点的指令序列。
 *
 * 每种节点翻译成一组栈式指令（见文件头指令集参考）。
 * 返回该子树生成的指令条数，父节点据此计算 if/while 的跳转目标。
 *
 * @param c 生成上下文
 * @param n 当前 AST 节点
 * @return 该子树生成的指令条数
 */
static int bc_emit(BcCtx *c, const Node *n)
{
    if (!n)
        return 0;
    int before = c->nins;
    char buf[64];

    switch (n->kind)
    {
    /* 数字字面量：常量表登记数值，LOAD_CONST 压栈 */
    case N_NUM: {
        int idx = bc_const_num(c, n->num);
        snprintf(buf, sizeof(buf), "LOAD_CONST %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    /* 字符串字面量：常量表登记字符串，LOAD_CONST 压栈 */
    case N_STR: {
        int idx = bc_const_str(c, n->str);
        snprintf(buf, sizeof(buf), "LOAD_CONST %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    /* 变量读取：符号表登记变量名，LOAD_NAME 把当前值压栈 */
    case N_IDENT: {
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "LOAD_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    /* 变量声明：先求初始值表达式压栈，再 STORE_NAME 存入变量 */
    case N_VAR: {
        bc_emit(c, n->right);
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "STORE_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    /* 数组声明：先求广播初始值，NEW_ARRAY 建数组，再 STORE_NAME 存入变量 */
    case N_ARRAY: {
        bc_emit(c, n->right);
        snprintf(buf, sizeof(buf), "NEW_ARRAY %lld", n->num);
        bc_ins(c, buf, n->line);
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "STORE_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    /* 赋值：目标为数组下标走 STORE_INDEX（基→下标→新值压栈），
     * 否则走 STORE_NAME（只求新值） */
    case N_ASSIGN: {
        if (n->left && n->left->kind == N_INDEX)
        {
            bc_emit(c, n->left->left);   /* 基表达式（数组名/嵌套下标） */
            bc_emit(c, n->left->right);  /* 下标表达式 */
            bc_emit(c, n->right);        /* 新值 */
            bc_ins(c, "STORE_INDEX", n->line);
        }
        else
        {
            bc_emit(c, n->right);
            int idx = bc_name(c, n->left->str);
            snprintf(buf, sizeof(buf), "STORE_NAME %d", idx);
            bc_ins(c, buf, n->line);
        }
        break;
    }
    /* 数组下标读取：先求基址再求下标，LOAD_INDEX 取出元素压栈 */
    case N_INDEX: {
        bc_emit(c, n->left);
        bc_emit(c, n->right);
        bc_ins(c, "LOAD_INDEX", n->line);
        break;
    }
    /* 二元运算：左、右操作数依次压栈，再发对应助记符 */
    case N_BINARY: {
        bc_emit(c, n->left);
        bc_emit(c, n->right);
        bc_ins(c, bc_op_name(n->op), n->line);
        break;
    }
    /* 函数调用：逐个实参压栈，再 CALL_FN/CALL_BUILTIN 调用。
     * "喵叫" 是内置打印函数，走 CALL_BUILTIN，其余走 CALL_FN */
    case N_CALL: {
        for (int i = 0; i < n->nargs; i++)
            bc_emit(c, n->args[i]);
        int idx = bc_name(c, n->str);
        if (!strcmp(n->str, "喵叫"))
            snprintf(buf, sizeof(buf), "CALL_BUILTIN %d %d", idx, n->nargs);
        else
            snprintf(buf, sizeof(buf), "CALL_FN %d %d", idx, n->nargs);
        bc_ins(c, buf, n->line);
        break;
    }
    /* if/else：条件压栈 → JUMP_IF_FALSE 跳过真分支 → 真分支 →
     * JUMP 跳过假分支 → 假分支（可空）。
     * L_false 与 L_end 在递归返回后由指令号算出 */
    case N_IF: {
        bc_emit(c, n->left);             /* 条件表达式 */
        int jf = c->nins;
        bc_jump(c, 1, n->line);          /* JUMP_IF_FALSE L_false */
        bc_emit(c, n->body);             /* 真分支语句块 */
        int jm = c->nins;
        bc_jump(c, 0, n->line);          /* JUMP L_end */
        int l_false = c->pc;             /* 假分支起点 = 下一条真实指令号 */
        int l_end = l_false;
        if (n->elseB)
            l_end = l_false + bc_emit(c, n->elseB);  /* 假分支（可空） */
        c->ins[jf].target = l_false;     /* 回填两个跳转目标 */
        c->ins[jm].target = l_end;
        break;
    }
    /* while：L_start 标条件起点 → 条件 → JUMP_IF_FALSE 出循环 →
     * 循环体 → JUMP 跳回 L_start。
     * L_end = JUMP 之后的下一条指令号 */
    case N_WHILE: {
        int l_start = c->pc;             /* 循环入口（条件起点） */
        bc_emit(c, n->left);             /* 循环条件 */
        int jf = c->nins;
        bc_jump(c, 1, n->line);          /* JUMP_IF_FALSE L_end */
        bc_emit(c, n->body);             /* 循环体 */
        int jm = c->nins;
        bc_jump(c, 0, n->line);          /* JUMP L_start */
        c->ins[jf].target = c->pc;       /* L_end = 下一条真实指令号 */
        c->ins[jm].target = l_start;
        break;
    }
    /* 返回：返回值表达式压栈（可空），RETURN_VALUE 结束当前函数 */
    case N_RETURN: {
        if (n->left)
            bc_emit(c, n->left);
        bc_ins(c, "RETURN_VALUE", n->line);
        break;
    }
    /* 函数定义：只登记不执行，MAKE_FUNCTION 后跟函数体指令 */
    case N_FUNC: {
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "MAKE_FUNCTION %d %d", idx, n->nparams);
        bc_ins(c, buf, n->line);
        bc_emit(c, n->body);
        break;
    }
    /* 程序/代码块：逐条生成内部语句 */
    case N_PROG:
    case N_BLOCK: {
        for (int i = 0; i < n->nstmts; i++)
            bc_emit(c, n->stmts[i]);
        break;
    }
    }
    return c->nins - before;
}

/**
 * 带转义地打印带引号的字符串。
 *
 * 输出双引号包裹的字符串，其中 " \ 换行 制表符 分别转义为
 * \" \\ \n \t，保证输出单行、可读、可回填。
 *
 * @param f 输出流（stderr）
 * @param s 要打印的字符串
 */
static void bc_print_quoted(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++)
    {
        if (*s == '"')
            fputs("\\\"", f);
        else if (*s == '\\')
            fputs("\\\\", f);
        else if (*s == '\n')
            fputs("\\n", f);
        else if (*s == '\t')
            fputs("\\t", f);
        else
            fputc(*s, f);
    }
    fputc('"', f);
}

/**
 * 把编译好的 AST 静态翻译成字节码文本并输出到 stderr。
 *
 * 输出结构（头尾用 ================ 包裹）：
 *   == 常量表 ==    去重的数字/字符串常量，指令用索引引用
 *   == 符号表 ==    去重的变量名/函数名
 *   == 跳转表 ==    全部 JUMP / JUMP_IF_FALSE 的来源 → 目标
 *   == 字节码 ==    全局平铺的指令流（L<pc>: 标注跳转目标）
 *
 * 主程序语句在前，随后是 fn_table 里每个函数的
 * MAKE_FUNCTION + 函数体展开（段首有 "; --- 函数 xxx ---" 注释）。
 *
 * @param prog 程序节点（顶层语句列表）
 */
static void debug_dump_bytecode(const Node *prog)
{
    BcCtx c;
    memset(&c, 0, sizeof(c));

    /* 第一遍：静态遍历 AST，累积指令流与常量表/符号表 */
    bc_comment(&c, "; --- 主程序 ---");
    bc_emit(&c, prog);
    for (int i = 0; i < fn_count; i++)
    {
        Fn *f = fn_table[i];
        char buf[64];
        snprintf(buf, sizeof(buf), "; --- 函数 %s ---", f->name);
        bc_comment(&c, buf);
        int idx = bc_name(&c, f->name);
        snprintf(buf, sizeof(buf), "MAKE_FUNCTION %d %d", idx, f->nparams);
        bc_ins(&c, buf, f->body ? f->body->line : 0);
        bc_emit(&c, f->body);
    }

    /* 第二遍：回填跳转指令文本，并标记所有被跳转引用的指令号，
     * 使输出时在目标指令前插入 "L<pc>:" 伪 label 行 */
    for (int i = 0; i < c.nins; i++)
    {
        BcIns *in = &c.ins[i];
        if (!in->is_jump)
            continue;
        if (in->target >= 0 && in->target < BC_MAX_INS)
            c.label_at[in->target] = 1;
        snprintf(in->text, sizeof(in->text), "%s L%d",
                 in->jkind ? "JUMP_IF_FALSE" : "JUMP", in->target);
    }

    /* 输出：头尾分割线包住，依次是常量表、符号表、跳转表、字节码流 */
    fprintf(stderr, "=================\n");

    fprintf(stderr, "== 常量表 ==\n");
    for (int i = 0; i < c.nconsts; i++)
    {
        fprintf(stderr, "%d   ", i);
        if (c.consts_is_str[i])
            bc_print_quoted(stderr, c.consts_str[i]);
        else
            fprintf(stderr, "%lld", c.consts_num[i]);
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "== 符号表 ==\n");
    for (int i = 0; i < c.nnames; i++)
    {
        fprintf(stderr, "%d   ", i);
        bc_print_quoted(stderr, c.names[i]);
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "== 跳转表 ==\n");
    for (int i = 0; i < c.nins; i++)
    {
        BcIns *in = &c.ins[i];
        if (in->is_jump)
            fprintf(stderr, "%s → L%d\n",
                    in->jkind ? "JUMP_IF_FALSE" : "JUMP", in->target);
    }

    fprintf(stderr, "== 字节码 ==\n");
    for (int i = 0; i < c.nins; i++)
    {
        BcIns *in = &c.ins[i];
        if (in->is_comment)
        {
            fprintf(stderr, "%s\n", in->text);
            continue;
        }
        /* 被跳转引用的目标指令前插一行伪 label */
        if (in->pc < BC_MAX_INS && c.label_at[in->pc])
            fprintf(stderr, "L%d:\n", in->pc);
        fprintf(stderr, "%3d   %-22s ; 行%d\n", in->pc, in->text, in->line);
    }

    fprintf(stderr, "=================\n");
}

#else /* 未定义 DEBUG：字节码输出为空 */

/**
 * 把编译好的 AST 静态翻译成字节码文本并输出到 stderr（空实现）。
 *
 * 未定义 DEBUG 时整个函数体为空，调用点无任何开销；
 * @param prog 程序节点（未使用，仅保持调用点签名一致）
 */
static void debug_dump_bytecode(const Node *prog)
{
    (void)prog;
}

#endif /* DEBUG */

#endif /* RUA_BYTECODE_H */
