#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <memory>
#include "语法树.h"
#include "语义分析.h"

//
// 字节码生成 —— 将 AST 编译为栈式虚拟机字节码
//
// 指令集：
//   0x00  HALT    程序终止
//   0x01  ICONST  压入整数常量（操作数：常量表索引）
//   0x02  SCONST  压入字符串常量（操作数：字符串表索引）
//   0x03  ADD     弹出两值相加，压入结果
//   0x04  SUB     弹出两值相减
//   0x05  MUL     弹出两值相乘
//   0x06  DIV     弹出两值相除
//   0x07  EQ      弹出两值比较相等，压入 0 或 1
//   0x08  JMP     无条件跳转（操作数：相对偏移量）
//   0x09  JIF     弹出值，若为 0 则跳转（操作数：相对偏移量）
//   0x0A  LOAD    加载局部变量（操作数：槽位索引）
//   0x0B  STORE   弹出值存入局部变量（操作数：槽位索引）
//   0x0C  CALL    调用函数（操作数：函数表索引）
//   0x0D  PRINT   弹出栈顶值并输出
//   0x0E  RET     函数返回
//   0x0F  POP     弹出栈顶值并丢弃
//   0x10  LT      小于比较
//   0x11  GT      大于比较
//   0x12  NEQ     不等于比较
//   0x13  MOD     取模
//

enum class Opcode : uint8_t
{
    HALT = 0x00,
    ICONST = 0x01,
    SCONST = 0x02,
    ADD = 0x03,
    SUB = 0x04,
    MUL = 0x05,
    DIV = 0x06,
    EQ = 0x07,
    JMP = 0x08,
    JIF = 0x09,
    LOAD = 0x0A,
    STORE = 0x0B,
    CALL = 0x0C,
    PRINT = 0x0D,
    RET = 0x0E,
    POP = 0x0F,
    LT = 0x10,
    GT = 0x11,
    NEQ = 0x12,
    MOD = 0x13,
#ifdef OPTIMIZATION
    SUB_ICONST = 0x14,
    GT_ICONST = 0x15,
#endif
};

inline bool hasOperand(Opcode op)
{
    switch (op)
    {
    case Opcode::ICONST:
    case Opcode::SCONST:
    case Opcode::JMP:
    case Opcode::JIF:
    case Opcode::LOAD:
    case Opcode::STORE:
    case Opcode::CALL:
#ifdef OPTIMIZATION
    case Opcode::SUB_ICONST:
    case Opcode::GT_ICONST:
#endif
        return true;
    default:
        return false;
    }
}

inline int instructionSize(Opcode op)
{
    return hasOperand(op) ? 5 : 1;
}

struct FunctionInfo
{
    std::string name;
    int paramCount;
    int localCount;
    int codeOffset;
};

class BytecodeProgram
{
public:
    std::vector<uint8_t> code;
    std::vector<int> constants;
    std::vector<std::string> strings;
    std::vector<FunctionInfo> functions;
    std::string entryPoint;

    int addConstant(int value);
    int addString(const std::string &value);
    int addFunction(const std::string &name, int paramCount);
    void emit(Opcode op);
    void emit(Opcode op, int operand);
    int getCodeSize() const;
    void patchOperand(int offset, int value);
    void print() const;
};

class BytecodeGenerator : public ASTVisitor
{
private:
    BytecodeProgram program;
    const SymbolTable *symTable;
    std::vector<std::unordered_map<std::string, int>> slotMaps;
    int totalSlotCount = 0;
    std::string currentFunction;

public:
    explicit BytecodeGenerator(const SymbolTable &symbolTable);
    BytecodeProgram generate(Program &ast);

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

private:
    void enterScope();
    void exitScope();
    int allocateSlot(const std::string &name);
    int lookupSlot(const std::string &name);

#ifdef OPTIMIZATION
    bool canConstantFold(int funcIdx, std::set<int> &visited);
    bool tryConstantFold(int funcIdx, const std::vector<int> &constArgs);
#endif
};
