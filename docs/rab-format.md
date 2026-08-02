# Rua 字节码文件格式（.rab）

`.rab` 是 Rua 编译产物的二进制格式，可被 `native/` 下的 Rust 运行时重新加载执行。

所有多字节整数均为**小端序**，`i32` 为 4 字节有符号整数。

```
偏移       大小      字段                  说明
─────────────────────────────────────────────────────────────
0x00       4        magic                 ASCII "RUA\0"
0x04       4        version               i32，当前为 1
─────────────────────────────────────────────────────────────
0x08       4        constantCount         i32，整数常量个数
0x0C       n*4      constants             constantCount 个 i32 常量值
─────────────────────────────────────────────────────────────
...        4        stringCount           i32，字符串常量个数
...        ...      strings               每个字符串：i32 长度 + UTF-8 字节
─────────────────────────────────────────────────────────────
...        4        functionCount         i32，函数个数
...        ...      functions             每个函数：
                                         - i32 nameLen + UTF-8 字节 name
                                         - i32 paramCount
                                         - i32 localCount
                                         - i32 regCount
                                         - i32 codeOffset
─────────────────────────────────────────────────────────────
...        4        entryPointLen         i32
...        len      entryPoint            UTF-8 入口函数名
─────────────────────────────────────────────────────────────
...        4        codeSize              i32，指令流字节数
...        codeSize  code                 指令流，每条指令定长 8 字节
─────────────────────────────────────────────────────────────
```

## 指令流（code）

每条指令 8 字节：

```
byte[0] = opcode
byte[1] = rd
byte[2] = rs1
byte[3] = rs2
byte[4..8] = extra (i32 小端)
```

opcode 枚举值（与 `includes/Bytecode.h` 中 `Opcode` 一致）：

```
HALT=0x00  MOVI=0x01  MOVS=0x02  MOV=0x03
ADD=0x04   SUB=0x05   MUL=0x06   DIV=0x07   MOD=0x08
EQ=0x09    NE=0x0A    LT=0x0B    GT=0x0C
JMP=0x0D   JIF=0x0E   PUSH=0x0F  CALL=0x10
RET=0x11   PRINT=0x12 LE=0x13    GE=0x14
ARRNEW=0x15 ARRGET=0x16 ARRSET=0x17
```

## 调用约定（VM 语义，Rust 端须对齐）

- 帧 = 值栈上一组连续寄存器；`r0` 保留给返回值，参数从 `r1..rN`
- CALL 前先 PUSH 参数（含 r0 占位），CALL 将 PUSH 的值作为新帧 `r0..rN`
- RET 将返回值写回调用者 `r0`
- ARRNEW 使用数组池，数组句柄为池索引；`Value{ type, data }`，type ∈ {INTEGER, STRING, ARRAY}
- JMP/JIF 的 extra 是**相对偏移**：`ip += 8 + extra`
