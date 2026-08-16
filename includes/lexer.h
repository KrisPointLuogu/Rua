/* easy/lexer.h — 词法分析
 *
 * 把源码字符串切成一个个 Token（词法单元）：
 *   - 识别中文关键字（喵、变量、如果、那么、否则、当、返回、喵叫、数组）；
 *   - 识别标识符、整数、字符串字面量；
 *   - 识别运算符与界符；
 *   - 跳过空白与注释（# 单行、#* ... *# 多行）。
 */

#ifndef RUA_LEXER_H
#define RUA_LEXER_H

#include <stdlib.h>
#include <string.h>
#include "utf8.h"

/**
 * Token 类型。
 *
 * TK_ 前缀 + 英文缩写表示中文关键字：
 *   TK_MIAO=喵 TK_BIAN=变量
 *   TK_RUGUO=如果 TK_NAME=那么
 *   TK_FOUZE=否则
 *   TK_DANG=当 TK_FANHUI=返回
 *    TK_MIAOJIAO=喵叫 TK_SHIZU=数组
 * 其余为数字、字符串、标识符、运算符和界符。
 */
typedef enum
{
    TK_EOF,
    TK_NUM,
    TK_STR,
    TK_IDENT,
    TK_MIAO,
    TK_BIAN,
    TK_RUGUO,
    TK_NAME,
    TK_FOUZE,
    TK_DANG,
    TK_FANHUI,
    TK_MIAOJIAO,
    TK_SHIZU,
    TK_LP,
    TK_RP,
    TK_LB,
    TK_RB,
    TK_LC,
    TK_RC,
    TK_COMMA,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_PERCENT,
    TK_EQEQ,
    TK_NEQ,
    TK_GT,
    TK_LT,
    TK_GE,
    TK_LE,
    TK_ASSIGN
} TokenKind;

/**
 * 一个词法单元。
 *
 * @param kind token 类型
 * @param text 附加文本：标识符名 / 字符串字面量内容（数字与运算符为 NULL）
 * @param num  数字字面量的数值（仅 kind == TK_NUM 时有效）
 * @param line 该 token 所在源文件行号（用于报错定位）
 */
typedef struct
{
    TokenKind kind;
    char *text;
    long long num;
    int line;
} Token;

/**
 * 构造一个最普通的 Token（不含文本）。
 *
 * text 统一置为 NULL，仅数字 token 需要额外设置 num。
 *
 * @param kind token 类型
 * @param num  数字字面量的值（非数字时填 0）
 * @param line 所在行号
 * @return 构造好的 Token
 */
static Token token_make(TokenKind kind, long long num, int line)
{
    Token t;
    t.kind = kind;
    t.text = NULL;
    t.num = num;
    t.line = line;
    return t;
}

/**
 * 判断一段 UTF-8 文本是不是关键字。
 *
 * 把词法分析器切出的完整标识符串与关键字表逐项比对；
 * 命中返回对应关键字类型，否则返回 TK_IDENT（普通标识符）。
 *
 * @param s 以 \0 结尾的 UTF-8 文本
 * @return 对应的关键字 TokenKind，或 TK_IDENT
 */
static TokenKind keyword_kind(const char *s)
{
    if (!strcmp(s, "喵"))
        return TK_MIAO;
    if (!strcmp(s, "变量"))
        return TK_BIAN;
    if (!strcmp(s, "如果"))
        return TK_RUGUO;
    if (!strcmp(s, "那么"))
        return TK_NAME;
    if (!strcmp(s, "否则"))
        return TK_FOUZE;
    if (!strcmp(s, "当"))
        return TK_DANG;
    if (!strcmp(s, "返回"))
        return TK_FANHUI;
    if (!strcmp(s, "喵叫"))
        return TK_MIAOJIAO;
    if (!strcmp(s, "数组"))
        return TK_SHIZU;
    return TK_IDENT;
}

/**
 * 词法分析器的工作状态。
 *
 * @param p    指向源码中下一个待扫描字符的指针
 * @param line 当前所在行号（遇换行 +1，用于报错定位）
 */
typedef struct
{
    const char *p;
    int line;
} Lexer;

/**
 * 扫描一个字符串字面量，处理转义序列。
 *
 * 支持成对引号："" ''（英文）与“” ‘’（中文全角）。
 * 字符串内可包含 \n \t \r \\ \" \' 转义（仅英文引号内生效）。
 * 字符串内容存入 Token.text（malloc 分配，调用方负责释放）。
 *
 * @param lx   词法分析器状态，p 指向起始引号，会被推进到字符串结束之后
 * @param q    起始引号字符（决定对应的结束引号）
 * @param line 字符串起始所在行号
 * @return 内容为字符串字面量的 Token
 */
static Token lex_string(Lexer *lx, Char q, int line)
{
    Char close;
    if (q == '"')
        close = '"';
    else if (q == '\'')
        close = '\'';
    else if (q == 0x201C)
        close = 0x201D;
    else
        close = 0x2019;
    utf8_advance(&lx->p);

    char *buf = (char *)malloc(16);
    int len = 0, cap = 16;

    for (;;)
    {
        if (*lx->p == 0)
            error_at("字符串未闭合", line);
        Char c = utf8_peek(lx->p);
        if (c == '\n')
            lx->line++;
        if (c == close)
        {
            utf8_advance(&lx->p);
            break;
        }

        if (c == '\\' && (q == '"' || q == '\''))
        {
            utf8_advance(&lx->p);
            Char e = utf8_peek(lx->p);
            char esc = 0;
            if (e == 'n')
                esc = '\n';
            else if (e == 't')
                esc = '\t';
            else if (e == 'r')
                esc = '\r';
            else if (e == '\\')
                esc = '\\';
            else if (e == '"')
                esc = '"';
            else if (e == '\'')
                esc = '\'';
            utf8_advance(&lx->p);
            if (esc)
            {
                if (len + 1 >= cap)
                {
                    cap *= 2;
                    buf = (char *)realloc(buf, cap);
                }
                buf[len++] = esc;
            }
            continue;
        }

        const char *start = lx->p;
        utf8_advance(&lx->p);
        int n = (int)(lx->p - start);
        if (len + n + 1 >= cap)
        {
            while (len + n + 1 >= cap)
                cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        memcpy(buf + len, start, n);
        len += n;
    }

    buf[len] = 0;
    Token t = token_make(TK_STR, 0, line);
    t.text = buf;
    return t;
}

/**
 * 扫描下一个 Token（跳过空白与注释）。
 *
 * 依次识别：注释 -> 数字 -> 标识符/关键字 -> 字符串 -> 运算符/界符。
 * 遇到未知字符或未闭合字符串会调用 error_at 报错。
 *
 * @param lx 词法分析器状态，p 会被推进到下一个 Token 之后
 * @return 扫描出的下一个 Token；源码扫完时返回 TK_EOF
 */
static Token lex_next(Lexer *lx)
{
    for (;;)
    {
        if (*lx->p == 0)
            return token_make(TK_EOF, 0, lx->line);
        char c0 = *lx->p;

        if (c0 == '\n')
        {
            lx->p++;
            lx->line++;
            continue;
        }
        if (c0 == ' ' || c0 == '\t' || c0 == '\r')
        {
            lx->p++;
            continue;
        }

        if (c0 == '#')
        {
            if (lx->p[1] == '*')
            {
                lx->p += 2;
                while (*lx->p)
                {
                    if (lx->p[0] == '*' && lx->p[1] == '#')
                    {
                        lx->p += 2;
                        break;
                    }
                    if (*lx->p == '\n')
                        lx->line++;
                    lx->p++;
                }
            }
            else
            {
                while (*lx->p && *lx->p != '\n')
                    lx->p++;
            }
            continue;
        }

        if (c0 >= '0' && c0 <= '9')
        {
            long long n = 0;
            while (*lx->p >= '0' && *lx->p <= '9')
            {
                n = n * 10 + (*lx->p - '0');
                lx->p++;
            }
            return token_make(TK_NUM, n, lx->line);
        }

        if (is_ident_char(utf8_peek(lx->p)))
        {
            const char *start = lx->p;
            while (is_ident_char(utf8_peek(lx->p)))
                utf8_advance(&lx->p);
            int len = (int)(lx->p - start);
            char *text = (char *)malloc(len + 1);
            memcpy(text, start, len);
            text[len] = 0;
            TokenKind k = keyword_kind(text);
            if (k == TK_IDENT)
            {
                Token t = token_make(TK_IDENT, 0, lx->line);
                t.text = text;
                return t;
            }
            free(text);
            return token_make(k, 0, lx->line);
        }

        if (c0 == '"' || c0 == '\'')
            return lex_string(lx, c0, lx->line);
        Char c = utf8_peek(lx->p);
        if (c == 0x201C || c == 0x2018)
            return lex_string(lx, c, lx->line);

        switch (c0)
        {
        case '+':
            lx->p++;
            return token_make(TK_PLUS, 0, lx->line);
        case '-':
            lx->p++;
            return token_make(TK_MINUS, 0, lx->line);
        case '*':
            lx->p++;
            return token_make(TK_STAR, 0, lx->line);
        case '/':
            lx->p++;
            return token_make(TK_SLASH, 0, lx->line);
        case '%':
            lx->p++;
            return token_make(TK_PERCENT, 0, lx->line);
        case '(':
            lx->p++;
            return token_make(TK_LP, 0, lx->line);
        case ')':
            lx->p++;
            return token_make(TK_RP, 0, lx->line);
        case '[':
            lx->p++;
            return token_make(TK_LB, 0, lx->line);
        case ']':
            lx->p++;
            return token_make(TK_RB, 0, lx->line);
        case '{':
            lx->p++;
            return token_make(TK_LC, 0, lx->line);
        case '}':
            lx->p++;
            return token_make(TK_RC, 0, lx->line);
        case ',':
            lx->p++;
            return token_make(TK_COMMA, 0, lx->line);
        case '=':
            if (lx->p[1] == '=')
            {
                lx->p += 2;
                return token_make(TK_EQEQ, 0, lx->line);
            }
            lx->p++;
            return token_make(TK_ASSIGN, 0, lx->line);
        case '!':
            if (lx->p[1] == '=')
            {
                lx->p += 2;
                return token_make(TK_NEQ, 0, lx->line);
            }
            error_at("无法识别的字符", lx->line);
        case '>':
            if (lx->p[1] == '=')
            {
                lx->p += 2;
                return token_make(TK_GE, 0, lx->line);
            }
            lx->p++;
            return token_make(TK_GT, 0, lx->line);
        case '<':
            if (lx->p[1] == '=')
            {
                lx->p += 2;
                return token_make(TK_LE, 0, lx->line);
            }
            lx->p++;
            return token_make(TK_LT, 0, lx->line);
        default:
            error_at("无法识别的字符", lx->line);
        }
    }
}

/**
 * 对整个源码执行词法分析。
 *
 * 循环调用 lex_next 直到 TK_EOF，把所有 Token 收集进动态数组。
 *
 * @param src   以 \0 结尾的源码字符串（UTF-8）
 * @param count 输出参数：返回的 Token 个数
 * @return malloc 分配的 Token 数组（含结尾的 TK_EOF），调用方负责释放
 */
static Token *lex_all(const char *src, int *count)
{
    Lexer lx;
    lx.p = src;
    lx.line = 1;
    Token *toks = (Token *)malloc(8 * sizeof(Token));
    int n = 0, cap = 8;
    for (;;)
    {
        Token t = lex_next(&lx);
        if (n + 1 >= cap)
        {
            cap *= 2;
            toks = (Token *)realloc(toks, cap * sizeof(Token));
        }
        toks[n++] = t;
        if (t.kind == TK_EOF)
            break;
    }
    *count = n;
    return toks;
}

#ifdef DEBUG
/**
 * 返回 Token 类型的可读名（仅调试输出用）。
 * @param k Token 类型
 * @return 中文字符串名
 */
static const char *token_kind_name(TokenKind k)
{
    switch (k)
    {
    case TK_EOF:
        return "文件结束";
    case TK_NUM:
        return "数字";
    case TK_STR:
        return "字符串";
    case TK_IDENT:
        return "标识符";
    case TK_MIAO:
        return "喵(函数)";
    case TK_BIAN:
        return "变量";
    case TK_RUGUO:
        return "如果";
    case TK_NAME:
        return "那么";
    case TK_FOUZE:
        return "否则";
    case TK_DANG:
        return "当";
    case TK_FANHUI:
        return "返回";
    case TK_MIAOJIAO:
        return "喵叫";
    case TK_SHIZU:
        return "数组";
    case TK_LP:
        return "左括号(";
    case TK_RP:
        return "右括号)";
    case TK_LB:
        return "左中括号[";
    case TK_RB:
        return "右中括号]";
    case TK_LC:
        return "左花括号{";
    case TK_RC:
        return "右花括号}";
    case TK_COMMA:
        return "逗号";
    case TK_PLUS:
        return "加号+";
    case TK_MINUS:
        return "减号-";
    case TK_STAR:
        return "乘号*";
    case TK_SLASH:
        return "除号/";
    case TK_PERCENT:
        return "模号%";
    case TK_EQEQ:
        return "等于==";
    case TK_NEQ:
        return "不等于!=";
    case TK_GT:
        return "大于>";
    case TK_LT:
        return "小于<";
    case TK_GE:
        return "大于等于>=";
    case TK_LE:
        return "小于等于<=";
    case TK_ASSIGN:
        return "赋值=";
    }
    return "未知";
}
#endif /* DEBUG */

#endif
