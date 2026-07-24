# Rua 模块结构

## 编译流水线

```
源码 → 词法分析器 → Token → 语法分析器 → AST → 语义分析 → 字节码生成 → 虚拟机执行
                                                          ↓
                                                   JIT 编译 (x86-64)
                                                          ↓
                                                   机器码直接执行
```

**执行策略：**
- 优化模式 (`-DOPTIMIZATION`)：纯整数/算术函数走 JIT，含字符串/输出的函数退回解释器
- 非优化模式：全部走解释器

---

## 文件与类总览

### `includes/语法树.h` — AST 节点定义

| 类 | 继承 | 实现 | 概括 |
|---|------|------|------|
| `ASTNode` | — | — | AST 节点基类，含源码位置 (`line`, `column`) |
| `ASTVisitor` | — | — | 访问者接口，每个节点类型有一个纯虚 `visit()` |
| `Program` | ASTNode | — | 根节点，持有函数列表和顶层语句 |
| `Function` | ASTNode | — | 函数定义：名称、参数列表、函数体 |
| `Block` | ASTNode | — | 语句块（花括号括起来的语句序列） |
| `VarDecl` | ASTNode | — | 变量声明：名称 + 可选的初始化表达式 |
| `IfStmt` | ASTNode | — | 条件分支：条件 + then 块 + 可选的 else 块 |
| `WhileStmt` | ASTNode | — | while 循环：条件 + 循环体 |
| `ReturnStmt` | ASTNode | — | return 语句：返回值表达式 |
| `ExprStmt` | ASTNode | — | 表达式语句 |
| `BinaryExpr` | ASTNode | — | 二元运算：左操作数 + 右操作数 + 运算符类型 |
| `CallExpr` | ASTNode | — | 函数调用：被调函数名 + 参数列表 |
| `NumberLiteral` | ASTNode | — | 整数字面量 |
| `StringLiteral` | ASTNode | — | 字符串字面量 |
| `Identifier` | ASTNode | — | 变量/参数引用 |

---

### `includes/词法分析器.h` — 词法分析器

| 类 | 继承 | 实现 | 概括 |
|---|------|------|------|
| `词法分析器类` | — | — | 将源码字符串扫描为 Token 列表 |
| `私有工具` | — | — | 词法分析辅助：字符串匹配、标识符读取 |

**包含：** `全局内容.h`

---

### `includes/语法分析器.h` — 语法分析器

| 类 | 继承 | 实现 | 概括 |
|---|------|------|------|
| `ParserError` | `std::runtime_error` | — | 语法错误异常 |
| `Parser` | — | — | 递归下降解析器，Token 流 → AST |

**包含：** `UTF32支持.h` `语法树.h` `词法分析器.h`

---

### `includes/语义分析.h` — 语义分析

| 类 | 继承 | 实现 | 概括 |
|---|------|------|------|
| `SemanticError` | `std::runtime_error` | — | 语义错误异常 |
| `SymbolTable` | — | — | 符号表，支持嵌套作用域和重复声明检查 |
| `SemanticAnalyzer` | — | `ASTVisitor` | 语义分析器：检查作用域、变量声明、参数匹配 |

**包含：** `语法树.h`

---

### `includes/字节码.h` — 字节码生成

| 类/结构体 | 继承 | 实现 | 概括 |
|----------|------|------|------|
| `FunctionInfo` | — | — | 函数元信息：参数数、局部变量数、寄存器数、代码偏移 |
| `BytecodeProgram` | — | — | 字节码程序：指令流 + 常量表 + 字符串表 + 函数表 |
| `BytecodeGenerator` | — | `ASTVisitor` | 字节码生成器：AST → 寄存器式三地址码 |

**包含：** `语法树.h` `语义分析.h`

**指令格式：** 每条 8 字节固定长度：`opcode(1) + rd(1) + rs1(1) + rs2(1) + extra(4)`

**寄存器约定：** r0 保留给返回值，参数从 r1 开始分配，临时值每句结束后回收。

---

### `includes/虚拟机.h` — 虚拟机

| 类/结构体 | 继承 | 实现 | 概括 |
|----------|------|------|------|
| `Value` | — | — | 运行时值：整数或字符串引用（索引到运行时字符串池） |
| `VMError` | `std::runtime_error` | — | 运行时错误异常 |
| `VM` | — | — | 寄存器式虚拟机：执行 8 字节/条指令的字节码 |

**包含：** `字节码.h`

**架构：** 值栈存放所有函数帧，每帧是一组连续虚拟寄存器。帧栈保存调用链帧基址，调用栈保存返回地址。

---

### `includes/REPL.h` — REPL 接口

| 函数 | 概括 |
|------|------|
| `编译并运行(源码)` | 完整流水线：词法分析 → 语法分析 → 语义分析 → 字节码生成 → VM 执行 |
| `运行文件(路径)` | 从 `.rua` 文件读取并编译运行 |
| `交互与执行()` | REPL 主循环：累积多行输入，遇到 "运行" 时编译执行 |
| `基本界面()` | 显示欢迎界面和平台信息 |
| `输入()` / `退出()` | 读一行 / 退出程序 |
| `获取系统用户名()` | 跨平台获取当前用户名（通过 arch::getUser） |
| `生成随机数(a, b)` | 生成随机整数 |

**包含：** `词法分析器.h` `语法分析器.h` `语义分析.h` `字节码.h` `虚拟机.h` `Console.h`

---

### `main.cpp` — 入口

| 全局变量 | 类型 | 概括 |
|---------|------|------|
| `全局内容` | `全局` | 全局状态：系统信息、用户名、ANSI 支持标志、喵语录 |

**入口逻辑：** `-h` → 显示帮助；有参数 → 运行文件；无参数 → 进入 REPL。

---

### `arch/` — 平台抽象层

所有平台相关代码集中在此目录，其余代码完全平台无关。

| 文件 | 概括 |
|------|------|
| `JITPlatform.h` | JIT 可执行内存管理接口（`#ifdef` 分支） |
| `JITPlatform_linux.h/.cpp` | Linux: `mmap`/`mprotect`/`sysconf` |
| `JITPlatform_windows.h/.cpp` | Windows: `VirtualAlloc`/`VirtualProtect`/`GetSystemInfo` |
| `Console.h` | 终端控制接口（`#ifdef` 分支） |
| `Console_linux.h/.cpp` | Linux: 终端标题、用户名、locale |
| `Console_windows.h/.cpp` | Windows: `SetConsoleTitle`/`GetUserNameA`/UTF-8 |

**设计原则：** `#ifdef` 仅存在于 arch/*.h 文件中，其余代码不使用平台宏。

---

### `src/JIT.cpp` — JIT 编译器

| 函数/类 | 概括 |
|---------|------|
| `JITCompiler::compile(prog)` | 编译整个程序，填充 BytecodeProgram 中的 jitFunc |
| `JITCompiler::compileFunction(...)` | 编译单个函数，返回 JIT 函数指针 |
| `JITCompiler::canJIT(func, prog)` | 判断函数是否适合 JIT（无 MOVS/PRINT） |

**寄存器映射 (x86-64)：**
```
v0 = RAX   (返回值)
v1 = RBX   (参数1, callee-saved)
v2 = R12   (callee-saved)
v3 = R13   (callee-saved)
v4 = R14   (callee-saved)
v5 = R15   (callee-saved)
v6 = RSI   (caller-saved)
v7 = RDI   (caller-saved)
v8 = RDX   (caller-saved)
v9 = RCX   (caller-saved)
v10 = R8   (caller-saved)
v11 = R9   (caller-saved)
v12 = R10  (caller-saved, 临时)
v13 = R11  (caller-saved, 临时)
v14+ = 栈溢出 (spill)
```

**函数调用约定 (SysV ABI)：**
- 参数通过 RDI, RSI, RDX, RCX, R8, R9 传递
- CALL 前保存 caller-saved 寄存器 (v6-v13)
- CALL 后恢复 caller-saved 寄存器
- 返回值在 RAX

**调试输出：**
- 编译时添加 `-D_DEBUG` 启用 JIT 调试信息
- 输出寄存器分配、CALL 处理、函数大小等信息

**包含：** `JIT.h` `JITPlatform.h`

---

### `includes/JIT.h` — JIT 编译器接口

| 类/函数 | 概括 |
|---------|------|
| `JITCompiler` | JIT 编译器类，管理代码缓冲区和编译流程 |
| `JITFunc` | JIT 函数指针类型：`int64_t (*)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t)` |
| `vregToPhys(vreg)` | 虚拟寄存器 → x86-64 物理寄存器映射 |
| `isCalleeSaved(preg)` | 判断物理寄存器是否为 callee-saved |

**条件编译：** 仅在 `#ifdef OPTIMIZATION` 下启用。

---

### 工具模块

| 文件 | 类/函数 | 概括 |
|------|---------|------|
| `平台检测.h` | `平台检测()` | 检测 OS 和 CPU 架构，返回描述字符串 |
| `全局内容.h` | `struct 全局` | 全局单例状态对象 |
| `信息上报.h` | `信息上报()` | 将错误码映射为中文错误信息并委托给异常处理 |
| `异常上报.h` | `异常处理()` | 中心化错误/警告处理：彩色输出 + 可选抛出异常 |
| `输出彩色支持.h` | `输出文本()` | ANSI 彩色终端输出 |
| `调试输出支持.h` | `调试输出()` | DEBUG 模式下的调试输出 |
| `UTF32支持.h/.cpp` | `UTF8转UTF32()` `UTF32转UTF8()` | UTF-8 ↔ UTF-32 编解码 |
| `main.h` | `初始化窗口()` | 控制台窗口初始化（通过 arch:: 调用） |

---

## 继承关系

```
std::runtime_error
  ├── ParserError         语法分析器.h
  ├── SemanticError       语义分析.h
  └── VMError             虚拟机.h

ASTVisitor (抽象接口)
  ├── SemanticAnalyzer    语义分析.h
  └── BytecodeGenerator   字节码.h

ASTNode (抽象基类)
  ├── Program             根节点
  ├── Function            函数定义
  ├── Block               语句块
  ├── VarDecl             变量声明
  ├── IfStmt              条件分支
  ├── WhileStmt           循环
  ├── ReturnStmt          返回语句
  ├── ExprStmt            表达式语句
  ├── BinaryExpr          二元运算
  ├── CallExpr            函数调用
  ├── NumberLiteral       整数
  ├── StringLiteral       字符串
  └── Identifier          标识符
```

---

## 包含依赖图

```
main.cpp
  └── REPL.h ──────────────────────────────────────────────────┐
       ├── 词法分析器.h                                          │
       │    └── 全局内容.h                                       │
       │         └── 平台检测.h (in main.cpp)                   │
       ├── 语法分析器.h                                          │
       │    ├── UTF32支持.h                                      │
       │    ├── 语法树.h                                         │
       │    └── 词法分析器.h                                     │
       ├── 语义分析.h                                            │
       │    └── 语法树.h                                         │
       ├── 字节码.h                                              │
       │    ├── 语法树.h                                         │
       │    └── 语义分析.h                                       │
       ├── 虚拟机.h                                              │
       │    └── 字节码.h                                         │
       └── Console.h ──→ arch/Console_linux.h 或 _windows.h     │
                                                                    │
信息上报.h ───→ 异常上报.h ───→ 输出彩色支持.h ───→ 全局内容.h ←──┘
```

---

1. **Visitor 模式**：语义分析和字节码生成都实现 `ASTVisitor`，分离关注点
2. **寄存器式 VM**：固定 8 字节指令，三地址码（rd, rs1, rs2）+ extra 字段
3. **平台抽象**：arch/ 目录封装所有平台相关代码，其余代码完全跨平台
