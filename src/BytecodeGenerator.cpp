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
#include "Bytecode.h"

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
    case Opcode::ARRNEW: return "ARRNEW";
    case Opcode::ARRGET: return "ARRGET";
    case Opcode::ARRSET: return "ARRSET";
    default: return "???";
    }
}

void BytecodeProgram::print() const
{
    print(std::cout);
}

void BytecodeProgram::print(std::ostream& out) const
{
    out << "====== 字节码程序 ======\n\n";
    out << "--- 函数表 ---\n";
    for (size_t i = 0; i < functions.size(); i++) {
        const auto& f = functions[i];
        out << "  [" << i << "] " << f.name << " (参数=" << f.paramCount
            << ", 局部变量=" << f.localCount
            << ", 寄存器数=" << f.regCount << ", 偏移=" << f.codeOffset
            << ")\n";
    }

    out << "\n--- 整数常量表 ---\n";
    for (size_t i = 0; i < constants.size(); i++)
        out << "  [" << i << "] " << constants[i] << "\n";

    out << "\n--- 字符串表 ---\n";
    for (size_t i = 0; i < strings.size(); i++)
        out << "  [" << i << "] \"" << strings[i] << "\"\n";

    out << "\n--- 字节码 ---\n";
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

        out << "  " << ip << ":\t" << opcodeName(op);

        switch (op) {
        case Opcode::MOVI:
            out << " r" << (int)rd << ", #" << extra;
            break;
        case Opcode::MOVS:
            out << " r" << (int)rd << ", \"" << strings[extra] << "\"";
            break;
        case Opcode::MOV:
            out << " r" << (int)rd << ", r" << (int)rs1;
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
            out << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                << (int)rs2;
            break;
        case Opcode::JMP: out << " " << (ip + 8 + extra); break;
        case Opcode::JIF:
            out << " r" << (int)rs1 << ", " << (ip + 8 + extra);
            break;
        case Opcode::PUSH: out << " r" << (int)rs1; break;
        case Opcode::CALL:
            if (extra >= 0 && extra < static_cast<int>(functions.size()))
                out << " " << functions[extra].name;
            else
                out << " " << extra;
            break;
        case Opcode::PRINT: out << " r" << (int)rs1; break;
        case Opcode::ARRNEW:
            out << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                << (int)rs2;
            break;
        case Opcode::ARRGET:
            out << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                << (int)rs2;
            break;
        case Opcode::ARRSET:
            out << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                << (int)rs2;
            break;
        default:
            if (extra) out << " " << extra;
            break;
        }
        out << "\n";
        ip += 8;
    }
    out << "========================\n";
}

// ==================== .rab 二进制序列化 ====================
// 格式见 docs/rab-format.md

namespace {

// 小端写入 i32
void 写入i32(std::string& buf, int32_t v)
{
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void 写入字符串(std::string& buf, const string& s)
{
    写入i32(buf, static_cast<int32_t>(s.size()));
    buf.append(s);
}

// 小端读取 i32（不越界检查，调用方保证长度）
int32_t 读取i32(const std::string& buf, size_t& pos)
{
    int32_t v = static_cast<uint8_t>(buf[pos])
                | (static_cast<uint8_t>(buf[pos + 1]) << 8)
                | (static_cast<uint8_t>(buf[pos + 2]) << 16)
                | (static_cast<uint8_t>(buf[pos + 3]) << 24);
    pos += 4;
    return v;
}

bool 读取字符串(const std::string& buf, size_t& pos, string& out)
{
    int32_t len = 读取i32(buf, pos);
    if (len < 0 || pos + static_cast<size_t>(len) > buf.size()) return false;
    out.assign(buf.data() + pos, static_cast<size_t>(len));
    pos += static_cast<size_t>(len);
    return true;
}

const char RAB_MAGIC[4] = { 'R', 'U', 'A', '\0' };
const int32_t RAB_VERSION = 1;

} // namespace

bool BytecodeProgram::save(const std::string& path, string& 错误信息) const
{
    std::string buf;
    buf.reserve(64 + code.size() + strings.size() * 16);

    buf.append(RAB_MAGIC, 4);
    写入i32(buf, RAB_VERSION);

    写入i32(buf, static_cast<int32_t>(constants.size()));
    for (int c : constants) 写入i32(buf, c);

    写入i32(buf, static_cast<int32_t>(strings.size()));
    for (const auto& s : strings) 写入字符串(buf, s);

    写入i32(buf, static_cast<int32_t>(functions.size()));
    for (const auto& f : functions) {
        写入字符串(buf, f.name);
        写入i32(buf, f.paramCount);
        写入i32(buf, f.localCount);
        写入i32(buf, f.regCount);
        写入i32(buf, f.codeOffset);
    }

    写入字符串(buf, entryPoint);

    写入i32(buf, static_cast<int32_t>(code.size()));
    buf.append(reinterpret_cast<const char*>(code.data()), code.size());

    if (!arch::writeFile(path, buf)) {
        错误信息 = "无法写入文件 '" + path + "'";
        return false;
    }
    return true;
}

bool BytecodeProgram::load(const std::string& path, string& 错误信息)
{
    std::string buf;
    if (!arch::readFile(path, buf)) {
        错误信息 = "无法打开文件 '" + path + "'";
        return false;
    }

    size_t pos = 0;
    if (buf.size() < 8 || buf.compare(0, 4, RAB_MAGIC, 4) != 0) {
        错误信息 = "不是有效的 .rab 文件（magic 不匹配）";
        return false;
    }
    pos = 4;
    int32_t version = 读取i32(buf, pos);
    if (version != RAB_VERSION) {
        错误信息 = "不支持的 .rab 版本: " + std::to_string(version);
        return false;
    }

    constants.clear();
    strings.clear();
    functions.clear();
    code.clear();

    int32_t cCount = 读取i32(buf, pos);
    if (cCount < 0 || pos + static_cast<size_t>(cCount) * 4 > buf.size()) {
        错误信息 = ".rab 文件损坏（常量表长度非法）";
        return false;
    }
    constants.reserve(static_cast<size_t>(cCount));
    for (int32_t i = 0; i < cCount; i++) constants.push_back(读取i32(buf, pos));

    int32_t sCount = 读取i32(buf, pos);
    if (sCount < 0) {
        错误信息 = ".rab 文件损坏（字符串表长度非法）";
        return false;
    }
    strings.reserve(static_cast<size_t>(sCount));
    for (int32_t i = 0; i < sCount; i++) {
        string s;
        if (!读取字符串(buf, pos, s)) {
            错误信息 = ".rab 文件损坏（字符串表）";
            return false;
        }
        strings.push_back(std::move(s));
    }

    int32_t fCount = 读取i32(buf, pos);
    if (fCount < 0) {
        错误信息 = ".rab 文件损坏（函数表长度非法）";
        return false;
    }
    functions.reserve(static_cast<size_t>(fCount));
    for (int32_t i = 0; i < fCount; i++) {
        FunctionInfo info;
        if (!读取字符串(buf, pos, info.name)) {
            错误信息 = ".rab 文件损坏（函数名）";
            return false;
        }
        info.paramCount = 读取i32(buf, pos);
        info.localCount = 读取i32(buf, pos);
        info.regCount = 读取i32(buf, pos);
        info.codeOffset = 读取i32(buf, pos);
#ifdef OPTIMIZATION
        info.jitFunc = nullptr;
#endif
        functions.push_back(info);
    }

    if (!读取字符串(buf, pos, entryPoint)) {
        错误信息 = ".rab 文件损坏（入口函数名）";
        return false;
    }

    int32_t codeSize = 读取i32(buf, pos);
    if (codeSize < 0 || pos + static_cast<size_t>(codeSize) > buf.size()) {
        错误信息 = ".rab 文件损坏（指令流长度非法）";
        return false;
    }
    code.assign(
        reinterpret_cast<const uint8_t*>(buf.data() + pos),
        reinterpret_cast<const uint8_t*>(buf.data() + pos) + codeSize);

    return true;
}

bool BytecodeProgram::saveText(const std::string& path, string& 错误信息) const
{
    std::ostringstream 缓冲区;
    print(缓冲区);
    if (!arch::writeFile(path, 缓冲区.str())) {
        错误信息 = "无法写入文件 '" + path + "'";
        return false;
    }
    return true;
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

int BytecodeGenerator::visit(ArrayDecl& node)
{
    int slot = allocReg(node.name);

    tempReg = totalReg;
    int sizeReg = allocTemp();
    program.emit(Opcode::MOVI, sizeReg, 0, 0, program.addConstant(node.size));

    int initReg = node.initialValue->accept(*this);
    program.emit(Opcode::ARRNEW, slot, sizeReg, initReg);

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
        if (auto* idx = dynamic_cast<IndexExpr*>(node.left.get())) {
            int arrReg = idx->base->accept(*this);
            int idxReg = idx->index->accept(*this);
            int valReg = node.right->accept(*this);
            program.emit(Opcode::ARRSET, valReg, arrReg, idxReg);
            int resultReg = allocTemp();
            program.emit(Opcode::MOV, resultReg, valReg);
            return resultReg;
        }

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
        std::cerr << "内部错误：赋值左侧必须是变量或数组元素" << std::endl;
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

int BytecodeGenerator::visit(IndexExpr& node)
{
    // 基表达式求值：数组变量取其寄存器，嵌套索引取其 ARRGET 结果（数组句柄）
    int arrReg = node.base->accept(*this);
    int idxReg = node.index->accept(*this);
    int resultReg = allocTemp();
    program.emit(Opcode::ARRGET, resultReg, arrReg, idxReg);
    return resultReg;
}

#ifdef OPTIMIZATION

static Opcode tacOpToOpcode(TACOpcode op) {
    switch (op) {
    case TACOpcode::ADD: return Opcode::ADD;
    case TACOpcode::SUB: return Opcode::SUB;
    case TACOpcode::MUL: return Opcode::MUL;
    case TACOpcode::DIV: return Opcode::DIV;
    case TACOpcode::MOD: return Opcode::MOD;
    case TACOpcode::EQ:  return Opcode::EQ;
    case TACOpcode::NE:  return Opcode::NE;
    case TACOpcode::LT:  return Opcode::LT;
    case TACOpcode::GT:  return Opcode::GT;
    case TACOpcode::LE:  return Opcode::LE;
    case TACOpcode::GE:  return Opcode::GE;
    default: return Opcode::HALT;
    }
}

BytecodeProgram BytecodeGenerator::generateFromTAC(const TACProgram& tac,
                                                     const std::vector<int>& tacRegCounts)
{
    program = BytecodeProgram();
    totalReg = 0;
    tempReg = 0;
    maxReg = 0;
    regMaps.clear();

    // Register function metadata
    for (size_t i = 0; i < tac.functions.size(); i++) {
        auto& tf = tac.functions[i];
        program.addFunction(tf.name, tf.paramCount);
    }

    // Skip over function definitions
    int jmpPos = program.getCodeSize();
    program.emit(Opcode::JMP, 0, 0, 0, 0);

    // For each function: record jump patches needed
    struct JumpPatch {
        int bytecodeOffset;
        int targetTACInstIdx;
    };
    std::vector<JumpPatch> patches;

    // Emit each function
    for (size_t fi = 0; fi < tac.functions.size(); fi++) {
        auto& tf = tac.functions[fi];
        auto& funcInfo = program.functions[fi];
        funcInfo.codeOffset = program.getCodeSize();

        funcInfo.localCount = tf.regCount - tf.paramCount;
        funcInfo.regCount = tf.regCount;

        // Map: TAC instruction index -> bytecode offset
        std::vector<int> instToOffset(tf.instructions.size(), -1);

        for (int ti = 0; ti < static_cast<int>(tf.instructions.size()); ti++) {
            auto& inst = tf.instructions[ti];
            instToOffset[ti] = program.getCodeSize();
            auto op = inst->getOpcode();

            if (op == TACOpcode::MOVI) {
                auto* m = static_cast<TACMovI*>(inst.get());
                program.emit(Opcode::MOVI, m->rd.index, 0, 0, program.addConstant(m->constVal));
            }
            else if (op == TACOpcode::MOVS) {
                auto* m = static_cast<TACMovS*>(inst.get());
                program.emit(Opcode::MOVS, m->rd.index, 0, 0,
                             program.addString(tac.strings[m->stringIdx]));
            }
            else if (op == TACOpcode::MOV) {
                auto* m = static_cast<TACMov*>(inst.get());
                program.emit(Opcode::MOV, m->rd.index, m->rs.index);
            }
            else if (op == TACOpcode::ADD || op == TACOpcode::SUB ||
                     op == TACOpcode::MUL || op == TACOpcode::DIV ||
                     op == TACOpcode::MOD || op == TACOpcode::EQ ||
                     op == TACOpcode::NE || op == TACOpcode::LT ||
                     op == TACOpcode::GT || op == TACOpcode::LE ||
                     op == TACOpcode::GE) {
                auto* b = static_cast<TACBinary*>(inst.get());
                program.emit(tacOpToOpcode(op), b->rd.index, b->rs1.index, b->rs2.index);
            }
            else if (op == TACOpcode::JMP) {
                auto* j = static_cast<TACJmp*>(inst.get());
                // Emit with placeholder, patch later
                int patchPos = program.getCodeSize();
                program.emit(Opcode::JMP, 0, 0, 0, 0);
                patches.push_back({patchPos, j->targetBlock});
            }
            else if (op == TACOpcode::JIF) {
                auto* j = static_cast<TACJif*>(inst.get());
                int jifPatch = program.getCodeSize();
                program.emit(Opcode::JIF, 0, j->cond.index, 0, 0);
                patches.push_back({jifPatch, j->targetBlock});
                if (j->fallBlock != ti + 1) {
                    int jmpPatch = program.getCodeSize();
                    program.emit(Opcode::JMP, 0, 0, 0, 0);
                    patches.push_back({jmpPatch, j->fallBlock});
                }
            }
            else if (op == TACOpcode::PUSH) {
                auto* p = static_cast<TACParm*>(inst.get());
                program.emit(Opcode::PUSH, 0, p->rs.index);
            }
            else if (op == TACOpcode::CALL) {
                auto* c = static_cast<TACCall*>(inst.get());
                program.emit(Opcode::CALL, 0, 0, 0, c->funcIdx);
                program.emit(Opcode::MOV, c->rd.index, 0);
            }
            else if (op == TACOpcode::PRINT) {
                auto* p = static_cast<TACPrint*>(inst.get());
                program.emit(Opcode::PRINT, 0, p->rs.index);
            }
            else if (op == TACOpcode::RET) {
                auto* r = static_cast<TACRet*>(inst.get());
                program.emit(Opcode::MOV, 0, r->rs.index);
                program.emit(Opcode::RET);
            }
            else if (op == TACOpcode::HALT) {
                program.emit(Opcode::HALT);
            }
        }

        // Default epilogue if no explicit RET
        if (tf.instructions.empty() || tf.instructions.back()->getOpcode() != TACOpcode::RET) {
            int retReg = funcInfo.regCount;
            program.emit(Opcode::MOVI, retReg, 0, 0, program.addConstant(0));
            program.emit(Opcode::MOV, 0, retReg);
            program.emit(Opcode::RET);
        }

        // Patch jumps: map TAC instruction index -> bytecode offset
        // We need to patch within this function's range
        // instToOffset maps TAC instruction index to bytecode offset
        // We need a local patch list for this function
    }

    // Now patch all jumps globally
    // patches[].targetTACInstIdx is relative to the TAC function
    // but we need to know which function each patch belongs to
    // Actually, patches are accumulated across functions, so we need per-function tracking
    // Let me redo this with per-function tracking

    // For now, the patches have been accumulated. We need to map
    // TAC instruction index -> global bytecode offset
    // But TAC instruction indices are per-function, so we need a global mapping

    // Let me rebuild: compute cumulative instruction offsets
    std::vector<int> funcStartTACIdx; // TAC instruction index where each function starts
    int cumulativeIdx = 0;
    for (size_t fi = 0; fi < tac.functions.size(); fi++) {
        funcStartTACIdx.push_back(cumulativeIdx);
        cumulativeIdx += static_cast<int>(tac.functions[fi].instructions.size());
    }

    // The patches store targetTACInstIdx which is local to each function
    // We need to convert to global TAC index and then to bytecode offset
    // This is getting complicated. Let me redo the patching with a simpler approach

    // Actually, let me just redo the whole thing with per-function patch lists
    program = BytecodeProgram();
    totalReg = 0;
    tempReg = 0;
    maxReg = 0;

    for (size_t i = 0; i < tac.functions.size(); i++) {
        auto& tf = tac.functions[i];
        program.addFunction(tf.name, tf.paramCount);
    }

    jmpPos = program.getCodeSize();
    program.emit(Opcode::JMP, 0, 0, 0, 0);

    // Per-function patches
    struct Patch { int bcOffset; int tacTarget; };
    std::vector<Patch> allPatches;

    for (size_t fi = 0; fi < tac.functions.size(); fi++) {
        auto& tf = tac.functions[fi];
        auto& funcInfo = program.functions[fi];
        funcInfo.codeOffset = program.getCodeSize();
        funcInfo.localCount = tf.regCount - tf.paramCount;
        funcInfo.regCount = tf.regCount;

        std::vector<int> instToOffset(tf.instructions.size(), -1);

        for (int ti = 0; ti < static_cast<int>(tf.instructions.size()); ti++) {
            auto& inst = tf.instructions[ti];
            instToOffset[ti] = program.getCodeSize();
            auto op = inst->getOpcode();

            if (op == TACOpcode::MOVI) {
                auto* m = static_cast<TACMovI*>(inst.get());
                program.emit(Opcode::MOVI, m->rd.index, 0, 0, program.addConstant(m->constVal));
            }
            else if (op == TACOpcode::MOVS) {
                auto* m = static_cast<TACMovS*>(inst.get());
                program.emit(Opcode::MOVS, m->rd.index, 0, 0,
                             program.addString(tac.strings[m->stringIdx]));
            }
            else if (op == TACOpcode::MOV) {
                auto* m = static_cast<TACMov*>(inst.get());
                program.emit(Opcode::MOV, m->rd.index, m->rs.index);
            }
            else if (op == TACOpcode::ADD || op == TACOpcode::SUB ||
                     op == TACOpcode::MUL || op == TACOpcode::DIV ||
                     op == TACOpcode::MOD || op == TACOpcode::EQ ||
                     op == TACOpcode::NE || op == TACOpcode::LT ||
                     op == TACOpcode::GT || op == TACOpcode::LE ||
                     op == TACOpcode::GE) {
                auto* b = static_cast<TACBinary*>(inst.get());
                program.emit(tacOpToOpcode(op), b->rd.index, b->rs1.index, b->rs2.index);
            }
            else if (op == TACOpcode::JMP) {
                auto* j = static_cast<TACJmp*>(inst.get());
                int pos = program.getCodeSize();
                program.emit(Opcode::JMP, 0, 0, 0, 0);
                allPatches.push_back({pos, j->targetBlock});
            }
            else if (op == TACOpcode::JIF) {
                auto* j = static_cast<TACJif*>(inst.get());
                int jifPos = program.getCodeSize();
                program.emit(Opcode::JIF, 0, j->cond.index, 0, 0);
                allPatches.push_back({jifPos, j->targetBlock});
                if (j->fallBlock != ti + 1) {
                    int jmpPos2 = program.getCodeSize();
                    program.emit(Opcode::JMP, 0, 0, 0, 0);
                    allPatches.push_back({jmpPos2, j->fallBlock});
                }
            }
            else if (op == TACOpcode::PUSH) {
                auto* p = static_cast<TACParm*>(inst.get());
                program.emit(Opcode::PUSH, 0, p->rs.index);
            }
            else if (op == TACOpcode::CALL) {
                auto* c = static_cast<TACCall*>(inst.get());
                program.emit(Opcode::CALL, 0, 0, 0, c->funcIdx);
                program.emit(Opcode::MOV, c->rd.index, 0);
            }
            else if (op == TACOpcode::ARRNEW) {
                auto* n = static_cast<TACArrayNew*>(inst.get());
                program.emit(Opcode::ARRNEW, n->rd.index, n->size.index, n->init.index);
            }
            else if (op == TACOpcode::ARRGET) {
                auto* g = static_cast<TACArrayGet*>(inst.get());
                program.emit(Opcode::ARRGET, g->rd.index, g->arr.index, g->idx.index);
            }
            else if (op == TACOpcode::ARRSET) {
                auto* s = static_cast<TACArraySet*>(inst.get());
                program.emit(Opcode::ARRSET, s->val.index, s->arr.index, s->idx.index);
            }
            else if (op == TACOpcode::PRINT) {
                auto* p = static_cast<TACPrint*>(inst.get());
                program.emit(Opcode::PRINT, 0, p->rs.index);
            }
            else if (op == TACOpcode::RET) {
                auto* r = static_cast<TACRet*>(inst.get());
                program.emit(Opcode::MOV, 0, r->rs.index);
                program.emit(Opcode::RET);
            }
            else if (op == TACOpcode::HALT) {
                program.emit(Opcode::HALT);
            }
        }

        // Default epilogue
        if (tf.instructions.empty() || tf.instructions.back()->getOpcode() != TACOpcode::RET) {
            int retReg = funcInfo.regCount;
            program.emit(Opcode::MOVI, retReg, 0, 0, program.addConstant(0));
            program.emit(Opcode::MOV, 0, retReg);
            program.emit(Opcode::RET);
        }

        // Patch jumps within this function
        for (auto& p : allPatches) {
            // tacTarget is a TAC instruction index local to this function
            if (p.tacTarget >= 0 && p.tacTarget < static_cast<int>(instToOffset.size())) {
                int targetBcOffset = instToOffset[p.tacTarget];
                int relativeOffset = targetBcOffset - (p.bcOffset + 8);
                program.patchOperand(p.bcOffset, relativeOffset);
            }
        }
        allPatches.clear();
    }

    // Patch the initial JMP to entry point
    int entryPos = program.getCodeSize();
    program.patchOperand(jmpPos, entryPos - (jmpPos + 8));

    int mainIdx = -1;
    for (int i = 0; i < static_cast<int>(program.functions.size()); i++) {
        if (program.functions[i].name == "主函数") {
            mainIdx = i;
            break;
        }
    }

    if (mainIdx >= 0) {
        int r0Temp = 1;
        program.emit(Opcode::MOVI, r0Temp, 0, 0, program.addConstant(0));
        program.emit(Opcode::PUSH, 0, r0Temp);
        program.emit(Opcode::CALL, 0, 0, 0, mainIdx);
        program.emit(Opcode::HALT);
    }
    program.entryPoint = "主函数";
    return program;
}

#endif
