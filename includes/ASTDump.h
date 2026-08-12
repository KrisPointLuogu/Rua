#pragma once
#include "AST.h"
#include <iostream>
#include <string>

#ifdef _DEBUG

/*
 * AST 调试输出 —— 仅在 _DEBUG 下可用。
 * 以带缩进的树状结构打印整棵抽象语法树，便于排查解析结果。
 */
namespace ASTDump {

// 词法分析器中的运算符令牌编号（与 Lexer.h 的令牌类型枚举对应）
const int TK_等号 = 21;
const int TK_加号 = 22;
const int TK_减号 = 23;
const int TK_乘号 = 24;
const int TK_除号 = 25;
const int TK_等于 = 26;
const int TK_不等于 = 27;
const int TK_大于 = 28;
const int TK_小于 = 29;
const int TK_模 = 30;
const int TK_乘方 = 31;
const int TK_整除 = 32;
const int TK_加等于 = 33;
const int TK_减等于 = 34;
const int TK_乘等于 = 35;
const int TK_除等于 = 36;
const int TK_乘方等于 = 37;
const int TK_模等于 = 38;
const int TK_整除等于 = 39;
const int TK_大于等于 = 44;
const int TK_小于等于 = 45;

inline const char* 运算符名(int op)
{
    switch (op) {
    case TK_等号: return "=";
    case TK_加号: return "+";
    case TK_减号: return "-";
    case TK_乘号: return "*";
    case TK_除号: return "/";
    case TK_等于: return "==";
    case TK_不等于: return "!=";
    case TK_大于: return ">";
    case TK_小于: return "<";
    case TK_模: return "%";
    case TK_乘方: return "**";
    case TK_整除: return "//";
    case TK_加等于: return "+=";
    case TK_减等于: return "-=";
    case TK_乘等于: return "*=";
    case TK_除等于: return "/=";
    case TK_乘方等于: return "**=";
    case TK_模等于: return "%=";
    case TK_整除等于: return "//=";
    case TK_大于等于: return ">=";
    case TK_小于等于: return "<=";
    default: return "?";
    }
}

class ASTDumpVisitor : public ASTVisitor {
  public:
    explicit ASTDumpVisitor(std::ostream& os) : os_(os) {}

    int visit(Program& node) override
    {
        indent();
        os_ << "Program\n";
        indent_++;
        for (auto& f : node.functions) f->accept(*this);
        for (auto& s : node.topLevelStmts) s->accept(*this);
        indent_--;
        return 0;
    }

    int visit(Function& node) override
    {
        indent();
        os_ << "Function " << node.name << "(";
        for (size_t i = 0; i < node.params.size(); ++i) {
            if (i) os_ << ", ";
            os_ << node.params[i];
        }
        os_ << ")\n";
        indent_++;
        if (node.body) node.body->accept(*this);
        indent_--;
        return 0;
    }

    int visit(Block& node) override
    {
        for (auto& stmt : node.statements) stmt->accept(*this);
        return 0;
    }

    int visit(VarDecl& node) override
    {
        indent();
        os_ << "VarDecl " << node.name;
        if (node.initializer) {
            os_ << " = ";
            node.initializer->accept(*this);
        }
        os_ << "\n";
        return 0;
    }

    int visit(ArrayDecl& node) override
    {
        indent();
        os_ << "ArrayDecl " << node.name << "[";
        for (size_t i = 0; i < node.sizes.size(); ++i) {
            if (i) os_ << "][";
            node.sizes[i]->accept(*this);
        }
        os_ << "]";
        if (node.initialValue) {
            os_ << " = ";
            node.initialValue->accept(*this);
        }
        os_ << "\n";
        return 0;
    }

    int visit(ArrayLiteral& node) override
    {
        os_ << "{";
        for (size_t i = 0; i < node.elements.size(); ++i) {
            if (i) os_ << ", ";
            node.elements[i]->accept(*this);
        }
        os_ << "}";
        return 0;
    }

    int visit(IfStmt& node) override
    {
        indent();
        os_ << "If: cond = ";
        if (node.condition) node.condition->accept(*this);
        os_ << "\n";
        indent_++;
        indent();
        os_ << "then:\n";
        indent_++;
        if (node.thenBranch) node.thenBranch->accept(*this);
        indent_--;
        if (node.elseBranch) {
            indent();
            os_ << "else:\n";
            indent_++;
            node.elseBranch->accept(*this);
            indent_--;
        }
        indent_--;
        return 0;
    }

    int visit(WhileStmt& node) override
    {
        indent();
        os_ << "While: cond = ";
        if (node.condition) node.condition->accept(*this);
        os_ << "\n";
        indent_++;
        if (node.body) node.body->accept(*this);
        indent_--;
        return 0;
    }

    int visit(ReturnStmt& node) override
    {
        indent();
        os_ << "Return";
        if (node.value) {
            os_ << ": ";
            node.value->accept(*this);
        }
        os_ << "\n";
        return 0;
    }

    int visit(ExprStmt& node) override
    {
        indent();
        if (node.expression) node.expression->accept(*this);
        os_ << "\n";
        return 0;
    }

    int visit(BinaryExpr& node) override
    {
        os_ << "(";
        if (node.left) node.left->accept(*this);
        os_ << " " << 运算符名(node.op) << " ";
        if (node.right) node.right->accept(*this);
        os_ << ")";
        return 0;
    }

    int visit(CallExpr& node) override
    {
        os_ << node.callee << "(";
        for (size_t i = 0; i < node.args.size(); ++i) {
            if (i) os_ << ", ";
            node.args[i]->accept(*this);
        }
        os_ << ")";
        return 0;
    }

    int visit(IndexExpr& node) override
    {
        if (node.base) node.base->accept(*this);
        os_ << "[";
        if (node.index) node.index->accept(*this);
        os_ << "]";
        return 0;
    }

    int visit(NumberLiteral& node) override { os_ << node.value; return 0; }

    int visit(StringLiteral& node) override
    {
        os_ << "\"" << node.value << "\"";
        return 0;
    }

    int visit(Identifier& node) override { os_ << node.name; return 0; }

  private:
    std::ostream& os_;
    int indent_ = 0;

    void indent()
    {
        for (int i = 0; i < indent_; ++i) os_ << "  ";
    }
};

} // namespace ASTDump

inline void dumpAST(Program& program)
{
    std::cout << "\n========== AST ==========\n";
    ASTDump::ASTDumpVisitor visitor(std::cout);
    program.accept(visitor);
    std::cout << "=========================\n";
}

#endif // _DEBUG
