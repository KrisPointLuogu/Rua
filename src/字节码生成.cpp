/*
 * 寄存器式字节码生成器 —— BytecodeGenerator
 *
 * 遍历 AST，为每个表达式分配虚拟寄存器，发出三地址码。
 *
 * 寄存器分配：
 *   参数 → r0..r(paramCount-1)
 *   局部变量 → r(paramCount)..r(paramCount+localCount-1)
 *   临时值 → 从 lastReg 开始依次分配，每句结束后重置
 *
 * 调用约定：
 *   通过 PUSH 传递参数，CALL 创建新帧，RET 将结果写回调用者 r0
 */

#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include "字节码.h"

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

namespace {
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
} // namespace

// ==================== BytecodeProgram ====================

int BytecodeProgram::addConstant(int value)
{
    for (size_t i = 0; i < constants.size(); i++) {
        if (constants[i] == value) return static_cast<int>(i);
    }
    constants.push_back(value);
    return static_cast<int>(constants.size() - 1);
}

int BytecodeProgram::addString(const string& value)
{
    for (size_t i = 0; i < strings.size(); i++) {
        if (strings[i] == value) return static_cast<int>(i);
    }
    strings.push_back(value);
    return static_cast<int>(strings.size() - 1);
}

int BytecodeProgram::addFunction(const string& name, int paramCount)
{
    FunctionInfo info;
    info.name = name;
    info.paramCount = paramCount;
    info.localCount = 0;
    info.regCount = 0;
    info.codeOffset = 0;
    functions.push_back(info);
    return static_cast<int>(functions.size() - 1);
}

void BytecodeProgram::emit(Opcode op, int rd, int rs1, int rs2, int extra)
{
    code.push_back(static_cast<uint8_t>(op));
    code.push_back(static_cast<uint8_t>(rd));
    code.push_back(static_cast<uint8_t>(rs1));
    code.push_back(static_cast<uint8_t>(rs2));
    code.push_back(static_cast<uint8_t>(extra & 0xFF));
    code.push_back(static_cast<uint8_t>((extra >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((extra >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((extra >> 24) & 0xFF));
}

int BytecodeProgram::getCodeSize() const
{
    return static_cast<int>(code.size());
}

void BytecodeProgram::patchOperand(int offset, int value)
{
    code[offset + 4] = static_cast<uint8_t>(value & 0xFF);
    code[offset + 5] = static_cast<uint8_t>((value >> 8) & 0xFF);
    code[offset + 6] = static_cast<uint8_t>((value >> 16) & 0xFF);
    code[offset + 7] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static const char* opcodeName(Opcode op)
{
    switch (op) {
    case Opcode::HALT: return "HALT";
    case Opcode::MOVI: return "MOVI";
    case Opcode::MOVS: return "MOVS";
    case Opcode::MOV: return "MOV";
    case Opcode::ADD: return "ADD";
    case Opcode::SUB: return "SUB";
    case Opcode::MUL: return "MUL";
    case Opcode::DIV: return "DIV";
    case Opcode::MOD: return "MOD";
    case Opcode::EQ: return "EQ";
    case Opcode::NE: return "NE";
    case Opcode::LT: return "LT";
    case Opcode::GT: return "GT";
    case Opcode::JMP: return "JMP";
    case Opcode::JIF: return "JIF";
    case Opcode::PUSH: return "PUSH";
    case Opcode::CALL: return "CALL";
    case Opcode::RET: return "RET";
    case Opcode::PRINT: return "PRINT";
    case Opcode::LE: return "LE";
    case Opcode::GE: return "GE";
    default: return "???";
    }
}

void BytecodeProgram::print() const
{
    std::cout << "====== 字节码程序 ======\n\n";
    std::cout << "--- 函数表 ---\n";
    for (size_t i = 0; i < functions.size(); i++) {
        const auto& f = functions[i];
        std::cout << "  [" << i << "] " << f.name << " (参数=" << f.paramCount
                  << ", 局部变量=" << f.localCount
                  << ", 寄存器数=" << f.regCount << ", 偏移=" << f.codeOffset
                  << ")\n";
    }

    std::cout << "\n--- 整数常量表 ---\n";
    for (size_t i = 0; i < constants.size(); i++)
        std::cout << "  [" << i << "] " << constants[i] << "\n";

    std::cout << "\n--- 字符串表 ---\n";
    for (size_t i = 0; i < strings.size(); i++)
        std::cout << "  [" << i << "] \"" << strings[i] << "\"\n";

    std::cout << "\n--- 字节码 ---\n";
    int ip = 0;
    while (ip < static_cast<int>(code.size())) {
        Opcode op = static_cast<Opcode>(code[ip]);
        uint8_t rd = code[ip + 1];
        uint8_t rs1 = code[ip + 2];
        uint8_t rs2 = code[ip + 3];
        int extra = static_cast<int>(code[ip + 4])
                    | (static_cast<int>(code[ip + 5]) << 8)
                    | (static_cast<int>(code[ip + 6]) << 16)
                    | (static_cast<int>(code[ip + 7]) << 24);

        std::cout << "  " << ip << ":\t" << opcodeName(op);

        switch (op) {
        case Opcode::MOVI:
            std::cout << " r" << (int)rd << ", #" << extra;
            break;
        case Opcode::MOVS:
            std::cout << " r" << (int)rd << ", \"" << strings[extra] << "\"";
            break;
        case Opcode::MOV:
            std::cout << " r" << (int)rd << ", r" << (int)rs1;
            break;
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
        case Opcode::EQ:
        case Opcode::NE:
        case Opcode::LT:
        case Opcode::GT:
        case Opcode::LE:
        case Opcode::GE:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                      << (int)rs2;
            break;
        case Opcode::JMP: std::cout << " " << (ip + 8 + extra); break;
        case Opcode::JIF:
            std::cout << " r" << (int)rs1 << ", " << (ip + 8 + extra);
            break;
        case Opcode::PUSH: std::cout << " r" << (int)rs1; break;
        case Opcode::CALL:
            if (extra >= 0 && extra < static_cast<int>(functions.size()))
                std::cout << " " << functions[extra].name;
            else
                std::cout << " " << extra;
            break;
        case Opcode::PRINT: std::cout << " r" << (int)rs1; break;
        default:
            if (extra) std::cout << " " << extra;
            break;
        }
        std::cout << "\n";
        ip += 8;
    }
    std::cout << "========================\n";
}

// ==================== BytecodeGenerator ====================

BytecodeGenerator::BytecodeGenerator(const SymbolTable& symbolTable)
  : symTable(&symbolTable)
{
}

BytecodeProgram BytecodeGenerator::generate(Program& ast)
{
    ast.accept(*this);
    return program;
}

void BytecodeGenerator::enterScope() { regMaps.emplace_back(); }

void BytecodeGenerator::exitScope() { regMaps.pop_back(); }

int BytecodeGenerator::allocReg(const string& name)
{
    int reg = totalReg;
    regMaps.back()[name] = reg;
    totalReg++;
    if (reg > maxReg) maxReg = reg;
    return reg;
}

int BytecodeGenerator::lookupReg(const string& name)
{
    for (int i = static_cast<int>(regMaps.size()) - 1; i >= 0; i--) {
        auto it = regMaps[i].find(name);
        if (it != regMaps[i].end()) return it->second;
    }
    return -1;
}

int BytecodeGenerator::allocTemp()
{
    int r = tempReg++;
    if (r > maxReg) maxReg = r;
    return r;
}

// ==================== Visitor 实现 ====================

int BytecodeGenerator::visit(Program& node)
{
    // 顶层语句合并到主函数
    if (!node.topLevelStmts.empty()) {
        bool hasExplicitMain = false;
        for (auto& f : node.functions) {
            if (f->name == "主函数") {
                hasExplicitMain = true;
                auto oldBody = std::move(f->body);
                f->body = make_unique<Block>();
                for (auto& stmt : node.topLevelStmts)
                    f->body->statements.push_back(std::move(stmt));
                for (auto& stmt : oldBody->statements)
                    f->body->statements.push_back(std::move(stmt));
                break;
            }
        }
        if (!hasExplicitMain) {
            auto mainFunc = make_unique<Function>();
            mainFunc->name = "主函数";
            mainFunc->body = make_unique<Block>();
            for (auto& stmt : node.topLevelStmts)
                mainFunc->body->statements.push_back(std::move(stmt));
            node.functions.push_back(std::move(mainFunc));
        }
        node.topLevelStmts.clear();
    }

    if (node.functions.empty()) {
        auto mainFunc = make_unique<Function>();
        mainFunc->name = "主函数";
        mainFunc->body = make_unique<Block>();
        node.functions.push_back(std::move(mainFunc));
    }

    for (auto& func : node.functions)
        program.addFunction(func->name, static_cast<int>(func->params.size()));

    int jmpPos = program.getCodeSize();
    program.emit(Opcode::JMP, 0, 0, 0, 0);

    for (auto& func : node.functions) {
        currentFunction = func->name;
        func->accept(*this);
    }

    int entryPos = program.getCodeSize();
    program.patchOperand(jmpPos, entryPos - (jmpPos + 8));

    // 查找主函数索引
    int mainIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == "主函数") {
            mainIdx = i;
            break;
        }
    }

    if (mainIdx < 0)
        throw std::runtime_error("内部错误：未找到入口函数 主函数");

    // 入口调用：PUSH 一个 r0 占位
    int r0Temp = allocTemp();
    program.emit(Opcode::MOVI, r0Temp, 0, 0, program.addConstant(0));
    program.emit(Opcode::PUSH, 0, r0Temp);
    program.emit(Opcode::CALL, 0, 0, 0, mainIdx);
    program.emit(Opcode::HALT);
    program.entryPoint = "主函数";
    return 0;
}

int BytecodeGenerator::visit(Function& node)
{
    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == node.name) {
            funcIdx = i;
            break;
        }
    }
    currentFuncIdx = funcIdx;

    auto& funcInfo = program.functions[funcIdx];
    funcInfo.codeOffset = program.getCodeSize();

    totalReg = 0;
    tempReg = 0;
    regMaps.clear();
    enterScope();

    // r0 保留给返回值，参数从 r1 开始分配
    totalReg = 1;
    tempReg = 1;
    maxReg = 0;
    for (const auto& param : node.params) allocReg(param);
    tempReg = totalReg;

    node.body->accept(*this);

    funcInfo.localCount = totalReg - static_cast<int>(node.params.size());
    funcInfo.regCount = maxReg + 1;

    exitScope();

    // 默认返回 0
    int retReg = allocTemp();
    program.emit(Opcode::MOVI, retReg, 0, 0, program.addConstant(0));
    program.emit(Opcode::MOV, 0, retReg);
    program.emit(Opcode::RET);
    return 0;
}

int BytecodeGenerator::visit(Block& node)
{
    enterScope();

    for (auto& stmt : node.statements) stmt->accept(*this);

    exitScope();
    return 0;
}

int BytecodeGenerator::visit(VarDecl& node)
{
    int slot = allocReg(node.name);

    if (node.initializer) {
        tempReg = totalReg;
        int valReg = node.initializer->accept(*this);
        program.emit(Opcode::MOV, slot, valReg);
    } else {
        tempReg = totalReg;
        int zeroReg = allocTemp();
        program.emit(Opcode::MOVI, zeroReg, 0, 0, program.addConstant(0));
        program.emit(Opcode::MOV, slot, zeroReg);
    }

    tempReg = totalReg;
    return slot;
}

int BytecodeGenerator::visit(IfStmt& node)
{
    int condReg = node.condition->accept(*this);
    tempReg = totalReg;

    int jifPos = program.getCodeSize();
    program.emit(Opcode::JIF, 0, condReg, 0, 0);

    node.thenBranch->accept(*this);

    if (node.elseBranch) {
        int jmpPos = program.getCodeSize();
        program.emit(Opcode::JMP, 0, 0, 0, 0);

        int elseStart = program.getCodeSize();
        program.patchOperand(jifPos, elseStart - (jifPos + 8));

        node.elseBranch->accept(*this);

        int afterElse = program.getCodeSize();
        program.patchOperand(jmpPos, afterElse - (jmpPos + 8));
    } else {
        int afterIf = program.getCodeSize();
        program.patchOperand(jifPos, afterIf - (jifPos + 8));
    }
    return 0;
}

int BytecodeGenerator::visit(WhileStmt& node)
{
    int loopStart = program.getCodeSize();

    int condReg = node.condition->accept(*this);
    tempReg = totalReg;

    int jifPos = program.getCodeSize();
    program.emit(Opcode::JIF, 0, condReg, 0, 0);

    node.body->accept(*this);

    program.emit(Opcode::JMP, 0, 0, 0, loopStart - (program.getCodeSize() + 8));

    int afterLoop = program.getCodeSize();
    program.patchOperand(jifPos, afterLoop - (jifPos + 8));
    return 0;
}

int BytecodeGenerator::visit(ReturnStmt& node)
{
    if (node.value) {
        int valReg = node.value->accept(*this);
        tempReg = totalReg;
        program.emit(Opcode::MOV, 0, valReg);
    } else {
        int zeroReg = allocTemp();
        program.emit(Opcode::MOVI, zeroReg, 0, 0, program.addConstant(0));
        program.emit(Opcode::MOV, 0, zeroReg);
    }

    program.emit(Opcode::RET);
    return 0;
}

int BytecodeGenerator::visit(ExprStmt& node)
{
    if (!node.expression) return 0;

    node.expression->accept(*this);
    tempReg = totalReg;
    return 0;
}

int BytecodeGenerator::visit(BinaryExpr& node)
{
    // 赋值
    if (node.op == TK_等号) {
        if (auto* id = dynamic_cast<Identifier*>(node.left.get())) {
            int slot = lookupReg(id->name);
            if (slot < 0) {
                std::cerr << "内部错误：未找到变量 '" << id->name
                          << "' 的寄存器" << std::endl;
                return 0;
            }
            int valReg = node.right->accept(*this);
            program.emit(Opcode::MOV, slot, valReg);
            int resultReg = allocTemp();
            program.emit(Opcode::MOV, resultReg, slot);
            return resultReg;
        }
        std::cerr << "内部错误：赋值左侧必须是变量" << std::endl;
        return 0;
    }

    int leftReg = node.left->accept(*this);
    int rightReg = node.right->accept(*this);
    int resultReg = allocTemp();

    switch (node.op) {
    case TK_加号:
        program.emit(Opcode::ADD, resultReg, leftReg, rightReg);
        break;
    case TK_减号:
        program.emit(Opcode::SUB, resultReg, leftReg, rightReg);
        break;
    case TK_乘号:
        program.emit(Opcode::MUL, resultReg, leftReg, rightReg);
        break;
    case TK_除号:
        program.emit(Opcode::DIV, resultReg, leftReg, rightReg);
        break;
    case TK_模: program.emit(Opcode::MOD, resultReg, leftReg, rightReg); break;
    case TK_等于: program.emit(Opcode::EQ, resultReg, leftReg, rightReg); break;
    case TK_不等于:
        program.emit(Opcode::NE, resultReg, leftReg, rightReg);
        break;
    case TK_大于: program.emit(Opcode::GT, resultReg, leftReg, rightReg); break;
    case TK_小于: program.emit(Opcode::LT, resultReg, leftReg, rightReg); break;
    case TK_大于等于:
        program.emit(Opcode::GE, resultReg, leftReg, rightReg);
        break;
    case TK_小于等于:
        program.emit(Opcode::LE, resultReg, leftReg, rightReg);
        break;
    }
    return resultReg;
}

int BytecodeGenerator::visit(CallExpr& node)
{
    if (node.callee == "喵叫") {
        // 喵叫实现为 PRINT 序列
        for (size_t i = 0; i < node.args.size(); i++) {
            int argReg = node.args[i]->accept(*this);
            program.emit(Opcode::PRINT, 0, argReg);

            if (i < node.args.size() - 1) {
                int spaceReg = allocTemp();
                program.emit(Opcode::MOVS, spaceReg, 0, 0,
                             program.addString(" "));
                program.emit(Opcode::PRINT, 0, spaceReg);
            }
        }
        int resultReg = allocTemp();
        program.emit(Opcode::MOVI, resultReg, 0, 0, program.addConstant(0));
        return resultReg;
    }

    // 查找函数索引
    int funcIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == node.callee) {
            funcIdx = i;
            break;
        }
    }

    if (funcIdx < 0) {
        std::cerr << "内部错误：未找到函数 '" << node.callee << "'"
                  << std::endl;
        int resultReg = allocTemp();
        program.emit(Opcode::MOVI, resultReg, 0, 0, program.addConstant(0));
        return resultReg;
    }

    // 计算参数并 PUSH
    // 先 PUSH r0 占位（保留给返回值）
    int r0Dummy = allocTemp();
    program.emit(Opcode::MOVI, r0Dummy, 0, 0, program.addConstant(0));
    program.emit(Opcode::PUSH, 0, r0Dummy);

    for (auto& arg : node.args) {
        int argReg = arg->accept(*this);
        program.emit(Opcode::PUSH, 0, argReg);
    }
    tempReg = maxReg + 1;

    program.emit(Opcode::CALL, 0, 0, 0, funcIdx);

    // 返回值在 r0，保存到临时寄存器
    int resultReg = allocTemp();
    program.emit(Opcode::MOV, resultReg, 0);
    return resultReg;
}

int BytecodeGenerator::visit(NumberLiteral& node)
{
    int rd = allocTemp();
    program.emit(Opcode::MOVI, rd, 0, 0, program.addConstant(node.value));
    return rd;
}

int BytecodeGenerator::visit(StringLiteral& node)
{
    int rd = allocTemp();
    program.emit(Opcode::MOVS, rd, 0, 0, program.addString(node.value));
    return rd;
}

int BytecodeGenerator::visit(Identifier& node)
{
    int slot = lookupReg(node.name);
    if (slot >= 0) {
        // 返回变量所在的寄存器
        return slot;
    }
    std::cerr << "内部错误：未找到变量 '" << node.name << "'" << std::endl;
    int rd = allocTemp();
    program.emit(Opcode::MOVI, rd, 0, 0, program.addConstant(0));
    return rd;
}
