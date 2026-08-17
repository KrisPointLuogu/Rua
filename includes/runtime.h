/* easy/runtime.h — 运行时
 *
 * 解释执行编译好的 AST：
 *   - 值系统：数字 / 字符串 / 数组（标签联合 Value）；
 *   - 环境：作用域链（Env），函数调用时新建子环境，用空闲链表复用减少分配；
 *   - 求值：exec() 按节点类型递归求值，返回语句通过 Ctx 的 returning
 * 标志向上传递；
 *   - 内置二元运算符：用函数指针表（bin_table）按运算符分发，FP 风格。
 */

#ifndef RUA_RUNTIME_H
#define RUA_RUNTIME_H

#include "ast.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * 值类型标记。
 *
 * @param V_NUM 数字（long long）
 * @param V_STR 字符串（char*）
 * @param V_ARR 数组（Array* 句柄）
 */
enum
{
    V_NUM = 0,
    V_STR = 1,
    V_ARR = 2
};

/**
 * 数组对象。
 *
 * 数组按引用共享（句柄语义）：赋值/传参只复制句柄本身，
 * 多个变量可指向同一份数组。
 *
 * @param size  数组长度
 * @param items 元素数组
 */
typedef struct Array
{
    long long size;
    struct Value *items;
} Array;

/**
 * 运行时的值：标签联合。
 *
 * 按 type 决定使用哪个字段：
 *   V_NUM 用 num，V_STR 用 str，V_ARR 用 arr。
 * 字符串/数组字段共享指针，不进行深拷贝。
 */
typedef struct Value
{
    int type;
    long long num;
    char *str;
    Array *arr;
} Value;

/* 前置声明：环境结构体 */
typedef struct Env Env;

/* 前置声明：变量绑定结构体 */
typedef struct Binding Binding;

/**
 * 一个变量绑定（环境里的一项）。
 *
 * @param name      变量名
 * @param val       变量当前值
 * @param next_free 空闲链表指针（该绑定被回收进空闲链表时使用）
 */
struct Binding
{
    char *name;
    Value val;
    Binding *next_free;
};

/**
 * 环境：一张作用域表，通过 parent 构成作用域链。
 *
 * 函数调用时新建子环境（parent 指向调用者环境）；
 * 查找变量沿作用域链向上（由内向外）。
 *
 * @param parent   父环境（外层作用域）
 * @param bindings 变量绑定数组
 * @param count    已用绑定数
 * @param cap      绑定数组容量
 * @param next_free 空闲链表指针（该环境被回收进空闲链表时使用）
 */
struct Env
{
    Env *parent;
    Binding *bindings;
    int count;
    int cap;
    Env *next_free;
};

/* 环境 / 绑定空闲链表头：函数调用结束后回收复用，避免频繁 malloc */
static Env *env_freelist = NULL;
static Binding *binding_freelist = NULL;

/**
 * 新建一个环境。
 *
 * 优先从空闲链表取（O(1)），链表为空才真正分配；
 * 绑定数组同样按需从空闲链表取。
 *
 * @param parent 父环境（可为 NULL，表示全局作用域）
 * @return 新建的环境指针
 */
static Env *env_new(Env *parent)
{
    Env *e = env_freelist ? env_freelist : (Env *)calloc(1, sizeof(Env));
    if (env_freelist)
        env_freelist = e->next_free;
    e->bindings = binding_freelist ? binding_freelist
                                   : (Binding *)malloc(8 * sizeof(Binding));
    if (binding_freelist)
        binding_freelist = e->bindings->next_free;
    e->parent = parent;
    e->count = 0;
    e->cap = 8;
    return e;
}

/**
 * 回收一个环境。
 *
 * 把环境及其绑定数组放回各自空闲链表（不释放内存，供下次复用）。
 * 由于函数调用严格按后进先出返回，回收是安全的。
 *
 * @param e 要回收的环境
 */
static void env_release(Env *e)
{
    e->next_free = env_freelist;
    env_freelist = e;
    e->bindings->next_free = binding_freelist;
    binding_freelist = e->bindings;
}

/**
 * 函数定义记录（函数表项）。
 *
 * @param name    函数名
 * @param params  形参名列表
 * @param nparams 形参个数
 * @param body    函数体（N_BLOCK 节点）
 */
typedef struct
{
    char *name;
    char **params;
    int nparams;
    Node *body;
    int jittable;   /* 1 = 已由 JIT 生成机器码 */
    void *jit_addr; /* 机器码入口（jittable 时有效） */
} Fn;

/* JIT 桥接（jit.h 实现）：仅 OPTIMIZATION 时启用 */
#ifdef OPTIMIZATION
typedef int64_t (*JitFn)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);
static int64_t jit_invoke(const Fn *f, const Value *args, int nargs);
#endif

/* 函数表：注册了所有顶层函数定义 */
static Fn *fn_table[256];
static int fn_count = 0;

/**
 * 求值上下文：一次函数调用（或程序运行）的状态。
 *
 * @param env      当前作用域环境
 * @param returning 是否已遇到返回语句（非 0 表示需要结束当前函数）
 * @param retval    返回语句携带的返回值
 */
typedef struct
{
    Env *env;
    int returning;
    Value retval;
} Ctx;

/**
 * 构造一个数字值。
 *
 * @param n 数值
 * @return 类型为 V_NUM 的值
 */
static Value num_val(long long n)
{
    Value v;
    v.type = V_NUM;
    v.num = n;
    v.str = NULL;
    v.arr = NULL;
    return v;
}

/**
 * 在当前环境绑定一个新变量（加入当前作用域）。
 *
 * 绑定数组满时自动扩容。
 *
 * @param e    目标环境
 * @param name 变量名
 * @param val  变量值
 */
static void env_bind(Env *e, const char *name, Value v)
{
    if (e->count + 1 >= e->cap)
    {
        e->cap *= 2;
        e->bindings = (Binding *)realloc(e->bindings, e->cap * sizeof(Binding));
    }
    e->bindings[e->count].name = strdup(name);
    e->bindings[e->count].val = v;
    e->count++;
}

/**
 * 沿作用域链查找变量值。
 *
 * @param e    起始环境
 * @param name 变量名
 * @return 最近一层作用域的变量值；找不到则报错终止
 */
static Value env_get(Env *e, const char *name)
{
    for (Env *cur = e; cur; cur = cur->parent)
    {
        for (int i = 0; i < cur->count; i++)
        {
            if (!strcmp(cur->bindings[i].name, name))
                return cur->bindings[i].val;
        }
    }
    error_at("未定义的变量", 0);
    return num_val(0);
}

/**
 * 沿作用域链找到变量并修改其值。
 *
 * 若各层都找不到该变量，则在当前环境新建绑定（隐式声明）。
 *
 * @param e    起始环境
 * @param name 变量名
 * @param v    新值
 */
static void env_set(Env *e, const char *name, Value v)
{
    for (Env *cur = e; cur; cur = cur->parent)
    {
        for (int i = 0; i < cur->count; i++)
        {
            if (!strcmp(cur->bindings[i].name, name))
            {
                cur->bindings[i].val = v;
                return;
            }
        }
    }
    env_bind(e, name, v);
}

/**
 * 在函数表中按名字查找函数。
 *
 * @param name 函数名
 * @return 找到的函数记录；未找到返回 NULL
 */
static Fn *find_fn(const char *name)
{
    for (int i = 0; i < fn_count; i++)
    {
        if (!strcmp(fn_table[i]->name, name))
            return fn_table[i];
    }
    return NULL;
}

/**
 * 把函数定义节点注册进函数表。
 *
 * 只复制函数名、形参表与函数体指针（共享 AST，不深拷贝）。
 *
 * @param f N_FUNC 节点
 */
static void register_fn(Node *f)
{
    Fn *fn = (Fn *)calloc(1, sizeof(Fn));
    fn->name = strdup(f->str);
    fn->params = f->params;
    fn->nparams = f->nparams;
    fn->body = f->body;
    fn_table[fn_count++] = fn;
}

/* 前置声明：相互递归的求值函数 */
static Value exec(Node *n, Ctx *ctx);
static Value apply_binop(int op, Value a, Value b);
static Value print_val(Value v);
static Value exec_call(Node *n, Ctx *ctx);

/**
 * 判断一个值是否为真（用于条件判断）。
 *
 * 数字非 0 为真；字符串非空为真；数组恒为真。
 *
 * @param v 待判断的值
 * @return 1 为真，0 为假
 */
static int is_true(Value v)
{
    if (v.type == V_STR)
        return v.str[0] != 0;
    if (v.type == V_ARR)
        return 1;
    return v.num != 0;
}

/**
 * 数组读取：arr[idx]。
 *
 * 越界或下标为负时抛运行时错误。
 *
 * @param base 数组句柄值
 * @param idx  下标值（必须是数字）
 * @param line 报错行号
 * @return 下标处的元素值
 */
static Value array_get(Value base, Value idx, int line)
{
    if (base.type != V_ARR)
        error_at("该变量不是数组", line);
    if (idx.type != V_NUM || idx.num < 0 || idx.num >= base.arr->size)
        error_at("数组下标越界", line);
    return base.arr->items[idx.num];
}

/**
 * 数组写入：arr[idx] = v。
 *
 * 越界或下标为负时抛运行时错误。
 *
 * @param base 数组句柄值
 * @param idx  下标值（必须是数字）
 * @param v    要写入的元素值
 * @param line 报错行号
 */
static void array_set(Value base, Value idx, Value v, int line)
{
    if (base.type != V_ARR)
        error_at("该变量不是数组", line);
    if (idx.type != V_NUM || idx.num < 0 || idx.num >= base.arr->size)
        error_at("数组下标越界", line);
    base.arr->items[idx.num] = v;
}

/**
 * 求值下标读取表达式（N_INDEX 节点）。
 *
 * 先求值基表达式与下标，再交给 array_get 完成读取。
 *
 * @param n   N_INDEX 节点
 * @param ctx 求值上下文
 * @return 数组下标处的元素值
 */
static Value index_read(Node *n, Ctx *ctx)
{
    Value base = exec(n->left, ctx);
    if (n->left->kind == N_IDENT && base.type != V_ARR)
        error_at("该变量不是数组", n->line);
    Value idx = exec(n->right, ctx);
    return array_get(base, idx, n->line);
}

/**
 * 把值写入赋值目标。
 *
 * 目标是变量（N_IDENT）则修改环境变量；
 * 目标是下标（N_INDEX）则修改数组元素。
 *
 * @param target 赋值目标节点（N_IDENT 或 N_INDEX）
 * @param v      新值
 * @param ctx    求值上下文
 */
static void assign_target(Node *target, Value v, Ctx *ctx)
{
    if (target->kind == N_IDENT)
    {
        env_set(ctx->env, target->str, v);
    }
    else if (target->kind == N_INDEX)
    {
        Value base = exec(target->left, ctx);
        Value idx = exec(target->right, ctx);
        array_set(base, idx, v, target->line);
    }
    else
    {
        error_at("赋值目标无效", target->line);
    }
}

/**
 * 打印一个值到标准输出（不换行）。
 *
 * 数字按 %lld 打印，字符串原样打印，数组打印 "数组"。
 *
 * @param v 要打印的值
 * @return 原值（便于链式使用）
 */
static Value print_val(Value v)
{
    if (v.type == V_STR)
        printf("%s", v.str);
    else if (v.type == V_ARR)
        printf("数组");
    else
        printf("%lld", v.num);
    return v;
}

/**
 * 执行函数调用（N_CALL 节点）。
 *
 * 内置"喵叫"：逐参数求值并打印，以空格分隔，末尾换行。
 * 用户函数：新建子环境绑定形参，在子上下文中求值函数体，
 * 调用结束后回收子环境，返回其返回值（无返回语句时返回函数体求值结果）。
 *
 * @param n   N_CALL 节点
 * @param ctx 调用者的求值上下文
 * @return 调用结果
 */
static Value exec_call(Node *n, Ctx *ctx)
{
    if (!strcmp(n->str, "喵叫"))
    {
        for (int i = 0; i < n->nargs; i++)
        {
            if (i > 0)
                printf(" ");
            print_val(exec(n->args[i], ctx));
        }
        printf("\n");
        return num_val(0);
    }

    Fn *f = find_fn(n->str);
    if (!f)
        error_at("未定义的函数", n->line);
    if (f->nparams != n->nargs)
        error_at("函数参数数量不匹配", n->line);
    if (n->nargs > 64)
        error_at("参数过多", n->line);

    Value arg_vals[64];
    for (int i = 0; i < n->nargs; i++)
        arg_vals[i] = exec(n->args[i], ctx);

#ifdef OPTIMIZATION
    if (f->jittable)
    {
        return num_val(jit_invoke(f, arg_vals, n->nargs));
    }
#endif

    Env *e = env_new(ctx->env);
    for (int i = 0; i < f->nparams; i++)
        env_bind(e, f->params[i], arg_vals[i]);

    Ctx child;
    child.env = e;
    child.returning = 0;
    child.retval = num_val(0);
    Value r = exec(f->body, &child);
    env_release(e);
    return child.returning ? child.retval : r;
}

/**
 * 解释执行 AST 节点（主分发函数）。
 *
 * 按节点类型递归求值：
 *   - 字面量/标识符直接取值；
 *   - 语句（声明、赋值、分支、循环、返回）按语义执行；
 *   - 二元运算通过函数指针表 apply_binop 分发；
 *   - 返回语句设置 ctx->returning，让循环/块提前结束并向上传递返回值。
 *
 * @param n   当前节点
 * @param ctx 求值上下文
 * @return 节点的求值结果
 */
static Value exec(Node *n, Ctx *ctx)
{
    if (!n)
        return num_val(0);

    switch (n->kind)
    {
    case N_NUM:
        return num_val(n->num);

    case N_STR:
    {
        Value v;
        v.type = V_STR;
        v.num = 0;
        v.str = n->str;
        v.arr = NULL;
        return v;
    }

    case N_IDENT:
        return env_get(ctx->env, n->str);

    case N_VAR:
    {
        Value v = exec(n->right, ctx);
        env_bind(ctx->env, n->str, v);
        return v;
    }

    case N_ARRAY:
    {
        Value init = exec(n->right, ctx);
        Array *a = (Array *)calloc(1, sizeof(Array));
        a->size = n->num;
        a->items = (Value *)malloc(sizeof(Value) * n->num);
        for (long long i = 0; i < n->num; i++)
            a->items[i] = init;
        Value v;
        v.type = V_ARR;
        v.num = 0;
        v.str = NULL;
        v.arr = a;
        env_bind(ctx->env, n->str, v);
        return v;
    }

    case N_ASSIGN:
    {
        Value v = exec(n->right, ctx);
        assign_target(n->left, v, ctx);
        return v;
    }

    case N_INDEX:
        return index_read(n, ctx);

    case N_BINARY:
    {
        Value a = exec(n->left, ctx);
        Value b = exec(n->right, ctx);
        return apply_binop(n->op, a, b);
    }

    case N_CALL:
        return exec_call(n, ctx);

    case N_IF:
    {
        Value c = exec(n->left, ctx);
        if (is_true(c))
            return exec(n->body, ctx);
        if (n->elseB)
            return exec(n->elseB, ctx);
        return num_val(0);
    }

    case N_WHILE:
    {
        Value last = num_val(0);
        while (is_true(exec(n->left, ctx)))
        {
            last = exec(n->body, ctx);
            if (ctx->returning)
                break;
        }
        return last;
    }

    case N_RETURN:
    {
        Value v = exec(n->left, ctx);
        ctx->returning = 1;
        ctx->retval = v;
        return v;
    }

    case N_FUNC:
        return num_val(0);

    case N_PROG:
    case N_BLOCK:
    {
        Value last = num_val(0);
        for (int i = 0; i < n->nstmts; i++)
        {
            last = exec(n->stmts[i], ctx);
            if (ctx->returning)
                break;
        }
        return last;
    }
    }
    return num_val(0);
}

/* ------- 内置二元运算符（函数指针分发） ------- */

/**
 * 二元运算符函数指针类型。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 运算结果
 */
typedef Value (*BinOp)(Value, Value);

/**
 * 把数字转成十进制字符串（临时使用）。
 *
 * @param n 数值
 * @return malloc 分配的字符串，调用方负责释放
 */
static char *num_to_str(long long n)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%lld", n);
    return strdup(buf);
}

/**
 * 加法：数字相加，任一侧为字符串则按字符串拼接。
 *
 * 字符串拼接时把另一侧的数字先转成字符串文本。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相加或拼接结果
 */
static Value bin_add(Value a, Value b)
{
    if (a.type == V_STR || b.type == V_STR)
    {
        char *sa = a.type == V_STR ? a.str : num_to_str(a.num);
        char *sb = b.type == V_STR ? b.str : num_to_str(b.num);
        char *res = (char *)malloc(strlen(sa) + strlen(sb) + 1);
        strcpy(res, sa);
        strcat(res, sb);
        if (a.type != V_STR)
            free(sa);
        if (b.type != V_STR)
            free(sb);
        Value v;
        v.type = V_STR;
        v.num = 0;
        v.str = res;
        v.arr = NULL;
        return v;
    }
    return num_val(a.num + b.num);
}

/**
 * 生成「纯数字算术」二元运算符（任一侧为字符串则报错）。
 *
 * @param name 函数名
 * @param op   运算符号（- * 等）
 */
#define DEFINE_ARITH_BINOP(name, op)            \
    static Value name(Value a, Value b)         \
    {                                           \
        if (a.type == V_STR || b.type == V_STR) \
        {                                       \
            error_at("运算需要数字", 0);        \
        }                                       \
        return num_val(a.num op b.num);         \
    }

DEFINE_ARITH_BINOP(bin_sub, -)
DEFINE_ARITH_BINOP(bin_mul, *)

/**
 * 除法：要求两个操作数都是数字，除数为 0 时报错。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 相除结果
 */
static Value bin_div(Value a, Value b)
{
    if (a.type == V_STR || b.type == V_STR)
        error_at("运算需要数字", 0);
    if (b.num == 0)
        error_at("不能除以 0", 0);
    return num_val(a.num / b.num);
}

/**
 * 取模：要求两个操作数都是数字，模数为 0 时报错。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 取模结果
 */
static Value bin_mod(Value a, Value b)
{
    if (a.type == V_STR || b.type == V_STR)
        error_at("运算需要数字", 0);
    if (b.num == 0)
        error_at("不能取模 0", 0);
    return num_val(a.num % b.num);
}

/**
 * 判断两个值是否相等。
 *
 * 两字符串比较内容；数字与数字比较数值；字符串与数字视为不相等。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 1 相等，0 不相等
 */
static int values_equal(Value a, Value b)
{
    if (a.type == V_STR || b.type == V_STR)
    {
        if (a.type != V_STR || b.type != V_STR)
            return 0;
        return !strcmp(a.str, b.str);
    }
    return a.num == b.num;
}

/**
 * 等于运算。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 1 相等，0 不相等
 */
static Value bin_eq(Value a, Value b) { return num_val(values_equal(a, b)); }

/**
 * 不等于运算。
 *
 * @param a 左操作数
 * @param b 右操作数
 * @return 1 不相等，0 相等
 */
static Value bin_ne(Value a, Value b) { return num_val(!values_equal(a, b)); }

/**
 * 生成「纯数字比较」二元运算符（任一侧为字符串则报错）。
 *
 * @param name 函数名
 * @param op   比较符号（> < >= <=）
 */
#define DEFINE_CMP_BINOP(name, op)              \
    static Value name(Value a, Value b)         \
    {                                           \
        if (a.type == V_STR || b.type == V_STR) \
        {                                       \
            error_at("比较需要数字", 0);        \
        }                                       \
        return num_val(a.num op b.num);         \
    }

DEFINE_CMP_BINOP(bin_gt, >)
DEFINE_CMP_BINOP(bin_lt, <)
DEFINE_CMP_BINOP(bin_ge, >=)
DEFINE_CMP_BINOP(bin_le, <=)

/* 运算符 -> 函数指针 的分发表，下标用 TokenKind 值 */
static BinOp bin_table[256];

/**
 * 初始化二元运算符函数指针表。
 *
 * 在 main 里程序运行前调用一次；
 * 把每个运算符的 TokenKind 映射到对应的实现函数。
 */
static void init_bin_ops(void)
{
    bin_table[TK_PLUS] = bin_add;
    bin_table[TK_MINUS] = bin_sub;
    bin_table[TK_STAR] = bin_mul;
    bin_table[TK_SLASH] = bin_div;
    bin_table[TK_PERCENT] = bin_mod;
    bin_table[TK_EQEQ] = bin_eq;
    bin_table[TK_NEQ] = bin_ne;
    bin_table[TK_GT] = bin_gt;
    bin_table[TK_LT] = bin_lt;
    bin_table[TK_GE] = bin_ge;
    bin_table[TK_LE] = bin_le;
}

/**
 * 应用二元运算符（通过函数指针表分发）。
 *
 * @param op 运算符对应的 TokenKind
 * @param a  左操作数
 * @param b  右操作数
 * @return 运算结果
 */
static Value apply_binop(int op, Value a, Value b)
{
    BinOp f = bin_table[op];
    if (!f)
        error_at("未知运算符", 0);
    return f(a, b);
}

#endif
