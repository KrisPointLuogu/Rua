/* easy/parser.h — 语法分析
 *
 * 递归下降语法分析器：把 Token 流编译成 AST。
 *   - 表达式用「优先级爬升」统一处理二元运算符（见 parse_expr_prec）；
 *   - 语句按关键字分发：变量/数组/如果/当/返回/表达式语句；
 *   - 顶层另处理函数定义（喵 ...）。
 */

#ifndef RUA_PARSER_H
#define RUA_PARSER_H

#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "lexer.h"

/**
 * 语法分析器的工作状态。
 *
 * @param toks Token 数组（来自词法分析器）
 * @param n    Token 总数（含结尾 TK_EOF）
 * @param pos  当前读到的 Token 下标
 */
typedef struct
{
    Token *toks;
    int n;
    int pos;
} Parser;

/**
 * 查看当前位置的 Token 类型（不前进）。
 *
 * @param ps 分析器状态
 * @return 当前 Token 的类型
 */
static TokenKind peek(Parser *ps) { return ps->toks[ps->pos].kind; }

/**
 * 取走当前位置的 Token 并前进。
 *
 * @param ps 分析器状态
 * @return 被取走的 Token 的指针（指向 toks 数组内部）
 */
static Token *advance(Parser *ps) { return &ps->toks[ps->pos++]; }

/**
 * 期望下一个 Token 是指定类型，否则报语法错误。
 *
 * @param ps 分析器状态
 * @param k  期望的 Token 类型
 * @return 被取走的 Token 的类型
 */
static TokenKind expect(Parser *ps, TokenKind k)
{
    if (peek(ps) != k)
    {
        error_at("语法错误：期望的符号不匹配", ps->toks[ps->pos].line);
    }
    return advance(ps)->kind;
}

/**
 * 期望下一个 Token 是标识符，并返回其名字（malloc 副本）。
 *
 * @param ps 分析器状态
 * @return 标识符字符串，调用方负责释放
 */
static char *expect_ident(Parser *ps)
{
    if (peek(ps) != TK_IDENT)
    {
        error_at("语法错误：期望标识符", ps->toks[ps->pos].line);
    }
    return strdup(advance(ps)->text);
}

/**
 * 往动态节点数组尾部追加一个节点（数组满时自动扩容）。
 *
 * @param list 节点数组指针（会被 realloc）
 * @param n    已用个数
 * @param cap  数组容量
 * @param item 要追加的节点
 */
static void node_list_add(Node ***list, int *n, int *cap, Node *item)
{
    if (*n + 1 >= *cap)
    {
        *cap *= 2;
        *list = (Node **)realloc(*list, *cap * sizeof(Node *));
    }
    (*list)[(*n)++] = item;
}

/**
 * 往动态字符串数组尾部追加一项（数组满时自动扩容）。
 *
 * @param list 字符串数组指针（会被 realloc）
 * @param n    已用个数
 * @param cap  数组容量
 * @param item 要追加的字符串
 */
static void str_list_add(char ***list, int *n, int *cap, char *item)
{
    if (*n + 1 >= *cap)
    {
        *cap *= 2;
        *list = (char **)realloc(*list, *cap * sizeof(char *));
    }
    (*list)[(*n)++] = item;
}

/* 前置声明：parse_expr 与 parse_block 互相递归调用 */
static Node *parse_expr(Parser *ps);
static Node *parse_primary(Parser *ps);

/**
 * 解析调用实参： ( 表达式, 表达式, ... )。
 *
 * @param ps    分析器状态
 * @param args  输出参数：实参节点数组（malloc，调用方负责释放）
 * @param nargs 输出参数：实参个数
 */
static void parse_call_args(Parser *ps, Node ***args, int *nargs)
{
    int cap = 8;
    *args = (Node **)malloc(cap * sizeof(Node *));
    *nargs = 0;
    expect(ps, TK_LP);
    if (peek(ps) != TK_RP)
    {
        node_list_add(args, nargs, &cap, parse_expr(ps));
        while (peek(ps) == TK_COMMA)
        {
            advance(ps);
            node_list_add(args, nargs, &cap, parse_expr(ps));
        }
    }
    expect(ps, TK_RP);
}

/**
 * 返回二元运算符的优先级（数字越大越优先），非运算符返回 0。
 *
 * @param k Token 类型
 * @return 优先级等级（1~4），不是二元运算符则为 0
 */
static int op_prec(TokenKind k)
{
    switch (k)
    {
    case TK_EQEQ:
    case TK_NEQ:
        return 1;
    case TK_GT:
    case TK_LT:
    case TK_GE:
    case TK_LE:
        return 2;
    case TK_PLUS:
    case TK_MINUS:
        return 3;
    case TK_STAR:
    case TK_SLASH:
    case TK_PERCENT:
        return 4;
    default:
        return 0;
    }
}

/**
 * 解析表达式（优先级爬升，左结合）。
 *
 * 先解析一个原子，再循环查看下一个运算符：若其优先级不低于
 * min_prec 就取走它并递归解析右操作数（右操作数优先级 +1 保证左结合）。
 *
 * @param ps      分析器状态
 * @param min_prec 允许的最低运算符优先级
 * @return 二元表达式对应的 AST 节点
 */
static Node *parse_expr_prec(Parser *ps, int min_prec)
{
    Node *left = parse_primary(ps);
    for (;;)
    {
        TokenKind op = peek(ps);
        int prec = op_prec(op);
        if (prec < min_prec)
            break;
        advance(ps);
        Node *right = parse_expr_prec(ps, prec + 1);
        Node *b = new_node(N_BINARY, ps->toks[ps->pos - 1].line);
        b->op = op;
        b->left = left;
        b->right = right;
        left = b;
    }
    return left;
}

/**
 * 解析原子表达式（最高优先级）。
 *
 * 可解析：数字、字符串、括号表达式、标识符、喵叫调用；
 * 之后循环处理后缀：函数调用（标识符后跟 ( 实参 )）与下标（后跟 [ 下标 ]）。
 *
 * @param ps 分析器状态
 * @return 原子表达式对应的 AST 节点
 */
static Node *parse_primary(Parser *ps)
{
    TokenKind k = peek(ps);
    Node *n = NULL;

    if (k == TK_NUM)
    {
        n = new_node(N_NUM, ps->toks[ps->pos].line);
        n->num = advance(ps)->num;
    }
    else if (k == TK_STR)
    {
        n = new_node(N_STR, ps->toks[ps->pos].line);
        n->str = strdup(advance(ps)->text);
    }
    else if (k == TK_LP)
    {
        advance(ps);
        n = parse_expr(ps);
        expect(ps, TK_RP);
    }
    else if (k == TK_IDENT)
    {
        n = new_node(N_IDENT, ps->toks[ps->pos].line);
        n->str = strdup(advance(ps)->text);
    }
    else if (k == TK_MIAOJIAO)
    {
        advance(ps);
        n = new_node(N_CALL, ps->toks[ps->pos - 1].line);
        n->str = strdup("喵叫");
        parse_call_args(ps, &n->args, &n->nargs);
        return n;
    }
    else
    {
        error_at("语法错误：期望表达式", ps->toks[ps->pos].line);
    }

    for (;;)
    {
        if (peek(ps) == TK_LP)
        {
            if (n->kind != N_IDENT)
                error_at("语法错误：只能调用函数", n->line);
            Node *c = new_node(N_CALL, n->line);
            c->str = n->str;
            parse_call_args(ps, &c->args, &c->nargs);
            n = c;
        }
        else if (peek(ps) == TK_LB)
        {
            advance(ps);
            Node *ix = new_node(N_INDEX, ps->toks[ps->pos - 1].line);
            ix->left = n;
            ix->right = parse_expr(ps);
            expect(ps, TK_RB);
            n = ix;
        }
        else
        {
            break;
        }
    }
    return n;
}

/**
 * 解析表达式（最低优先级，含赋值）。
 *
 * 先做二元运算优先级爬升，若后面紧跟 '=' 则构造成赋值节点
 * （目标为变量或数组下标）。
 *
 * @param ps 分析器状态
 * @return 表达式对应的 AST 节点
 */
static Node *parse_expr(Parser *ps)
{
    Node *n = parse_expr_prec(ps, 1);
    if (peek(ps) == TK_ASSIGN)
    {
        advance(ps);
        Node *v = parse_expr(ps);
        Node *a = new_node(N_ASSIGN, ps->toks[ps->pos - 1].line);
        a->left = n;
        a->right = v;
        n = a;
    }
    return n;
}

/* 前置声明：parse_block 与 parse_statement 互相递归调用 */
static Node *parse_block(Parser *ps);

/**
 * 解析变量声明：变量 名 = 表达式。
 *
 * @param ps 分析器状态
 * @return N_VAR 节点
 */
static Node *parse_var(Parser *ps)
{
    int line = advance(ps)->line;
    Node *n = new_node(N_VAR, line);
    n->str = expect_ident(ps);
    expect(ps, TK_ASSIGN);
    n->right = parse_expr(ps);
    return n;
}

/**
 * 解析数组声明：数组 名[长度] = { 广播初始值 }。
 *
 * 长度必须是数字字面量；{} 里的单个表达式会广播填充整个数组。
 *
 * @param ps 分析器状态
 * @return N_ARRAY 节点
 */
static Node *parse_array(Parser *ps)
{
    int line = advance(ps)->line;
    Node *n = new_node(N_ARRAY, line);
    n->str = expect_ident(ps);
    expect(ps, TK_LB);
    if (peek(ps) != TK_NUM)
        error_at("数组长度必须是数字", ps->toks[ps->pos].line);
    n->num = advance(ps)->num;
    expect(ps, TK_RB);
    expect(ps, TK_ASSIGN);
    expect(ps, TK_LC);
    n->right = parse_expr(ps);
    expect(ps, TK_RC);
    return n;
}

/**
 * 解析如果语句：如果 条件 那么? { } 否则? { }。
 *
 * 那么为可选引导词；否则分支也可选。
 *
 * @param ps 分析器状态
 * @return N_IF 节点
 */
static Node *parse_if(Parser *ps)
{
    int line = advance(ps)->line;
    Node *n = new_node(N_IF, line);
    n->left = parse_expr(ps);
    if (peek(ps) == TK_NAME)
        advance(ps);
    n->body = parse_block(ps);
    if (peek(ps) == TK_FOUZE)
    {
        advance(ps);
        n->elseB = parse_block(ps);
    }
    return n;
}

/**
 * 解析当循环：当 条件 那么? { }。
 *
 * @param ps 分析器状态
 * @return N_WHILE 节点
 */
static Node *parse_while(Parser *ps)
{
    int line = advance(ps)->line;
    Node *n = new_node(N_WHILE, line);
    n->left = parse_expr(ps);
    if (peek(ps) == TK_NAME)
        advance(ps);
    n->body = parse_block(ps);
    return n;
}

/**
 * 解析返回语句：返回 表达式?。
 *
 * 后面紧跟 } 或文件结束时视为无返回值（返回默认值 0）。
 *
 * @param ps 分析器状态
 * @return N_RETURN 节点
 */
static Node *parse_return(Parser *ps)
{
    int line = advance(ps)->line;
    Node *n = new_node(N_RETURN, line);
    if (peek(ps) != TK_LC && peek(ps) != TK_EOF && peek(ps) != TK_RC)
    {
        n->left = parse_expr(ps);
    }
    return n;
}

/**
 * 解析单条语句。
 *
 * 按关键字分发到对应的语句解析函数；
 * 其余一律按表达式语句处理（赋值、函数调用等）。
 *
 * @param ps 分析器状态
 * @return 语句对应的 AST 节点
 */
static Node *parse_statement(Parser *ps)
{
    switch (peek(ps))
    {
    case TK_BIAN:
        return parse_var(ps);
    case TK_SHIZU:
        return parse_array(ps);
    case TK_RUGUO:
        return parse_if(ps);
    case TK_DANG:
        return parse_while(ps);
    case TK_FANHUI:
        return parse_return(ps);
    default:
        return parse_expr(ps);
    }
}

/**
 * 解析代码块：{ 语句* }。
 *
 * @param ps 分析器状态
 * @return N_BLOCK 节点，包含语句列表
 */
static Node *parse_block(Parser *ps)
{
    int line = expect(ps, TK_LC) == TK_LC ? ps->toks[ps->pos - 1].line : 0;
    Node *b = new_node(N_BLOCK, line);
    int cap = 8;
    b->stmts = (Node **)malloc(cap * sizeof(Node *));
    b->nstmts = 0;
    while (peek(ps) != TK_RC && peek(ps) != TK_EOF)
    {
        node_list_add(&b->stmts, &b->nstmts, &cap, parse_statement(ps));
    }
    expect(ps, TK_RC);
    return b;
}

/**
 * 解析函数定义：喵 函数名(形参*) { 函数体 }。
 *
 * @param ps 分析器状态
 * @return N_FUNC 节点，随后由 compile() 注册进函数表
 */
static Node *parse_function(Parser *ps)
{
    int line = expect(ps, TK_MIAO) == TK_MIAO ? ps->toks[ps->pos - 1].line : 0;
    Node *f = new_node(N_FUNC, line);
    f->str = expect_ident(ps);
    expect(ps, TK_LP);
    int cap = 8;
    f->params = (char **)malloc(cap * sizeof(char *));
    f->nparams = 0;
    if (peek(ps) != TK_RP)
    {
        str_list_add(&f->params, &f->nparams, &cap, expect_ident(ps));
        while (peek(ps) == TK_COMMA)
        {
            advance(ps);
            str_list_add(&f->params, &f->nparams, &cap, expect_ident(ps));
        }
    }
    expect(ps, TK_RP);
    f->body = parse_block(ps);
    return f;
}

#endif
