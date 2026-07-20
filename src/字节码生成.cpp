/*
 * 字节码生成器 —— BytecodeGenerator
 *
 * 遍历已通过语义分析的 AST，生成栈式虚拟机字节码。
 *
 * 调用约定：
 *   - 调用前，参数已压入值栈
 *   - CALL 指令保存返回地址，跳转到函数代码
 *   - RET 弹出局部变量和参数，保留返回值
 *
 * 变量槽位分配：
 *   - 先参数，后局部变量（按声明顺序）
 *   - 块级作用域中的变量依次分配槽位
 */

#include "字节码.h"
#include <iostream>
#include <sstream>
#include <cassert>

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

// 令牌类型常量
namespace
{
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
    const int TK_大于等于 = 44;
    const int TK_小于等于 = 45;
}

// ==================== BytecodeProgram ====================

int BytecodeProgram::addConstant(int value)
{
    for (size_t i = 0; i < constants.size(); i++)
    {
        if (constants[i] == value)
            return static_cast<int>(i);
    }
    constants.push_back(value);
    return static_cast<int>(constants.size() - 1);
}

int BytecodeProgram::addString(const string &value)
{
    for (size_t i = 0; i < strings.size(); i++)
    {
        if (strings[i] == value)
            return static_cast<int>(i);
    }
    strings.push_back(value);
    return static_cast<int>(strings.size() - 1);
}

int BytecodeProgram::addFunction(const string &name, int paramCount)
{
    FunctionInfo info;
    info.name = name;
    info.paramCount = paramCount;
    info.localCount = 0;
    info.codeOffset = getCodeSize();
    functions.push_back(info);
    return static_cast<int>(functions.size() - 1);
}

void BytecodeProgram::emit(Opcode op)
{
    code.push_back(static_cast<uint8_t>(op));
}

void BytecodeProgram::emit(Opcode op, int operand)
{
    code.push_back(static_cast<uint8_t>(op));
    code.push_back(static_cast<uint8_t>(operand & 0xFF));
    code.push_back(static_cast<uint8_t>((operand >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((operand >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((operand >> 24) & 0xFF));
}

int BytecodeProgram::getCodeSize() const
{
    return static_cast<int>(code.size());
}

void BytecodeProgram::patchOperand(int offset, int value)
{
    code[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    code[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    code[offset + 3] = static_cast<uint8_t>((value >> 16) & 0xFF);
    code[offset + 4] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static const char *opcodeName(Opcode op)
{
    switch (op)
    {
    case Opcode::HALT:
        return "HALT";
    case Opcode::ICONST:
        return "ICONST";
    case Opcode::SCONST:
        return "SCONST";
    case Opcode::ADD:
        return "ADD";
    case Opcode::SUB:
        return "SUB";
    case Opcode::MUL:
        return "MUL";
    case Opcode::DIV:
        return "DIV";
    case Opcode::EQ:
        return "EQ";
    case Opcode::JMP:
        return "JMP";
    case Opcode::JIF:
        return "JIF";
    case Opcode::LOAD:
        return "LOAD";
    case Opcode::STORE:
        return "STORE";
    case Opcode::CALL:
        return "CALL";
    case Opcode::PRINT:
        return "PRINT";
    case Opcode::RET:
        return "RET";
    case Opcode::POP:
        return "POP";
    case Opcode::LT:
        return "LT";
    case Opcode::GT:
        return "GT";
    case Opcode::NEQ:
        return "NEQ";
    case Opcode::MOD:
        return "MOD";
    default:
        return "???";
    }
}

void BytecodeProgram::print() const
{
    std::cout << "====== 字节码程序 ======\n\n";
    std::cout << "--- 函数表 ---\n";
    for (size_t i = 0; i < functions.size(); i++)
    {
        const auto &f = functions[i];
        std::cout << "  [" << i << "] " << f.name
                  << " (参数=" << f.paramCount
                  << ", 局部变量=" << f.localCount
                  << ", 偏移=" << f.codeOffset << ")\n";
    }

    std::cout << "\n--- 整数常量表 ---\n";
    for (size_t i = 0; i < constants.size(); i++)
        std::cout << "  [" << i << "] " << constants[i] << "\n";

    std::cout << "\n--- 字符串表 ---\n";
    for (size_t i = 0; i < strings.size(); i++)
        std::cout << "  [" << i << "] \"" << strings[i] << "\"\n";

    std::cout << "\n--- 字节码 ---\n";
    int ip = 0;
    while (ip < static_cast<int>(code.size()))
    {
        Opcode op = static_cast<Opcode>(code[ip]);
        std::cout << "  " << ip << ":\t" << opcodeName(op);

        if (hasOperand(op))
        {
            int operand = static_cast<int>(code[ip + 1]) | (static_cast<int>(code[ip + 2]) << 8) | (static_cast<int>(code[ip + 3]) << 16) | (static_cast<int>(code[ip + 4]) << 24);
            std::cout << " " << operand;
            ip += 5;
        }
        else
        {
            ip += 1;
        }
        std::cout << "\n";
    }
    std::cout << "========================\n";
}

// ==================== BytecodeGenerator ====================

BytecodeGenerator::BytecodeGenerator(const SymbolTable &symbolTable)
    : symTable(&symbolTable) {}

BytecodeProgram BytecodeGenerator::generate(Program &ast)
{
    ast.accept(*this);
    return program;
}

void BytecodeGenerator::enterScope()
{
    slotMaps.emplace_back();
}

void BytecodeGenerator::exitScope()
{
    slotMaps.pop_back();
}

int BytecodeGenerator::allocateSlot(const string &name)
{
    int slot = totalSlotCount;
    slotMaps.back()[name] = slot;
    totalSlotCount++;
    return slot;
}

int BytecodeGenerator::lookupSlot(const string &name)
{
    for (int i = static_cast<int>(slotMaps.size()) - 1; i >= 0; i--)
    {
        auto it = slotMaps[i].find(name);
        if (it != slotMaps[i].end())
            return it->second;
    }
    return -1;
}

// ==================== Visitor 实现 ====================

void BytecodeGenerator::visit(Program &node)
{
    // 顶层语句 → 隐式 主函数
    if (!node.topLevelStmts.empty())
    {
        bool hasExplicitMain = false;
        for (auto &f : node.functions)
        {
            if (f->name == "主函数")
            {
                hasExplicitMain = true;
                auto oldBody = std::move(f->body);
                f->body = make_unique<Block>();
                for (auto &stmt : node.topLevelStmts)
                    f->body->statements.push_back(std::move(stmt));
                for (auto &stmt : oldBody->statements)
                    f->body->statements.push_back(std::move(stmt));
                break;
            }
        }
        if (!hasExplicitMain)
        {
            auto mainFunc = make_unique<Function>();
            mainFunc->name = "主函数";
            mainFunc->body = make_unique<Block>();
            for (auto &stmt : node.topLevelStmts)
                mainFunc->body->statements.push_back(std::move(stmt));
            node.functions.push_back(std::move(mainFunc));
        }
        node.topLevelStmts.clear();
    }

    if (node.functions.empty())
    {
        auto mainFunc = make_unique<Function>();
        mainFunc->name = "主函数";
        mainFunc->body = make_unique<Block>();
        node.functions.push_back(std::move(mainFunc));
    }

    for (auto &func : node.functions)
        program.addFunction(func->name, static_cast<int>(func->params.size()));

    int jmpPos = program.getCodeSize();
    program.emit(Opcode::JMP, 0);

    for (auto &func : node.functions)
    {
        currentFunction = func->name;
        func->accept(*this);
    }

    int entryPos = program.getCodeSize();
    program.patchOperand(jmpPos, entryPos - (jmpPos + 5));

    int mainIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++)
    {
        if (program.functions[i].name == "主函数")
        {
            mainIdx = i;
            break;
        }
    }

    if (mainIdx < 0)
        throw std::runtime_error("内部错误：未找到入口函数 主函数");

    const FunctionInfo &mainFunc = program.functions[mainIdx];
    for (int i = 0; i < mainFunc.paramCount; i++)
        program.emit(Opcode::ICONST, program.addConstant(0));

    program.emit(Opcode::CALL, mainIdx);
    program.emit(Opcode::HALT);
    program.entryPoint = "主函数";
}

void BytecodeGenerator::visit(Function &node)
{
    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++)
    {
        if (program.functions[i].name == node.name)
        {
            funcIdx = i;
            break;
        }
    }

    auto &funcInfo = program.functions[funcIdx];
    funcInfo.codeOffset = program.getCodeSize();

    totalSlotCount = 0;
    slotMaps.clear();
    enterScope();

    for (const auto &param : node.params)
        allocateSlot(param);

    node.body->accept(*this);

    funcInfo.localCount = totalSlotCount - static_cast<int>(node.params.size());
    exitScope();

    program.emit(Opcode::ICONST, program.addConstant(0));
    program.emit(Opcode::RET);
}

void BytecodeGenerator::visit(Block &node)
{
    enterScope();

    for (auto &stmt : node.statements)
        stmt->accept(*this);

    exitScope();
}

void BytecodeGenerator::visit(VarDecl &node)
{
    int slot = allocateSlot(node.name);

    if (node.initializer)
        node.initializer->accept(*this);
    else
        program.emit(Opcode::ICONST, program.addConstant(0));

    program.emit(Opcode::STORE, slot);
}

void BytecodeGenerator::visit(IfStmt &node)
{
    node.condition->accept(*this);

    int jifPos = program.getCodeSize();
    program.emit(Opcode::JIF, 0);

    node.thenBranch->accept(*this);

    if (node.elseBranch)
    {
        int jmpPos = program.getCodeSize();
        program.emit(Opcode::JMP, 0);

        int elseStart = program.getCodeSize();
        program.patchOperand(jifPos, elseStart - (jifPos + 5));

        node.elseBranch->accept(*this);

        int afterElse = program.getCodeSize();
        program.patchOperand(jmpPos, afterElse - (jmpPos + 5));
    }
    else
    {
        int afterIf = program.getCodeSize();
        program.patchOperand(jifPos, afterIf - (jifPos + 5));
    }
}

void BytecodeGenerator::visit(WhileStmt &node)
{
    int loopStart = program.getCodeSize();

    node.condition->accept(*this);

    int jifPos = program.getCodeSize();
    program.emit(Opcode::JIF, 0);

    node.body->accept(*this);

    // 跳回循环开始
    program.emit(Opcode::JMP, loopStart - (program.getCodeSize() + 5));

    int afterLoop = program.getCodeSize();
    program.patchOperand(jifPos, afterLoop - (jifPos + 5));
}

void BytecodeGenerator::visit(ReturnStmt &node)
{
    if (node.value)
        node.value->accept(*this);
    else
        program.emit(Opcode::ICONST, program.addConstant(0));

    program.emit(Opcode::RET);
}

void BytecodeGenerator::visit(ExprStmt &node)
{
    if (!node.expression)
        return;

    bool isMiaoCall = false;
    if (auto *call = dynamic_cast<CallExpr *>(node.expression.get()))
    {
        if (call->callee == "喵叫")
            isMiaoCall = true;
    }

    node.expression->accept(*this);

    if (!isMiaoCall)
        program.emit(Opcode::POP);
}

void BytecodeGenerator::visit(BinaryExpr &node)
{
    // 赋值：右值求值 → STORE
    if (node.op == TK_等号)
    {
        if (auto *id = dynamic_cast<Identifier *>(node.left.get()))
        {
            int slot = lookupSlot(id->name);
            if (slot < 0)
            {
                std::cerr << "内部错误：未找到变量 '" << id->name << "' 的槽位" << std::endl;
                return;
            }
            node.right->accept(*this);
            program.emit(Opcode::STORE, slot);
            // 保留赋值结果在栈上
            program.emit(Opcode::LOAD, slot);
            return;
        }
        std::cerr << "内部错误：赋值左侧必须是变量" << std::endl;
        return;
    }

    if (node.left)
        node.left->accept(*this);
    if (node.right)
        node.right->accept(*this);

    switch (node.op)
    {
    case TK_加号:
        program.emit(Opcode::ADD);
        break;
    case TK_减号:
        program.emit(Opcode::SUB);
        break;
    case TK_乘号:
        program.emit(Opcode::MUL);
        break;
    case TK_除号:
        program.emit(Opcode::DIV);
        break;
    case TK_模:
        program.emit(Opcode::MOD);
        break;
    case TK_等于:
        program.emit(Opcode::EQ);
        break;
    case TK_不等于:
        program.emit(Opcode::NEQ);
        break;
    case TK_大于:
        program.emit(Opcode::GT);
        break;
    case TK_小于:
        program.emit(Opcode::LT);
        break;
    case TK_大于等于:
        program.emit(Opcode::LT);
        program.emit(Opcode::ICONST, program.addConstant(0));
        program.emit(Opcode::EQ);
        break;
    case TK_小于等于:
        program.emit(Opcode::GT);
        program.emit(Opcode::ICONST, program.addConstant(0));
        program.emit(Opcode::EQ);
        break;
    }
}

void BytecodeGenerator::visit(CallExpr &node)
{
    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++)
    {
        if (program.functions[i].name == node.callee)
        {
            funcIdx = i;
            break;
        }
    }

    if (node.callee == "喵叫")
    {
        for (int i = static_cast<int>(node.args.size()) - 1; i >= 0; i--)
            node.args[i]->accept(*this);

        for (size_t i = 0; i < node.args.size(); i++)
        {
            program.emit(Opcode::PRINT);
            if (i < node.args.size() - 1)
            {
                program.emit(Opcode::SCONST, program.addString(" "));
                program.emit(Opcode::PRINT);
            }
        }
    }
    else
    {
        for (auto &arg : node.args)
            arg->accept(*this);

        program.emit(Opcode::CALL, funcIdx);
    }
}

void BytecodeGenerator::visit(NumberLiteral &node)
{
    program.emit(Opcode::ICONST, program.addConstant(node.value));
}

void BytecodeGenerator::visit(StringLiteral &node)
{
    program.emit(Opcode::SCONST, program.addString(node.value));
}

void BytecodeGenerator::visit(Identifier &node)
{
    int slot = lookupSlot(node.name);
    if (slot >= 0)
        program.emit(Opcode::LOAD, slot);
}
