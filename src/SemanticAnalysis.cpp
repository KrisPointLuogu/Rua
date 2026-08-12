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

namespace {
// 算术运算符令牌值（与 Parser 对齐）
const int TK_加号 = 22;
const int TK_减号 = 23;
const int TK_乘号 = 24;
const int TK_除号 = 25;
const int TK_模 = 30;
} // namespace

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

void SymbolTable::declareVariable(const string& name, int line, int arrayRank)
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
    sym.arrayRank = arrayRank;
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
        // 参数无类型标注，视为「未知」句柄（-1），索引深度由运行时决定
        symbolTable.declareVariable(param, node.line, -1);
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
    if (node.sizes.empty()) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：数组 '" << node.name
            << "' 声明至少需要一个维度";
        throw SemanticError(oss.str());
    }

    int rank = static_cast<int>(node.sizes.size());
    vector<int> constDims(rank, -1); // -1 表示动态长度

    for (auto& dim : node.sizes) {
        dim->accept(*this); // 维度表达式的声明检查

        int 常量值;
        if (折叠常量表达式(dim.get(), 常量值)) {
            if (常量值 <= 0) {
                std::ostringstream oss;
                oss << "第 " << node.line << " 行：数组 '" << node.name
                    << "' 的长度必须为正数";
                throw SemanticError(oss.str());
            }
        }
    }

    // 记录各维常量值（供初始化校验使用）
    for (int i = 0; i < rank; i++) {
        int 常量值;
        if (折叠常量表达式(node.sizes[i].get(), 常量值)) {
            constDims[i] = 常量值;
        }
    }

    symbolTable.declareVariable(node.name, node.line, rank);

    if (node.initialValue) {
        校验数组初始化(node.initialValue.get(), rank, constDims);
    }
    return 0;
}

int SemanticAnalyzer::visit(ArrayLiteral& node)
{
    for (auto& el : node.elements) { el->accept(*this); }
    return 0;
}

bool SemanticAnalyzer::折叠常量表达式(const ASTNode* node, int& out)
{
    if (node->getType() == NodeType::NUMBER_LITERAL) {
        out = static_cast<const NumberLiteral*>(node)->value;
        return true;
    }
    if (node->getType() == NodeType::BINARY_EXPR) {
        const auto* b = static_cast<const BinaryExpr*>(node);
        int 左, 右;
        if (!折叠常量表达式(b->left.get(), 左) || !折叠常量表达式(b->right.get(), 右))
            return false;
        switch (b->op) {
        case TK_加号: out = 左 + 右; return true;
        case TK_减号: out = 左 - 右; return true;
        case TK_乘号: out = 左 * 右; return true;
        case TK_除号:
            if (右 == 0) return false;
            out = 左 / 右;
            return true;
        case TK_模:
            if (右 == 0) return false;
            out = 左 % 右;
            return true;
        default: return false;
        }
    }
    return false;
}

void SemanticAnalyzer::校验数组初始化(ASTNode* init, int rank,
                                    const vector<int>& constDims)
{
    auto* lit = dynamic_cast<ArrayLiteral*>(init);
    if (!lit) {
        std::ostringstream oss;
        oss << "第 " << init->line << " 行：数组初始化必须是 '{...}'";
        throw SemanticError(oss.str());
    }

    // 单个标量 → 广播，任意维度合法
    if (lit->elements.size() == 1
        && !dynamic_cast<ArrayLiteral*>(lit->elements[0].get())) {
        lit->elements[0]->accept(*this);
        return;
    }

    bool 有嵌套 = false;
    bool 有标量 = false;
    for (auto& el : lit->elements) {
        if (dynamic_cast<ArrayLiteral*>(el.get()))
            有嵌套 = true;
        else {
            有标量 = true;
            el->accept(*this);
        }
    }

    if (有嵌套 && 有标量) {
        std::ostringstream oss;
        oss << "第 " << init->line
            << " 行：数组初始化列表不能同时包含标量与嵌套列表";
        throw SemanticError(oss.str());
    }

    if (有嵌套) {
        // 嵌套定位需要静态行距：除首维外其余维度必须为编译期常量
        for (int i = 1; i < rank; i++) {
            if (constDims[i] < 0) {
                std::ostringstream oss;
                oss << "第 " << init->line
                    << " 行：嵌套初始化列表的维度须为编译期常量（除首维外）";
                throw SemanticError(oss.str());
            }
        }
        校验嵌套列表(lit, 0, rank, constDims);
        return;
    }

    // 纯标量列表 → row-major 平铺，元素数不得超过总长度
    if (rank == 1 && constDims[0] >= 0
        && static_cast<int>(lit->elements.size()) > constDims[0]) {
        std::ostringstream oss;
        oss << "第 " << init->line << " 行：数组初始化列表过长（"
            << lit->elements.size() << " 个元素，长度为 " << constDims[0]
            << "）";
        throw SemanticError(oss.str());
    }
    if (rank > 1) {
        long long 总长 = 1;
        bool 已知 = true;
        for (auto d : constDims) {
            if (d < 0)
                已知 = false;
            else
                总长 *= d;
        }
        if (已知 && static_cast<long long>(lit->elements.size()) > 总长) {
            std::ostringstream oss;
            oss << "第 " << init->line << " 行：数组初始化列表过长（"
                << lit->elements.size() << " 个元素，总长度为 " << 总长
                << "）";
            throw SemanticError(oss.str());
        }
    }
}

void SemanticAnalyzer::校验嵌套列表(ArrayLiteral* lit, int level, int rank,
                                   const vector<int>& constDims)
{
    if (level >= rank) {
        std::ostringstream oss;
        oss << "第 " << lit->line << " 行：数组初始化列表嵌套层数超过数组维度";
        throw SemanticError(oss.str());
    }

    if (level == rank - 1) {
        // 最内层：元素必须全是标量
        for (auto& el : lit->elements) {
            if (dynamic_cast<ArrayLiteral*>(el.get())) {
                std::ostringstream oss;
                oss << "第 " << lit->line
                    << " 行：数组初始化列表嵌套层数超过数组维度";
                throw SemanticError(oss.str());
            }
            el->accept(*this);
        }
        return;
    }

    if (constDims[level] >= 0
        && static_cast<int>(lit->elements.size()) > constDims[level]) {
        std::ostringstream oss;
        oss << "第 " << lit->line << " 行：数组初始化列表过长（"
            << lit->elements.size() << " 个元素，长度为 " << constDims[level]
            << "）";
        throw SemanticError(oss.str());
    }

    for (auto& el : lit->elements) {
        auto* sub = dynamic_cast<ArrayLiteral*>(el.get());
        if (!sub) {
            std::ostringstream oss;
            oss << "第 " << lit->line
                << " 行：数组初始化列表嵌套层级不完整";
            throw SemanticError(oss.str());
        }
        校验嵌套列表(sub, level + 1, rank, constDims);
    }
}

void SemanticAnalyzer::校验索引链(IndexExpr& node)
{
    // 收集整条索引链的下标（源顺序由内到外）并求值，供声明检查
    vector<IndexExpr*> chain;
    IndexExpr* cur = &node;
    while (true) {
        chain.push_back(cur);
        if (auto* inner = dynamic_cast<IndexExpr*>(cur->base.get()))
            cur = inner;
        else
            break;
    }

    // 下标按源顺序求值（保持副作用顺序）
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if ((*it)->index) (*it)->index->accept(*this);
    }

    // 基表达式：标识符直接查符号表；否则递归检查其内部
    auto* base = chain.back()->base.get();
    auto* id = dynamic_cast<Identifier*>(base);
    if (!id) {
        base->accept(*this);
        return;
    }

    Symbol* sym = symbolTable.lookup(id->name);
    if (!sym) {
        std::ostringstream oss;
        oss << "第 " << id->line << " 行：未声明的变量 '" << id->name << "'";
        throw SemanticError(oss.str());
    }

    int depth = static_cast<int>(chain.size());
    if (sym->arrayRank == 0) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：索引的目标不是数组 '" << id->name
            << "'";
        throw SemanticError(oss.str());
    }
    if (sym->arrayRank >= 2 && depth != sym->arrayRank) {
        std::ostringstream oss;
        oss << "第 " << node.line << " 行：数组 '" << id->name << "' 需要 "
            << sym->arrayRank << " 个下标，但提供了 " << depth << " 个";
        throw SemanticError(oss.str());
    }
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
    校验索引链(node);
    return 0;
}
