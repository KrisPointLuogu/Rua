/*
 * easy/rua.c — Rua 语言简化版解释器（C 语言，头文件式分文件）
 *
 * 功能：读取一个 .rua 源文件，词法分析 -> 语法分析（编译成 AST）->
 * 直接解释运行。 去掉了 REPL、字节码、虚拟机、JIT，只保留最核心的编译+运行。
 *
 * 实现方式（FP 风格）：
 *   - 纯函数：输入数据，返回数据，不依赖可变全局状态；
 *   - 递归求值：AST 用递归下降方式求值；
 *   - 函数指针：二元运算符通过函数指针表分发。
 *
 * 目录结构：
 *   rua.c         主程序（入口）
 *   includes/     头文件实现（头文件即实现，无构建系统）
 *     utf8.h      UTF-8 工具
 *     lexer.h     词法分析
 *     ast.h       语法树
 *     parser.h    语法分析
 *     runtime.h   运行时 + 内置二元运算
 *     debug_process.h 调试输出与计时（定义 DEBUG 才启用）
 *     jit.h       JIT 入口（定义 OPTIMIZATION 才启用）
 *     jit/ 目录    JIT 实现（jit_base/emit/analyze/codegen/debug.h）
 *   docs/API.md   完整 API 文档
 *   scripts/      构建脚本（build_easy_linux.py / build_easy_windows.py）
 *   example/      示例程序
 *   Makefile      构建（make / make release / make interp）
 *
 * 手动编译：
 *   cc easy/rua.c -o rua
 *   或
 *   gcc easy/rua.c -o rua
 *   然后： ./rua 示例.rua
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "includes/ast.h"
#include "includes/bytecode.h"
#include "includes/debug_process.h"
#include "includes/lexer.h"
#include "includes/parser.h"
#include "includes/runtime.h"
#include "includes/utf8.h"
#ifdef OPTIMIZATION
#include "includes/jit.h"
#endif

/**
 * 编译源码：词法分析 + 语法分析，产出可直接运行的程序节点。
 *
 * 顶层语句存入程序节点；顶层函数定义（喵 ...）直接注册进运行时函数表。
 * 扫描完释放 Token 数组及其文本。
 *
 * @param src 以 \0 结尾的源码字符串（UTF-8）
 * @return N_PROG 程序节点（顶层语句列表）
 */
static Node *compile(const char *src)
{
    int count = 0;
    Token *toks = lex_all(src, &count);

    debug_dump_tokens(toks, count);

    Parser ps;
    ps.toks = toks;
    ps.n = count;
    ps.pos = 0;

    Node *prog = new_node(N_PROG, 0);
    prog->stmts = (Node **)malloc(8 * sizeof(Node *));
    prog->nstmts = 0;
    int cap = 8;

    while (peek(&ps) != TK_EOF)
    {
        if (peek(&ps) == TK_MIAO)
        {
            register_fn(parse_function(&ps));
        }
        else
        {
            node_list_add(&prog->stmts, &prog->nstmts, &cap,
                          parse_statement(&ps));
        }
    }

    debug_print_stmts(prog);
    debug_print_fns(fn_table, fn_count);

    for (int i = 0; i < count; i++)
        free(toks[i].text);
    free(toks);
    return prog;
}

/**
 * 读取整个文件到内存。
 *
 * @param path 文件路径
 * @return malloc 分配的缓冲区，内容以 \0 结尾；打开失败则报错退出
 */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "无法打开文件：%s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(size + 1);
    if (!buf || fread(buf, 1, size, f) != (size_t)size)
    {
        fprintf(stderr, "读取文件失败：%s\n", path);
        exit(1);
    }
    buf[size] = 0;
    fclose(f);
    return buf;
}

/**
 * 程序入口。
 *
 * 用法： rua <源文件.rua>
 *   - 无参数或 -h/--help 时打印用法；
 *   - 读取文件 -> 编译 -> 在全局环境下执行程序。
 *
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 进程退出码
 */
int main(int argc, char *argv[])
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
    {
        printf("Rua 简易解释器\n");
        printf("用法： %s <源文件.rua>\n", argv[0]);
        return argc < 2 ? 1 : 0;
    }

    init_bin_ops();

    char *src = read_file(argv[1]);
    debug_print("[LANG] 读取文件：%s（%lu 字节）", argv[1], (unsigned long)strlen(src));

    Node *prog;
    DBG_TIMED("编译", prog = compile(src));
    debug_print("[LANG] 编译结果：顶层语句 %d 条，注册函数 %d 个", prog->nstmts,
                fn_count);
    /* 静态遍历 AST 生成字节码文本（仅 DEBUG 生效，非 DEBUG 为空实现）；
     * 放在解释执行之前，输出到 stderr 便于观察程序结构 */
    debug_dump_bytecode(prog);

#ifdef OPTIMIZATION
    DBG_TIMED("JIT 编译", jit_compile_all(fn_table, fn_count));
#endif

    Ctx ctx = {
        .env = env_new(NULL),
        .returning = 0,
        .retval = num_val(0)};
    DBG_TIMED("用户源码运行时间", exec(prog, &ctx));
    return 0;
}
