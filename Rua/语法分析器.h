#pragma once
#include <vector>
#include <memory>
#include <string>
#include <stdexcept>
#include "UTF32支持.h"
#include "语法树.h"

// 从词法分析器引用令牌类型
#include "词法分析器.h"
using 令牌类型 = int;
using 令牌 = 词法分析器类::令牌;

//
// 语法分析器 —— 递归下降解析，将 Token 流转换为 AST
//
// 文法：
//   program     = function*
//   function    = "喵" IDENTIFIER "(" [params] ")" block
//   params      = IDENTIFIER ("," IDENTIFIER)*
//   block       = "{" statement* "}"
//   statement   = varDecl | ifStmt | whileStmt | returnStmt | exprStmt
//   varDecl     = "变量" IDENTIFIER "=" expression
//   ifStmt      = "如果" expression "那么" block ("否则" block)?
//   whileStmt   = "当" expression "那么" block
//   returnStmt  = "返回" expression
//   exprStmt    = expression
//   expression  = equality
//   equality    = comparison (("=="|"!=") comparison)*
//   comparison  = addition ((">"|"<") addition)*
//   addition    = multiplication (("+"|"-") multiplication)*
//   multiplication = primary (("*"|"/"|"%") primary)*
//   primary     = NUMBER | STRING | "(" expression ")"
//                | IDENTIFIER ("(" [args] ")")?
//

// ----------------------------------------------------------
// ParserError —— 语法错误异常
// ----------------------------------------------------------
class ParserError : public std::runtime_error {
public:
    explicit ParserError(const std::string &message);
};

// ----------------------------------------------------------
// Parser —— 递归下降语法分析器
// ----------------------------------------------------------
class Parser {
private:
    const std::vector<令牌> &tokens;
    size_t current;

public:
    explicit Parser(const std::vector<令牌> &tokens);
    std::unique_ptr<Program> parseProgram();

private:
    bool isAtEnd() const;
    const 令牌 &peek() const;
    const 令牌 &previous() const;
    const 令牌 &advance();
    bool check(int type) const;
    bool match(int type);
    bool match(const std::vector<int> &types);
    const 令牌 &consume(int type, const std::string &errorMsg);
    ParserError error(const 令牌 &tok, const std::string &msg);

    // 递归下降解析
    std::unique_ptr<Function> parseFunction();
    std::unique_ptr<Block> parseBlock();
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<VarDecl> parseVarDecl();
    std::unique_ptr<IfStmt> parseIfStmt();
    std::unique_ptr<WhileStmt> parseWhileStmt();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<ASTNode> parseExprStmt();

    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseAssignment();
    std::unique_ptr<ASTNode> parseEquality();
    std::unique_ptr<ASTNode> parseComparison();
    std::unique_ptr<ASTNode> parseAddition();
    std::unique_ptr<ASTNode> parseMultiplication();
    std::unique_ptr<ASTNode> parsePrimary();
    std::unique_ptr<CallExpr> parseCall(const std::string &callee, int line, int column);

    // UTF-32 → UTF-8 辅助
    static std::string u32to8(const std::u32string &u32);
};
