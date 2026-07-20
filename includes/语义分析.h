#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include "语法树.h"

//
// 语义分析 —— 符号表管理、作用域检查
//

// ----------------------------------------------------------
// SemanticError —— 语义错误异常
// ----------------------------------------------------------
class SemanticError : public std::runtime_error
{
public:
    explicit SemanticError(const std::string &message);
};

// ----------------------------------------------------------
// SymbolKind —— 符号种类
// ----------------------------------------------------------
enum class SymbolKind
{
    VARIABLE,
    FUNCTION
};

// ----------------------------------------------------------
// Symbol —— 符号表中的单个条目
// ----------------------------------------------------------
struct Symbol
{
    std::string name;
    SymbolKind kind;
    int line;
    int paramCount = 0;
    int localCount = 0;
    int slotIndex = -1;
};

// ----------------------------------------------------------
// SymbolTable —— 符号表，支持嵌套作用域
// ----------------------------------------------------------
class SymbolTable
{
private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes;
    std::unordered_map<std::string, Symbol> functions;

public:
    SymbolTable();
    void enterScope();
    void exitScope();
    void declareVariable(const std::string &name, int line);
    void declareFunction(const std::string &name, int paramCount, int line);
    Symbol *lookup(const std::string &name);
    Symbol *lookupFunction(const std::string &name);
    int currentScopeVariableCount() const;
    const std::unordered_map<std::string, Symbol> &getFunctions() const;
};

// ----------------------------------------------------------
// SemanticAnalyzer —— 语义分析器（Visitor 模式）
// ----------------------------------------------------------
class SemanticAnalyzer : public ASTVisitor
{
private:
    SymbolTable symbolTable;
    std::string currentFunction;
    bool inFunction;

public:
    SemanticAnalyzer();
    bool analyze(Program &program);
    const SymbolTable &getSymbolTable() const;

    void visit(Program &node) override;
    void visit(Function &node) override;
    void visit(Block &node) override;
    void visit(VarDecl &node) override;
    void visit(IfStmt &node) override;
    void visit(WhileStmt &node) override;
    void visit(ReturnStmt &node) override;
    void visit(ExprStmt &node) override;
    void visit(BinaryExpr &node) override;
    void visit(CallExpr &node) override;
    void visit(NumberLiteral &node) override;
    void visit(StringLiteral &node) override;
    void visit(Identifier &node) override;
};
