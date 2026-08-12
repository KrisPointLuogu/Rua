#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "AST.h"

//
// 语义分析 —— 符号表管理、作用域检查
//

// ----------------------------------------------------------
// SemanticError —— 语义错误异常
// ----------------------------------------------------------
class SemanticError : public std::runtime_error {
  public:
    explicit SemanticError(const std::string& message);
};

// ----------------------------------------------------------
// SymbolKind —— 符号种类
// ----------------------------------------------------------
enum class SymbolKind { VARIABLE, FUNCTION };

// ----------------------------------------------------------
// Symbol —— 符号表中的单个条目
// ----------------------------------------------------------
struct Symbol {
    std::string name;
    SymbolKind kind;
    int line;
    int paramCount = 0;
    int localCount = 0;
    int slotIndex = -1;
    int arrayRank = 0; // 0 = 标量，>=1 = 数组维度
};

// ----------------------------------------------------------
// SymbolTable —— 符号表，支持嵌套作用域
// ----------------------------------------------------------
class SymbolTable {
  private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes;
    std::unordered_map<std::string, Symbol> functions;

  public:
    SymbolTable();
    void enterScope();
    void exitScope();
    void declareVariable(const std::string& name, int line,
                         int arrayRank = 0);
    void declareFunction(const std::string& name, int paramCount, int line);
    Symbol* lookup(const std::string& name);
    Symbol* lookupFunction(const std::string& name);
    int currentScopeVariableCount() const;
    const std::unordered_map<std::string, Symbol>& getFunctions() const;
};

// ----------------------------------------------------------
// SemanticAnalyzer —— 语义分析器（Visitor 模式）
// ----------------------------------------------------------
class SemanticAnalyzer : public ASTVisitor {
  private:
    SymbolTable symbolTable;
    std::string currentFunction;
    bool inFunction;

  public:
    SemanticAnalyzer();
    bool analyze(Program& program);
    const SymbolTable& getSymbolTable() const;

    int visit(Program& node) override;
    int visit(Function& node) override;
    int visit(Block& node) override;
    int visit(VarDecl& node) override;
    int visit(ArrayDecl& node) override;
    int visit(ArrayLiteral& node) override;
    int visit(IfStmt& node) override;
    int visit(WhileStmt& node) override;
    int visit(ReturnStmt& node) override;
    int visit(ExprStmt& node) override;
    int visit(BinaryExpr& node) override;
    int visit(CallExpr& node) override;
    int visit(IndexExpr& node) override;
    int visit(NumberLiteral& node) override;
    int visit(StringLiteral& node) override;
    int visit(Identifier& node) override;

  private:
    // 常量折叠：长度表达式仅限数字字面量 + 算术运算
    bool 折叠常量表达式(const ASTNode* node, int& out);
    void 校验数组初始化(ASTNode* init, int rank,
                      const std::vector<int>& constDims);
    void 校验嵌套列表(ArrayLiteral* lit, int level, int rank,
                    const std::vector<int>& constDims);
    void 校验索引链(IndexExpr& node);
};
