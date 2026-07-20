#pragma once
#include <string>
#include <vector>
#include <memory>

//
// 语法树 —— 抽象语法树（AST）节点定义
//
// 每个节点继承自 ASTNode 基类，使用 Visitor 模式遍历。
// 所有节点记录源代码位置，方便报错。
//

// 前置声明
class Program;
class Function;
class Block;
class VarDecl;
class IfStmt;
class WhileStmt;
class ReturnStmt;
class ExprStmt;
class BinaryExpr;
class CallExpr;
class NumberLiteral;
class StringLiteral;
class Identifier;

// ----------------------------------------------------------
// ASTVisitor —— Visitor 模式基类
// ----------------------------------------------------------
class ASTVisitor
{
public:
    virtual ~ASTVisitor() = default;
    virtual void visit(Program &node) = 0;
    virtual void visit(Function &node) = 0;
    virtual void visit(Block &node) = 0;
    virtual void visit(VarDecl &node) = 0;
    virtual void visit(IfStmt &node) = 0;
    virtual void visit(WhileStmt &node) = 0;
    virtual void visit(ReturnStmt &node) = 0;
    virtual void visit(ExprStmt &node) = 0;
    virtual void visit(BinaryExpr &node) = 0;
    virtual void visit(CallExpr &node) = 0;
    virtual void visit(NumberLiteral &node) = 0;
    virtual void visit(StringLiteral &node) = 0;
    virtual void visit(Identifier &node) = 0;
};

// ----------------------------------------------------------
// NodeType —— 所有 AST 节点类型的枚举
// ----------------------------------------------------------
enum class NodeType
{
    PROGRAM,
    FUNCTION,
    BLOCK,
    VAR_DECL,
    IF_STMT,
    WHILE_STMT,
    RETURN_STMT,
    EXPR_STMT,
    BINARY_EXPR,
    CALL_EXPR,
    NUMBER_LITERAL,
    STRING_LITERAL,
    IDENTIFIER
};

// ----------------------------------------------------------
// ASTNode —— AST 节点基类
// ----------------------------------------------------------
class ASTNode
{
public:
    virtual ~ASTNode() = default;
    virtual NodeType getType() const = 0;
    virtual void accept(ASTVisitor &visitor) = 0;
    int line = 0;
    int column = 0;
};

// ==================== 具体节点类型 ====================

class Program : public ASTNode
{
public:
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<ASTNode>> topLevelStmts;
    NodeType getType() const override { return NodeType::PROGRAM; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class Function : public ASTNode
{
public:
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<Block> body;
    NodeType getType() const override { return NodeType::FUNCTION; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class Block : public ASTNode
{
public:
    std::vector<std::unique_ptr<ASTNode>> statements;
    NodeType getType() const override { return NodeType::BLOCK; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class VarDecl : public ASTNode
{
public:
    std::string name;
    std::unique_ptr<ASTNode> initializer;
    NodeType getType() const override { return NodeType::VAR_DECL; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class IfStmt : public ASTNode
{
public:
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<Block> thenBranch;
    std::unique_ptr<Block> elseBranch;
    NodeType getType() const override { return NodeType::IF_STMT; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class WhileStmt : public ASTNode
{
public:
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<Block> body;
    NodeType getType() const override { return NodeType::WHILE_STMT; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class ReturnStmt : public ASTNode
{
public:
    std::unique_ptr<ASTNode> value;
    NodeType getType() const override { return NodeType::RETURN_STMT; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class ExprStmt : public ASTNode
{
public:
    std::unique_ptr<ASTNode> expression;
    NodeType getType() const override { return NodeType::EXPR_STMT; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class BinaryExpr : public ASTNode
{
public:
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    int op; // TokenType enum value (from 词法分析器.h)
    NodeType getType() const override { return NodeType::BINARY_EXPR; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class CallExpr : public ASTNode
{
public:
    std::string callee;
    std::vector<std::unique_ptr<ASTNode>> args;
    NodeType getType() const override { return NodeType::CALL_EXPR; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class NumberLiteral : public ASTNode
{
public:
    int value;
    NodeType getType() const override { return NodeType::NUMBER_LITERAL; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class StringLiteral : public ASTNode
{
public:
    std::string value;
    NodeType getType() const override { return NodeType::STRING_LITERAL; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};

class Identifier : public ASTNode
{
public:
    std::string name;
    NodeType getType() const override { return NodeType::IDENTIFIER; }
    void accept(ASTVisitor &visitor) override { visitor.visit(*this); }
};
