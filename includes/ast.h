/* easy/ast.h — 语法树
 *
 * 定义抽象语法树（AST）节点：一个通用的 Node 结构，
 * 用 kind 区分节点类型，各字段按节点类型复用，
 * 由语法分析器（parser.h）生成，交给运行时（runtime.h）解释执行。
 */

#ifndef RUA_AST_H
#define RUA_AST_H

#include <stdlib.h>

/**
 * AST 节点类型。
 *
 * N_ 前缀说明：
 *   - 表达式：N_NUM（数字）、N_STR（字符串）、N_IDENT（标识符/变量读取）、
 *     N_BINARY（二元运算）、N_CALL（函数调用）、N_INDEX（数组下标读取）；
 *   - 语句：N_VAR（变量声明）、N_ARRAY（数组声明）、N_ASSIGN（赋值）、
 *     N_IF（如果）、N_WHILE（当）、N_RETURN（返回）、N_BLOCK（代码块）；
 *   - 顶层：N_PROG（程序）、N_FUNC（函数定义）。
 */
typedef enum
{
    N_NUM,
    N_STR,
    N_IDENT,
    N_VAR,
    N_ARRAY,
    N_ASSIGN,
    N_INDEX,
    N_BINARY,
    N_CALL,
    N_IF,
    N_WHILE,
    N_RETURN,
    N_BLOCK,
    N_PROG,
    N_FUNC
} NodeKind;

typedef struct Node Node;

/**
 * AST 节点（通用结构，字段按 kind 复用）。
 *
 *   - N_NUM：       num 保存整数值；
 *   - N_STR：       str 保存字符串内容；
 *   - N_IDENT：     str 保存变量名；
 *   - N_VAR：       str 变量名，right 初始化表达式；
 *   - N_ARRAY：     str 数组名，num 长度，right 广播初始值表达式；
 *   - N_ASSIGN：    left 赋值目标（N_IDENT 或 N_INDEX），right 新值；
 *   - N_INDEX：     left 被索引的基表达式，right 下标表达式；
 *   - N_BINARY：    op 运算符，left/right 两个操作数；
 *   - N_CALL：      str 函数名，args/nargs 实参列表；
 *   - N_IF：        left 条件，body 真分支块，elseB 假分支块（可空）；
 *   - N_WHILE：     left 条件，body 循环体块；
 *   - N_RETURN：    left 返回值表达式（可为空）；
 *   - N_BLOCK：     stmts/nstmts 语句列表；
 *   - N_PROG：      顶层语句列表（同 N_BLOCK 用法）；
 *   - N_FUNC：      str 函数名，params/nparams 形参表，body 函数体块。
 */
struct Node
{
    NodeKind kind;
    int line;
    long long num;
    char *str;
    int op;
    Node *left;
    Node *right;
    Node *body;
    Node *elseB;
    Node **stmts;
    int nstmts;
    Node **args;
    int nargs;
    char **params;
    int nparams;
};

/**
 * 创建一个 AST 节点。
 *
 * calloc 清零所有字段，只设置类型与行号，其余字段由语法分析器填充。
 *
 * @param kind 节点类型
 * @param line 对应源码行号（用于报错定位）
 * @return malloc 分配的节点指针，调用方负责后续释放
 */
static Node *new_node(NodeKind kind, int line)
{
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

#ifdef DEBUG
/**
 * 返回 AST 节点类型的可读名（仅调试输出用）。
 * @param k 节点类型
 * @return 中文字符串名
 */
static const char *node_kind_name(NodeKind k)
{
    switch (k)
    {
    case N_NUM:
        return "数字";
    case N_STR:
        return "字符串";
    case N_IDENT:
        return "变量引用";
    case N_VAR:
        return "变量声明";
    case N_ARRAY:
        return "数组声明";
    case N_ASSIGN:
        return "赋值";
    case N_INDEX:
        return "数组下标";
    case N_BINARY:
        return "二元运算";
    case N_CALL:
        return "函数调用";
    case N_IF:
        return "如果";
    case N_WHILE:
        return "当循环";
    case N_RETURN:
        return "返回";
    case N_BLOCK:
        return "代码块";
    case N_PROG:
        return "程序";
    case N_FUNC:
        return "函数定义";
    }
    return "未知";
}
#endif /* DEBUG */

#endif
