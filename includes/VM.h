#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "Bytecode.h"

//
// 寄存器式虚拟机 —— 执行 Rua 寄存器字节码
//
// 架构：
//   值栈（stackData）：存放所有函数帧，每个帧是一组连续寄存器
//   帧基址（fp）：当前帧在值栈中的起始位置
//   寄存器 rX = stackData[fp + X]
//
// 帧结构：
//   r0..r(paramCount-1) : 参数
//   r(paramCount)..r(paramCount+localCount-1) : 局部变量
//   其余 : 临时值
//
// 调用约定：
//   调用前 PUSH 参数值（参数值在值栈上位于当前帧之上）
//   CALL 创建新帧，PUSH 的值变成新帧的 r0..r(N-1)
//   RET 将返回值写入调用者的 r0，恢复调用者帧
//

enum class ValueType { INTEGER, STRING };

struct Value {
    ValueType type;
    int data;

    Value() : type(ValueType::INTEGER), data(0) {}
    explicit Value(int v) : type(ValueType::INTEGER), data(v) {}
    Value(int d, ValueType t) : type(t), data(d) {}
    void print() const;
};

class VMError : public std::runtime_error {
  public:
    explicit VMError(const std::string& message);
};

class VM {
  private:
    static constexpr int STACK_CAP = 65536;
    static constexpr int CALL_CAP = 65536;
    static constexpr int FRAME_CAP = 65536;

    std::unique_ptr<Value[]> stackData;
    std::unique_ptr<int[]> callStackData;
    std::unique_ptr<int[]> frameStackData;

    int sp;
    int cp;
    int fpStack; // frame stack pointer (index into frameStackData)

    const BytecodeProgram* program;
    int ip;

  public:
    VM();
    void run(const BytecodeProgram& prog);
    int getStackDepth() const { return sp + 1; }
    int getCallDepth() const { return cp + 1; }

  private:
    inline Value& reg(int r)
    {
        return stackData[fpStack >= 0 ? (frameStackData[fpStack] + r) : r];
    }
};
