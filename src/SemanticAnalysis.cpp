/*
 * 语义分析器 —— SemanticAnalyzer
 *
 * 遍历 AST，进行以下检查：
 *   1. 函数定义收集与重复检查
 *   2. 变量作用域管理（块级作用域）
 *   3. 变量使用前声明检查
 *   4. 函数调用参数个数匹配检查
 *   5. return 语句必须在函数体内
 *
 * 同时构建符号表，供字节码生成阶段使用。
 */

#include "SemanticAnalysis.h"
#include <sstream>

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::vector;

// ==================== SemanticError ====================

SemanticError::SemanticError(const string& message)
  : std::runtime_error(message)
{
}

// ==================== SymbolTable ====================

SymbolTable::SymbolTable() { scopes.emplace_back(); }

void SymbolTable::enterScope() { scopes.emplace_back(); }

void SymbolTable::exitScope()
{
    if (scopes.size() > 1) scopes.pop_back();
}

void SymbolTable::declareVariable(const string& name, int line, bool isArray)
{
    auto& currentScope = scopes.back();
    if (currentScope.count(name)) {
        std::ostringstream oss;
        oss << "第 " << line << " 行：变量 '" << name
            << "' 在当前作用域中重复声明";
        throw SemanticError(oss.str());
    }

    Symbol sym;
    sym.name = name;
    sym.kind = SymbolKind::VARIABLE;
    sym.line = line;
    sym.slotIndex = static_cast<int>(currentScope.size());
    sym.isArray = isArray;
    currentScope[name] = sym;
}

void SymbolTable::declareFunction(const string& name, int paramCount, int line)
{
    if (functions.count(name)) {
        std::ostringstream oss;
        oss << "第 " << line << " 行：函数 '" << name << "' 重复定义";
        throw SemanticError(oss.str());
    }

    Symbol sym;
    sym.name = name;
    sym.kind = SymbolKind::FUNCTION;
    sym.line = line;
    sym.paramCount = paramCount;
    functions[name] = sym;
}

Symbol* SymbolTable::lookup(const string& name)
{
    for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) return &it->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookupFunction(const string& name)
{
    auto it = functions.find(name);
    if (it != functions.end()) return &it->second;
    return nullptr;
}

int SymbolTable::currentScopeVariableCount() const
{
    return static_cast<int>(scopes.back().size());
}

const std::unordered_map<string, Symbol>& SymbolTable::getFunctions() const
{
    return functions;
}

// ==================== SemanticAnalyzer ====================

SemanticAnalyzer::SemanticAnalyzer() : inFunction(false) {}

bool SemanticAnalyzer::analyze(Program& program)
{
    program.accept(*this);
    return true;
}

const SymbolTable& SemanticAnalyzer::getSymbolTable() const
{
    return symbolTable;
}

// ==================== Visitor 实现 ====================

int SemanticAnalyzer::visit(Program& node)
{
    symbolTable.declareFunction("喵叫", -1, 0);
    symbolTable.declareFunction("运行", 1, 0);

    for (auto& func : node.functions) {
        symbolTable.declareFunction(func->name,
                                    static_cast<int>(func->params.size()),
                                    func->line);
    }

    for (auto& func : node.functions) {
        currentFunction = func->name;
        inFunction = true;
        func->accept(*this);
    }

    inFunction = false;
    currentFunction.clear();

    // 分析顶层语句
    for (auto& stmt : node.topLevelStmts) { stmt->accept(*this); }

    return 0;
}

int SemanticAnalyzer::visit(Function& node)
{
    symbolTable.enterScope();

    for (const auto& param : node.params) {
        symbolTable.declareVariable(param, node.line);
    }

    Symbol* funcSym = symbolTable.lookupFunction(node.name);
    if (funcSym) { funcSym->localCount = static_cast<int>(node.params.size()); }

    node.body->accept(*this);

    if (funcSym) { funcSym->localCount = static_cast<int>(node.params.size()); }

    symbolTable.exitScope();
    return 0;
}

int SemanticAnalyzer::visit(Block& node)
{
    symbolTable.enterScope();

    for (auto& stmt : node.statements) { stmt->accept(*this); }

    symbolTable.exitScope();
    return 0;
}

int SemanticAnalyzer::visit(VarDecl& node)
{
    symbolTable.declareVariable(node.name, node.line);

    if (node.initializer) { node.initializer->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(ArrayDecl& node)
{
    if (node.size <= 0) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：数组 '" << node.name
            << "' 的长度必须为正数";
        throw SemanticError(oss.str());
    }

    symbolTable.declareVariable(node.name, node.line, true);

    if (node.initialValue) { node.initialValue->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(IfStmt& node)
{
    node.condition->accept(*this);
    node.thenBranch->accept(*this);

    if (node.elseBranch) { node.elseBranch->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(WhileStmt& node)
{
    node.condition->accept(*this);
    if (node.body) node.body->accept(*this);
    return 0;
}

int SemanticAnalyzer::visit(ReturnStmt& node)
{
    if (!inFunction) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：'返回' 语句只能在函数体内使用";
        throw SemanticError(oss.str());
    }

    if (node.value) { node.value->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(ExprStmt& node)
{
    if (node.expression) { node.expression->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(BinaryExpr& node)
{
    if (node.left) node.left->accept(*this);
    if (node.right) node.right->accept(*this);
    return 0;
}

int SemanticAnalyzer::visit(CallExpr& node)
{
    Symbol* funcSym = symbolTable.lookupFunction(node.callee);
    if (!funcSym) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：调用了未定义的函数 '" << node.callee
            << "'";
        throw SemanticError(oss.str());
    }

    int argCount = static_cast<int>(node.args.size());
    if (funcSym->paramCount >= 0 && argCount != funcSym->paramCount) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：函数 '" << node.callee << "' 需要 "
            << funcSym->paramCount << " 个参数，但提供了 " << argCount << " 个";
        throw SemanticError(oss.str());
    }

    for (auto& arg : node.args) { arg->accept(*this); }
    return 0;
}

int SemanticAnalyzer::visit(NumberLiteral&) { return 0; }
int SemanticAnalyzer::visit(StringLiteral&) { return 0; }

int SemanticAnalyzer::visit(Identifier& node)
{
    Symbol* sym = symbolTable.lookup(node.name);
    if (!sym) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：未声明的变量 '" << node.name << "'";
        throw SemanticError(oss.str());
    }
    return 0;
}

int SemanticAnalyzer::visit(IndexExpr& node)
{
    Symbol* sym = symbolTable.lookup(node.arrayName);
    if (!sym) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：未声明的变量 '" << node.arrayName
            << "'";
        throw SemanticError(oss.str());
    }

    // 数组参数可持有数组句柄（引用语义），故这里只检查变量已声明；
    // 非数组变量被索引的运行时检查由 ARRGET/ARRSET 指令兜底（见 WAIT_FOR.md）
    if (node.index) { node.index->accept(*this); }
    return 0;
}
