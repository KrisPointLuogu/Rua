/* includes/bytecode.h — 静态 AST → 字节码文本（仅 DEBUG）
 *
 * 把编译好的 AST 走一遍静态遍历，翻译成类似 Python dis / Java javap -c
 * 的助记符指令流，输出到 stderr：
 *   - 每条指令对应一个 AST 节点，附源码行号；
 *   - 常量表/符号表去重，指令用索引引用；
 *   - 控制流（if/while）生成伪 label（L<指令号>）与跳转指令，目标静态算出；
 *   - 函数体（fn_table 注册的顶层函数）在 MAKE_FUNCTION 后展开；
 *   - 只做结构翻译，不求值、不做常量折叠。
 *
 * 只有在编译时定义 DEBUG 宏才生效；否则 debug_dump_bytecode 为空实现。
 */

#ifndef RUA_BYTECODE_H
#define RUA_BYTECODE_H

#include <stdio.h>
#include <string.h>

#include "runtime.h"

#ifdef DEBUG

#define BC_MAX_INS 4096
#define BC_MAX_TBL 256

/** 一条字节码指令。 */
typedef struct
{
    int is_comment;  /* 1 = 分隔注释行（如 "; --- 函数 fib ---"），非指令 */
    int pc;          /* 指令号（真实指令连续递增，注释沿用上一指令号） */
    char text[64];   /* 助记符文本（含操作数），或注释内容 */
    int line;        /* 源码行号 */
    int is_jump;     /* 1 = 跳转指令（目标待回填） */
    int jkind;       /* 跳转类型：0 = JUMP，1 = JUMP_IF_FALSE */
    int target;      /* 跳转目标指令号 */
} BcIns;

/** 字节码生成上下文：指令流 + 常量表 + 符号表。 */
typedef struct
{
    BcIns ins[BC_MAX_INS];
    int nins;
    int pc; /* 当前指令号（真实指令才递增） */
    /* 常量表：is_str 区分字符串/数字，去重 */
    int consts_is_str[BC_MAX_TBL];
    long long consts_num[BC_MAX_TBL];
    const char *consts_str[BC_MAX_TBL];
    int nconsts;
    /* 符号表：变量名/函数名，去重（指针借用 AST 字符串） */
    const char *names[BC_MAX_TBL];
    int nnames;
    /* 被引用为跳转目标的指令号（打印时插 L<pc>: 行） */
    int label_at[BC_MAX_INS];
} BcCtx;

/** 追加一条普通指令。 */
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

/** 追加一条跳转指令（目标号后填）。 */
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

/** 追加一条注释分隔行（不占指令号）。 */
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

/** 查找数字常量，不存在则入表；返回索引。 */
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

/** 查找字符串常量，不存在则入表；返回索引。 */
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

/** 查找名字（变量/函数），不存在则入符号表；返回索引。 */
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

/** 二元运算助记符（TokenKind → 助记符）。 */
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

/** 递归生成节点指令，返回该子树生成的指令条数。 */
static int bc_emit(BcCtx *c, const Node *n)
{
    if (!n)
        return 0;
    int before = c->nins;
    char buf[64];

    switch (n->kind)
    {
    case N_NUM: {
        int idx = bc_const_num(c, n->num);
        snprintf(buf, sizeof(buf), "LOAD_CONST %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    case N_STR: {
        int idx = bc_const_str(c, n->str);
        snprintf(buf, sizeof(buf), "LOAD_CONST %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    case N_IDENT: {
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "LOAD_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    case N_VAR: {
        bc_emit(c, n->right);
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "STORE_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    case N_ARRAY: {
        bc_emit(c, n->right);
        snprintf(buf, sizeof(buf), "NEW_ARRAY %lld", n->num);
        bc_ins(c, buf, n->line);
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "STORE_NAME %d", idx);
        bc_ins(c, buf, n->line);
        break;
    }
    case N_ASSIGN: {
        if (n->left && n->left->kind == N_INDEX)
        {
            bc_emit(c, n->left->left);   /* 基表达式 */
            bc_emit(c, n->left->right);  /* 下标 */
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
    case N_INDEX: {
        bc_emit(c, n->left);
        bc_emit(c, n->right);
        bc_ins(c, "LOAD_INDEX", n->line);
        break;
    }
    case N_BINARY: {
        bc_emit(c, n->left);
        bc_emit(c, n->right);
        bc_ins(c, bc_op_name(n->op), n->line);
        break;
    }
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
    case N_IF: {
        bc_emit(c, n->left);             /* 条件 */
        int jf = c->nins;
        bc_jump(c, 1, n->line);          /* JUMP_IF_FALSE L_false */
        bc_emit(c, n->body);             /* 真分支 */
        int jm = c->nins;
        bc_jump(c, 0, n->line);          /* JUMP L_end */
        int l_false = c->pc;             /* 下一条真实指令的指令号 */
        int l_end = l_false;
        if (n->elseB)
            l_end = l_false + bc_emit(c, n->elseB);
        c->ins[jf].target = l_false;
        c->ins[jm].target = l_end;
        break;
    }
    case N_WHILE: {
        int l_start = c->pc;             /* L_start */
        bc_emit(c, n->left);             /* 条件 */
        int jf = c->nins;
        bc_jump(c, 1, n->line);          /* JUMP_IF_FALSE L_end */
        bc_emit(c, n->body);
        int jm = c->nins;
        bc_jump(c, 0, n->line);          /* JUMP L_start */
        c->ins[jf].target = c->pc;       /* 下一条真实指令 = L_end */
        c->ins[jm].target = l_start;
        break;
    }
    case N_RETURN: {
        if (n->left)
            bc_emit(c, n->left);
        bc_ins(c, "RETURN_VALUE", n->line);
        break;
    }
    case N_FUNC: {
        int idx = bc_name(c, n->str);
        snprintf(buf, sizeof(buf), "MAKE_FUNCTION %d %d", idx, n->nparams);
        bc_ins(c, buf, n->line);
        bc_emit(c, n->body);
        break;
    }
    case N_PROG:
    case N_BLOCK: {
        for (int i = 0; i < n->nstmts; i++)
            bc_emit(c, n->stmts[i]);
        break;
    }
    }
    return c->nins - before;
}

/** 带转义地打印带引号的字符串。 */
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

    /* 回填跳转指令文本并标记 label 位置 */
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
        if (in->pc < BC_MAX_INS && c.label_at[in->pc])
            fprintf(stderr, "L%d:\n", in->pc);
        fprintf(stderr, "%3d   %-22s ; 行%d\n", in->pc, in->text, in->line);
    }

    fprintf(stderr, "=================\n");
}

#else /* 未定义 DEBUG：字节码输出为空 */

static void debug_dump_bytecode(const Node *prog)
{
    (void)prog;
}

#endif /* DEBUG */

#endif /* RUA_BYTECODE_H */
