# 虚拟机执行是怎么生成的？

## 输入

`BytecodeProgram`（来自字节码生成）。

## 输出

程序执行结果（标准输出）。

## 架构

```
值栈 (stackData)        — Value[] 预分配数组，存放所有函数帧
帧基址 (fs[_fs])         — 当前帧在值栈中的起始位置
寄存器 rX = stack[fp+X] — 每个函数帧是一组连续寄存器
调用栈 (callStackData)   — int[] 预分配数组，保存返回地址
帧栈 (frameStackData)    — int[] 预分配数组，保存每层函数调用的帧基址
指令指针 (ip)            — 当前正在执行的字节码位置
```

**帧结构（每个函数一帧）：**
```
r0:        保留给返回值（不分配给变量）
r1..rN:    参数
r(N+1)..:  局部变量 + 临时值
```

## 执行流程

`VM::run(prog)` 执行以下步骤：

### 1. 初始化

```cpp
_sp = -1;        // 值栈空
_cp = -1;        // 调用栈空
_fs = -1;        // 帧栈空（无帧）
s = stackData.get();  // 预分配数组指针
```

### 2. 指令分发

使用简单的 `while (ip < code.size()) { switch(op) }` 循环，每条指令解析 opcode + rd + rs1 + rs2 + extra。

### 3. 调用约定

```
CALL funcIdx:
    // PUSH 的参数在值栈顶部，新帧从它们开始
    newFP = _sp - func.paramCount          // 第一个 PUSH 的是 r0 占位
    fs[++_fs] = newFP                      // 压入新帧基址
    s[newFP + (paramCount+1)..] = 0        // 清零局部变量和临时区
    _sp = newFP + func.regCount - 1        // 更新栈顶
    cs[++_cp] = ip + 8                     // 保存返回地址
    ip = func.codeOffset                   // 跳转到函数体

RET:
    retVal = greg(0)                       // 读取返回值 r0
    _sp = getFP() - 1                      // 弹出整个帧
    --_fs                                  // 恢复调用者帧
    greg(0) = retVal                       // 返回值写入调用者 r0
    ip = cs[_cp--]                         // 恢复返回地址
```

### 4. 寄存器访问

```
greg(r) = s[fs[_fs] + r]    // 通过当前帧基址访问寄存器
```

## 指令集

每条指令固定 8 字节：`opcode(1) + rd(1) + rs1(1) + rs2(1) + extra(4)`

| 操作码 | 指令 | 说明 |
|--------|------|------|
| 0x00 | HALT | 程序终止 |
| 0x01 | MOVI rd, ci | rd = constants[ci] |
| 0x02 | MOVS rd, si | rd = strings[si] |
| 0x03 | MOV rd, rs | rd = rs |
| 0x04 | ADD rd, rs1, rs2 | rd = rs1 + rs2 |
| 0x05 | SUB rd, rs1, rs2 | rd = rs1 - rs2 |
| 0x06 | MUL rd, rs1, rs2 | rd = rs1 * rs2 |
| 0x07 | DIV rd, rs1, rs2 | rd = rs1 / rs2 |
| 0x08 | MOD rd, rs1, rs2 | rd = rs1 % rs2 |
| 0x09 | EQ rd, rs1, rs2 | rd = (rs1 == rs2) |
| 0x0A | NE rd, rs1, rs2 | rd = (rs1 != rs2) |
| 0x0B | LT rd, rs1, rs2 | rd = (rs1 <  rs2) |
| 0x0C | GT rd, rs1, rs2 | rd = (rs1 >  rs2) |
| 0x0D | JMP offset | ip += offset |
| 0x0E | JIF rs, offset | if (rs == 0) ip += offset |
| 0x0F | PUSH rs | stack[++sp] = reg(rs) |
| 0x10 | CALL idx | 调用 functions[idx] |
| 0x11 | RET | 函数返回 |
| 0x12 | PRINT rs | 输出 reg(rs) |

## 关键代码

`src/虚拟机.cpp` — `VM::run()` 方法，约 350 行
`includes/虚拟机.h` — `VM` 类、`Value` 结构体
