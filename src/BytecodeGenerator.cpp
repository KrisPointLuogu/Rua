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
    case Opcode::ARRDIMSET: return "ARRDIMSET";
    case Opcode::ARRGETN: return "ARRGETN";
    case Opcode::ARRSETN: return "ARRSETN";
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
        case Opcode::ARRNEW:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                      << (int)rs2;
            break;
        case Opcode::ARRGET:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                      << (int)rs2;
            break;
        case Opcode::ARRSET:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", r"
                      << (int)rs2;
            break;
        case Opcode::ARRDIMSET:
            std::cout << " r" << (int)rs1 << ", dim" << extra << ", r"
                      << (int)rs2;
            break;
        case Opcode::ARRGETN:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", ["
                      << extra << " 下标]";
            break;
        case Opcode::ARRSETN:
            std::cout << " r" << (int)rd << ", r" << (int)rs1 << ", ["
                      << extra << " 下标]";
            break;
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

int BytecodeGenerator::visit(ArrayDecl& node)
{
    int slot = allocReg(node.name);

    tempReg = totalReg;

    int rank = static_cast<int>(node.sizes.size());
    std::vector<int> constDims(rank, -1);
    std::vector<int> dimRegs(rank, -1);

    // 各维度：常量直接 MOVI，动态求值
    for (int i = 0; i < rank; i++) {
        int 常量值;
        if (折叠常量表达式(node.sizes[i].get(), 常量值)) {
            constDims[i] = 常量值;
            int r = allocTemp();
            program.emit(Opcode::MOVI, r, 0, 0, program.addConstant(常量值));
            dimRegs[i] = r;
        } else {
            dimRegs[i] = node.sizes[i]->accept(*this);
        }
    }

    // 总长度 = 各维乘积
    int totalSizeReg = dimRegs[0];
    for (int i = 1; i < rank; i++) {
        int r = allocTemp();
        program.emit(Opcode::MUL, r, totalSizeReg, dimRegs[i]);
        totalSizeReg = r;
    }

    // 初始值：广播（单标量）或 0
    ArrayLiteral* 列表 = nullptr;
    int initReg;
    bool 已设初值 = false;
    if (node.initialValue) {
        if (auto* lit = dynamic_cast<ArrayLiteral*>(node.initialValue.get())) {
            if (lit->elements.size() == 1
                && !dynamic_cast<ArrayLiteral*>(lit->elements[0].get())) {
                initReg = lit->elements[0]->accept(*this);
                已设初值 = true;
            } else {
                列表 = lit;
            }
        } else {
            initReg = node.initialValue->accept(*this);
            已设初值 = true;
        }
    }
    if (!已设初值) {
        int zeroReg = allocTemp();
        program.emit(Opcode::MOVI, zeroReg, 0, 0, program.addConstant(0));
        initReg = zeroReg;
    }

    program.emit(Opcode::ARRNEW, slot, totalSizeReg, initReg);

    // 多值 / 嵌套初始化：按 row-major 平铺偏移逐个写入
    // （必须在 ARRDIMSET 之前，此时数组仍是 1 维、ARRSET 接受扁平偏移）
    if (列表) {
        std::vector<初始化项> items;
        展平初始化(列表, constDims, 0, 0, items);
        for (auto& item : items) {
            int offReg = allocTemp();
            program.emit(Opcode::MOVI, offReg, 0, 0,
                         program.addConstant(item.offset));
            int valReg = item.expr->accept(*this);
            program.emit(Opcode::ARRSET, valReg, slot, offReg);
        }
    }

    // 记录各维长度
    for (int i = 0; i < rank; i++) {
        program.emit(Opcode::ARRDIMSET, 0, slot, dimRegs[i], i);
    }

    tempReg = totalReg;
    return slot;
}

int BytecodeGenerator::visit(ArrayLiteral& node)
{
    // 数组字面量仅作为 ArrayDecl 的初始化出现，不会被单独求值
    (void)node;
    return 0;
}

bool BytecodeGenerator::折叠常量表达式(const ASTNode* node, int& out)
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

void BytecodeGenerator::展平初始化(ArrayLiteral* lit,
                                  const std::vector<int>& constDims,
                                  int level, int baseOffset,
                                  std::vector<初始化项>& out)
{
    // 本层元素全是标量（叶子层）→ 顺序填充；否则按行距定位各子列表
    bool 叶子层 = true;
    for (auto& el : lit->elements) {
        if (dynamic_cast<ArrayLiteral*>(el.get())) {
            叶子层 = false;
            break;
        }
    }
    if (叶子层) {
        for (size_t i = 0; i < lit->elements.size(); i++) {
            out.push_back({ baseOffset + static_cast<int>(i),
                            lit->elements[i].get() });
        }
        return;
    }

    // 本层行距 = 后段维度乘积（语义分析已保证其为常量）
    int stride = 1;
    for (int i = level + 1; i < static_cast<int>(constDims.size()); i++)
        stride *= constDims[i];

    for (size_t i = 0; i < lit->elements.size(); i++) {
        auto& el = lit->elements[i];
        int offset = baseOffset + static_cast<int>(i) * stride;
        if (auto* sub = dynamic_cast<ArrayLiteral*>(el.get())) {
            展平初始化(sub, constDims, level + 1, offset, out);
        } else {
            out.push_back({ offset, el.get() });
        }
    }
}

int BytecodeGenerator::收集索引链(IndexExpr& node,
                                std::vector<IndexExpr*>& out)
{
    IndexExpr* cur = &node;
    while (true) {
        out.push_back(cur);
        if (auto* inner = dynamic_cast<IndexExpr*>(cur->base.get()))
            cur = inner;
        else
            break;
    }
    return static_cast<int>(out.size());
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
            std::vector<IndexExpr*> chain;
            收集索引链(*idx, chain);

            if (chain.size() == 1) {
                // 深度 1 → 单索引快路径
                int arrReg = idx->base->accept(*this);
                int idxReg = idx->index->accept(*this);
                int valReg = node.right->accept(*this);
                program.emit(Opcode::ARRSET, valReg, arrReg, idxReg);
                int resultReg = allocTemp();
                program.emit(Opcode::MOV, resultReg, valReg);
                return resultReg;
            }

            // 深度 ≥ 2 → 变长下标写入
            int arrReg = chain.back()->base->accept(*this);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                int idxReg = (*it)->index->accept(*this);
                program.emit(Opcode::PUSH, 0, idxReg);
            }
            int valReg = node.right->accept(*this);
            program.emit(Opcode::ARRSETN, valReg, arrReg, 0,
                         static_cast<int>(chain.size()));
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
    std::vector<IndexExpr*> chain;
    收集索引链(node, chain);

    // 深度 1 → 单索引快路径
    if (chain.size() == 1) {
        int arrReg = node.base->accept(*this);
        int idxReg = node.index->accept(*this);
        int resultReg = allocTemp();
        program.emit(Opcode::ARRGET, resultReg, arrReg, idxReg);
        return resultReg;
    }

    // 深度 ≥ 2 → 变长下标读取：先求基表达式，再按源顺序求值下标并 PUSH
    int arrReg = chain.back()->base->accept(*this);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        int idxReg = (*it)->index->accept(*this);
        program.emit(Opcode::PUSH, 0, idxReg);
    }
    int resultReg = allocTemp();
    program.emit(Opcode::ARRGETN, resultReg, arrReg, 0,
                 static_cast<int>(chain.size()));
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
            else if (op == TACOpcode::ARRDIMSET) {
                auto* d = static_cast<TACArrayDimSet*>(inst.get());
                program.emit(Opcode::ARRDIMSET, 0, d->arr.index, d->val.index, d->dimIdx);
            }
            else if (op == TACOpcode::ARRGETN) {
                auto* g = static_cast<TACArrayGetN*>(inst.get());
                program.emit(Opcode::ARRGETN, g->rd.index, g->arr.index, 0, g->indexCount);
            }
            else if (op == TACOpcode::ARRSETN) {
                auto* s = static_cast<TACArraySetN*>(inst.get());
                program.emit(Opcode::ARRSETN, s->val.index, s->arr.index, 0, s->indexCount);
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
