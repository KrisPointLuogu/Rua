/*
 * 语法分析器 —— Parser（递归下降）
 *
 * 将 Token 流解析为抽象语法树（AST）。
 * 每个文法规则对应一个解析方法。
 *
 * 文法：
 *   program     = function*
 *   function    = "喵" IDENTIFIER "(" [params] ")" block
 *   params      = IDENTIFIER ("," IDENTIFIER)*
 *   block       = "{" statement* "}"
 *   statement   = varDecl | ifStmt | whileStmt | returnStmt | exprStmt
 *   varDecl     = "变量" IDENTIFIER "=" expression
 *   ifStmt      = "如果" expression "那么" block ("否则" block)?
 *   whileStmt   = "当" expression "那么" block
 *   returnStmt  = "返回" expression
 *   exprStmt    = expression
 *   expression  = equality
 *   equality    = comparison (("=="|"!=") comparison)*
 *   comparison  = addition ((">"|"<") addition)*
 *   addition    = multiplication (("+"|"-") multiplication)*
 *   multiplication = primary (("*"|"/"|"%") primary)*
 *   primary     = NUMBER | STRING | "(" expression ")"
 *                | IDENTIFIER ("(" [args] ")")?
 *                | "喵叫" "(" [args] ")"
 */

#include "语法分析器.h"
#include "UTF32支持.h"
#include <sstream>

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::vector;

// 令牌类型常量引用（来自 词法分析器.h）
// 为了可读性，用别名
namespace TK
{
    enum
    {
        函数 = 1, // 喵
        如果 = 2, // 如果
        那么 = 3, // 那么
        否则 = 4, // 否则
        或者 = 5, // 或者
        返回 = 6, // 返回
        当 = 7,   // 当
        变量 = 8, // 变量
        数组 = 9, // 数组
        标识符 = 10,
        数字 = 11,
        字符串 = 12,
        左括号 = 13,
        右括号 = 14,
        左中括号 = 15,
        右中括号 = 16,
        左花括号 = 17,
        右花括号 = 18,
        逗号 = 19,
        分号 = 20,
        等号 = 21,
        加号 = 22,
        减号 = 23,
        乘号 = 24,
        除号 = 25,
        等于 = 26,
        不等于 = 27,
        大于 = 28,
        小于 = 29,
        模 = 30,
        乘方 = 31,
        整除 = 32,
        换行符 = 40,
        结束 = 41,
        喵叫 = 42,
        运行 = 43,
        大于等于 = 44,
        小于等于 = 45,
    };
}

// ==================== ParserError ====================

ParserError::ParserError(const string &message)
    : std::runtime_error(message) {}

// ==================== Parser ====================

Parser::Parser(const vector<令牌> &tokens)
    : tokens(tokens), current(0) {}

// ==================== UTF-32 → UTF-8 ====================

string Parser::u32to8(const std::u32string &u32)
{
    return UTF32转UTF8(u32);
}

// ==================== 入口 ====================

unique_ptr<Program> Parser::parseProgram()
{
    auto program = make_unique<Program>();

    while (!isAtEnd() && peek().类型_ != TK::结束)
    {
        // 跳过空行
        if (peek().类型_ == TK::换行符)
        {
            advance();
            continue;
        }

        if (peek().类型_ == TK::函数)
        {
            program->functions.push_back(parseFunction());
        }
        else
        {
            program->topLevelStmts.push_back(parseStatement());
        }
    }

    return program;
}

// ==================== 函数解析 ====================

unique_ptr<Function> Parser::parseFunction()
{
    auto func = make_unique<Function>();
    func->line = peek().行位置_;
    func->column = peek().列位置_;

    consume(TK::函数, "函数定义需要以 '喵' 开头");

    const 令牌 &nameToken = consume(TK::标识符, "函数定义需要函数名");
    func->name = u32to8(nameToken.内容_);

    consume(TK::左括号, "函数名后需要 '('");

    if (!check(TK::右括号))
    {
        const 令牌 &firstParam = consume(TK::标识符, "参数名需要是标识符");
        func->params.push_back(u32to8(firstParam.内容_));

        while (match(TK::逗号))
        {
            const 令牌 &param = consume(TK::标识符, "逗号后需要参数名");
            func->params.push_back(u32to8(param.内容_));
        }
    }

    consume(TK::右括号, "参数列表后需要 ')'");

    // 跳过换行
    while (check(TK::换行符))
        advance();

    func->body = parseBlock();

    return func;
}

// ==================== 语句块解析 ====================

unique_ptr<Block> Parser::parseBlock()
{
    auto block = make_unique<Block>();
    block->line = peek().行位置_;
    block->column = peek().列位置_;

    consume(TK::左花括号, "语句块需要以 '{' 开头");

    while (!check(TK::右花括号) && !isAtEnd())
    {
        // 跳过换行
        while (check(TK::换行符))
            advance();
        if (check(TK::右花括号) || isAtEnd())
            break;

        block->statements.push_back(parseStatement());
    }

    consume(TK::右花括号, "语句块需要以 '}' 结束");

    return block;
}

// ==================== 语句解析 ====================

unique_ptr<ASTNode> Parser::parseStatement()
{
    // 跳过换行
    while (check(TK::换行符))
        advance();

    if (check(TK::变量))
        return parseVarDecl();
    if (check(TK::如果))
        return parseIfStmt();
    if (check(TK::当))
        return parseWhileStmt();
    if (check(TK::返回))
        return parseReturnStmt();

    return parseExprStmt();
}

unique_ptr<VarDecl> Parser::parseVarDecl()
{
    auto varDecl = make_unique<VarDecl>();
    varDecl->line = peek().行位置_;
    varDecl->column = peek().列位置_;

    consume(TK::变量, "变量声明需要以 '变量' 开头");

    const 令牌 &nameToken = consume(TK::标识符, "变量声明需要变量名");
    varDecl->name = u32to8(nameToken.内容_);

    consume(TK::等号, "变量声明需要 '='");
    varDecl->initializer = parseExpression();

    return varDecl;
}

unique_ptr<IfStmt> Parser::parseIfStmt()
{
    auto ifStmt = make_unique<IfStmt>();
    ifStmt->line = peek().行位置_;
    ifStmt->column = peek().列位置_;

    consume(TK::如果, "条件分支需要以 '如果' 开头");

    ifStmt->condition = parseExpression();

    // 可选的 "那么"
    if (check(TK::那么))
        advance();

    // 跳过换行
    while (check(TK::换行符))
        advance();

    ifStmt->thenBranch = parseBlock();

    // 跳过换行
    while (check(TK::换行符))
        advance();

    if (match(TK::否则))
    {
        // 跳过换行
        while (check(TK::换行符))
            advance();
        ifStmt->elseBranch = parseBlock();
    }

    return ifStmt;
}

unique_ptr<WhileStmt> Parser::parseWhileStmt()
{
    auto whileStmt = make_unique<WhileStmt>();
    whileStmt->line = peek().行位置_;
    whileStmt->column = peek().列位置_;

    consume(TK::当, "循环需要以 '当' 开头");

    whileStmt->condition = parseExpression();

    // 可选的 "那么"
    if (check(TK::那么))
        advance();

    // 跳过换行
    while (check(TK::换行符))
        advance();

    whileStmt->body = parseBlock();

    return whileStmt;
}

unique_ptr<ReturnStmt> Parser::parseReturnStmt()
{
    auto retStmt = make_unique<ReturnStmt>();
    retStmt->line = peek().行位置_;
    retStmt->column = peek().列位置_;

    consume(TK::返回, "返回语句需要以 '返回' 开头");
    retStmt->value = parseExpression();

    return retStmt;
}

unique_ptr<ASTNode> Parser::parseExprStmt()
{
    auto exprStmt = make_unique<ExprStmt>();
    exprStmt->line = peek().行位置_;
    exprStmt->column = peek().列位置_;

    exprStmt->expression = parseExpression();

    return exprStmt;
}

// ==================== 表达式解析 ====================

// expression = assignment
unique_ptr<ASTNode> Parser::parseExpression()
{
    return parseAssignment();
}

// assignment = equality ("=" assignment)?
unique_ptr<ASTNode> Parser::parseAssignment()
{
    auto expr = parseEquality();

    if (match(TK::等号))
    {
        auto assign = make_unique<BinaryExpr>();
        assign->line = previous().行位置_;
        assign->column = previous().列位置_;
        assign->op = previous().类型_;
        assign->left = std::move(expr);
        assign->right = parseAssignment();
        return assign;
    }

    return expr;
}

unique_ptr<ASTNode> Parser::parseEquality()
{
    auto expr = parseComparison();

    while (match({TK::等于, TK::不等于}))
    {
        auto binary = make_unique<BinaryExpr>();
        binary->line = previous().行位置_;
        binary->column = previous().列位置_;
        binary->op = previous().类型_;
        binary->left = std::move(expr);
        binary->right = parseComparison();
        expr = std::move(binary);
    }

    return expr;
}

unique_ptr<ASTNode> Parser::parseComparison()
{
    auto expr = parseAddition();

    while (match({TK::大于, TK::小于, TK::大于等于, TK::小于等于}))
    {
        auto binary = make_unique<BinaryExpr>();
        binary->line = previous().行位置_;
        binary->column = previous().列位置_;
        binary->op = previous().类型_;
        binary->left = std::move(expr);
        binary->right = parseAddition();
        expr = std::move(binary);
    }

    return expr;
}

unique_ptr<ASTNode> Parser::parseAddition()
{
    auto expr = parseMultiplication();

    while (match({TK::加号, TK::减号}))
    {
        auto binary = make_unique<BinaryExpr>();
        binary->line = previous().行位置_;
        binary->column = previous().列位置_;
        binary->op = previous().类型_;
        binary->left = std::move(expr);
        binary->right = parseMultiplication();
        expr = std::move(binary);
    }

    return expr;
}

unique_ptr<ASTNode> Parser::parseMultiplication()
{
    auto expr = parsePrimary();

    while (match({TK::乘号, TK::除号, TK::模}))
    {
        auto binary = make_unique<BinaryExpr>();
        binary->line = previous().行位置_;
        binary->column = previous().列位置_;
        binary->op = previous().类型_;
        binary->left = std::move(expr);
        binary->right = parsePrimary();
        expr = std::move(binary);
    }

    return expr;
}

unique_ptr<ASTNode> Parser::parsePrimary()
{
    if (match(TK::数字))
    {
        auto num = make_unique<NumberLiteral>();
        num->line = previous().行位置_;
        num->column = previous().列位置_;
        num->value = std::stoi(u32to8(previous().内容_));
        return num;
    }

    if (match(TK::字符串))
    {
        auto str = make_unique<StringLiteral>();
        str->line = previous().行位置_;
        str->column = previous().列位置_;
        str->value = u32to8(previous().内容_);
        return str;
    }

    if (match(TK::标识符))
    {
        string name = u32to8(previous().内容_);
        int line = previous().行位置_;
        int column = previous().列位置_;

        if (match(TK::左括号))
        {
            return parseCall(name, line, column);
        }

        auto id = make_unique<Identifier>();
        id->line = line;
        id->column = column;
        id->name = name;
        return id;
    }

    // 内置函数关键词也可作为函数调用
    if (match(TK::喵叫) || match(TK::运行))
    {
        string name = u32to8(previous().内容_);
        int line = previous().行位置_;
        int column = previous().列位置_;
        consume(TK::左括号, "'" + name + "' 后需要 '('");
        return parseCall(name, line, column);
    }

    if (match(TK::左括号))
    {
        auto expr = parseExpression();
        consume(TK::右括号, "括号表达式需要 ')'");
        return expr;
    }

    throw error(peek(), "期望表达式（数字、字符串、标识符或括号表达式）");
}

unique_ptr<CallExpr> Parser::parseCall(const string &callee, int line, int column)
{
    auto call = make_unique<CallExpr>();
    call->line = line;
    call->column = column;
    call->callee = callee;

    if (!check(TK::右括号))
    {
        call->args.push_back(parseExpression());
        while (match(TK::逗号))
        {
            call->args.push_back(parseExpression());
        }
    }

    consume(TK::右括号, "函数调用参数后需要 ')'");

    return call;
}

// ==================== 工具方法 ====================

bool Parser::isAtEnd() const
{
    return current >= tokens.size() || tokens[current].类型_ == TK::结束;
}

const 令牌 &Parser::peek() const
{
    return tokens[current];
}

const 令牌 &Parser::previous() const
{
    return tokens[current - 1];
}

const 令牌 &Parser::advance()
{
    if (!isAtEnd())
        current++;
    return previous();
}

bool Parser::check(int type) const
{
    if (isAtEnd())
        return false;
    return peek().类型_ == type;
}

bool Parser::match(int type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const vector<int> &types)
{
    for (auto type : types)
    {
        if (check(type))
        {
            advance();
            return true;
        }
    }
    return false;
}

const 令牌 &Parser::consume(int type, const string &errorMsg)
{
    if (check(type))
        return advance();
    throw error(peek(), errorMsg);
}

ParserError Parser::error(const 令牌 &tok, const string &msg)
{
    std::ostringstream oss;
    oss << "语法错误，第 " << tok.行位置_ << " 行，第 " << tok.列位置_ << " 列："
        << msg << "，但遇到了 \"" << u32to8(tok.内容_) << "\"";
    return ParserError(oss.str());
}
