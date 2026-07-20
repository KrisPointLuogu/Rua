/*
 * 栈式虚拟机 —— VM
 *
 * 执行由编译器生成的 Rua 字节码。
 *
 * 架构：
 *   值栈（stack）        — 运算和参数传递
 *   调用栈（callStack）  — 保存返回地址
 *   帧栈（frameStack）   — 每个函数栈帧的基址
 *   IP（ip）             — 指令指针
 */

#include "虚拟机.h"
#include <iostream>
#include <sstream>
#include <cassert>
#ifdef _DEBUG
#include <chrono>
#endif

using std::string;
using std::vector;

// ==================== Value ====================

Value::Value() : type(ValueType::INTEGER), intVal(0) {}
Value::Value(int v) : type(ValueType::INTEGER), intVal(v) {}
Value::Value(const string &s) : type(ValueType::STRING), intVal(0), strVal(s) {}

void Value::print() const
{
    switch (type)
    {
    case ValueType::INTEGER:
        std::cout << intVal;
        break;
    case ValueType::STRING:
        std::cout << strVal;
        break;
    }
}

// ==================== VMError ====================

VMError::VMError(const string &message) : std::runtime_error(message) {}

// ==================== 前置声明 ====================

static int readOperand(const std::vector<uint8_t> &code, int &ip);
static void expectInteger(const Value &v, const std::string &opName, int ip);

// ==================== VM ====================

VM::VM() : program(nullptr), ip(0) {}

void VM::run(const BytecodeProgram &prog)
{
    program = &prog;
    ip = 0;
    stack.clear();
    callStack.clear();
    frameStack.clear();

    if (program->code.empty())
        throw VMError("字节码为空");

#ifdef _DEBUG
    auto 开始 = std::chrono::high_resolution_clock::now();
#endif

    while (ip < static_cast<int>(program->code.size()))
    {
        Opcode op = static_cast<Opcode>(program->code[ip]);
        ip++;

        if (op == Opcode::HALT)
            break;

        switch (op)
        {
        case Opcode::ICONST:
        {
            int idx = readOperand(program->code, ip);
            if (idx < 0 || idx >= static_cast<int>(program->constants.size()))
                throw VMError("运行时错误：整数常量索引越界");
            push(Value(program->constants[idx]));
            break;
        }

        case Opcode::SCONST:
        {
            int idx = readOperand(program->code, ip);
            if (idx < 0 || idx >= static_cast<int>(program->strings.size()))
                throw VMError("运行时错误：字符串常量索引越界");
            push(Value(program->strings[idx]));
            break;
        }

        case Opcode::ADD:
        {
            Value b = pop();
            Value a = pop();
            if (a.type == ValueType::INTEGER && b.type == ValueType::INTEGER)
                push(Value(a.intVal + b.intVal));
            else if (a.type == ValueType::STRING && b.type == ValueType::STRING)
                push(Value(a.strVal + b.strVal));
            else
                throw VMError("运行时错误：ADD 操作数类型不匹配");
            break;
        }

        case Opcode::SUB:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "SUB", ip);
            expectInteger(b, "SUB", ip);
            push(Value(a.intVal - b.intVal));
            break;
        }

        case Opcode::MUL:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "MUL", ip);
            expectInteger(b, "MUL", ip);
            push(Value(a.intVal * b.intVal));
            break;
        }

        case Opcode::DIV:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "DIV", ip);
            expectInteger(b, "DIV", ip);
            if (b.intVal == 0)
                throw VMError("运行时错误：除数为零");
            push(Value(a.intVal / b.intVal));
            break;
        }

        case Opcode::MOD:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "MOD", ip);
            expectInteger(b, "MOD", ip);
            if (b.intVal == 0)
                throw VMError("运行时错误：除数为零");
            push(Value(a.intVal % b.intVal));
            break;
        }

        case Opcode::EQ:
        {
            Value b = pop();
            Value a = pop();
            if (a.type != b.type)
                push(Value(0));
            else if (a.type == ValueType::INTEGER)
                push(Value(a.intVal == b.intVal ? 1 : 0));
            else
                push(Value(a.strVal == b.strVal ? 1 : 0));
            break;
        }

        case Opcode::NEQ:
        {
            Value b = pop();
            Value a = pop();
            if (a.type != b.type)
                push(Value(1));
            else if (a.type == ValueType::INTEGER)
                push(Value(a.intVal != b.intVal ? 1 : 0));
            else
                push(Value(a.strVal != b.strVal ? 1 : 0));
            break;
        }

        case Opcode::LT:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "LT", ip);
            expectInteger(b, "LT", ip);
            push(Value(a.intVal < b.intVal ? 1 : 0));
            break;
        }

        case Opcode::GT:
        {
            Value b = pop();
            Value a = pop();
            expectInteger(a, "GT", ip);
            expectInteger(b, "GT", ip);
            push(Value(a.intVal > b.intVal ? 1 : 0));
            break;
        }

        case Opcode::JMP:
        {
            int offset = readOperand(program->code, ip);
            ip += offset;
            break;
        }

        case Opcode::JIF:
        {
            int offset = readOperand(program->code, ip);
            Value cond = pop();
            expectInteger(cond, "JIF", ip);
            if (cond.intVal == 0)
                ip += offset;
            break;
        }

        case Opcode::LOAD:
        {
            int slot = readOperand(program->code, ip);
            if (frameStack.empty())
                throw VMError("运行时错误：LOAD 时无活动帧");
            int base = frameStack.back();
            int idx = base + slot;
            if (idx < 0 || idx >= static_cast<int>(stack.size()))
                throw VMError("运行时错误：LOAD 访问越界");
            push(stack[idx]);
            break;
        }

        case Opcode::STORE:
        {
            int slot = readOperand(program->code, ip);
            if (frameStack.empty())
                throw VMError("运行时错误：STORE 时无活动帧");
            int base = frameStack.back();
            int idx = base + slot;
            if (idx < 0 || idx >= static_cast<int>(stack.size()))
                throw VMError("运行时错误：STORE 访问越界");
            stack[idx] = pop();
            break;
        }

        case Opcode::CALL:
        {
            int funcIdx = readOperand(program->code, ip);
            if (funcIdx < 0 || funcIdx >= static_cast<int>(program->functions.size()))
                throw VMError("运行时错误：函数索引越界");

            const FunctionInfo &func = program->functions[funcIdx];
            int frameBase = static_cast<int>(stack.size()) - func.paramCount;
            frameStack.push_back(frameBase);

            for (int i = 0; i < func.localCount; i++)
                push(Value(0));

            callStack.push_back(ip);
            ip = func.codeOffset;
            break;
        }

        case Opcode::RET:
        {
            Value retVal = pop();

            if (frameStack.empty())
                throw VMError("运行时错误：RET 时无活动帧");
            int frameBase = frameStack.back();
            frameStack.pop_back();

            stack.resize(frameBase);
            push(retVal);

            if (callStack.empty())
                throw VMError("运行时错误：RET 时调用栈为空");
            ip = callStack.back();
            callStack.pop_back();
            break;
        }

        case Opcode::PRINT:
        {
            Value v = pop();
            v.print();
            break;
        }

        case Opcode::POP:
        {
            pop();
            break;
        }

        default:
        {
            std::ostringstream oss;
            oss << "运行时错误：未知操作码 0x"
                << std::hex << static_cast<int>(op) << std::dec
                << "，IP=" << (ip - 1);
            throw VMError(oss.str());
        }
        }
    }

#ifdef _DEBUG
    auto 结束 = std::chrono::high_resolution_clock::now();
    auto 耗时 = std::chrono::duration_cast<std::chrono::microseconds>(结束 - 开始).count();
    std::cout << "\n[DEBUG] 字节码执行耗时: " << 耗时 << " 微秒 (" << (耗时 / 1000.0) << " 毫秒)\n";
#endif
}

int VM::getStackDepth() const
{
    return static_cast<int>(stack.size());
}

int VM::getCallDepth() const
{
    return static_cast<int>(callStack.size());
}

// ==================== 栈操作 ====================

Value VM::pop()
{
    if (stack.empty())
        throw VMError("运行时错误：栈为空，无法弹出");
    Value v = stack.back();
    stack.pop_back();
    return v;
}

void VM::push(const Value &val)
{
    stack.push_back(val);
}

Value VM::peek(int offset) const
{
    if (offset < 0 || offset >= static_cast<int>(stack.size()))
        throw VMError("运行时错误：栈索引越界");
    return stack[stack.size() - 1 - offset];
}

// ==================== 读取操作数 ====================

static int readOperand(const vector<uint8_t> &code, int &ip)
{
    int val = static_cast<int>(code[ip]) | (static_cast<int>(code[ip + 1]) << 8) | (static_cast<int>(code[ip + 2]) << 16) | (static_cast<int>(code[ip + 3]) << 24);
    ip += 4;
    return val;
}

// ==================== 类型检查辅助 ====================

static void expectInteger(const Value &v, const string &opName, int ip)
{
    if (v.type != ValueType::INTEGER)
    {
        std::ostringstream oss;
        oss << "运行时错误（IP=" << ip << "）：运算符 " << opName
            << " 要求整数操作数，但遇到字符串";
        throw VMError(oss.str());
    }
}


