# Rua 解释器（Rust 版）— API 文档

> 本文档描述 `src/` 下 Rust 实现的模块结构、数据结构与编译流水线，
> 以及它与原 C 版（includes/*.h）的对应关系。
>
> 想按「一次运行的旅程」顺序了解工作流程，请读 [workflow/ 系列](workflow/)：
> [01-总览](workflow/01-总览.md) ·
> [02-词法与语法分析](workflow/02-词法与语法分析.md) ·
> [03-运行时](workflow/03-运行时.md) ·
> [04-快路径](workflow/04-快路径.md) ·
> [05-错误处理](workflow/05-错误处理.md) ·
> [06-调试工具](workflow/06-调试工具.md)。

## 一、总览：整条流水线

```
        main()                      src/main.rs
          │
          ├──> read_file(path) ───────────── 读取源码（UTF-8）
          │
          ├──> lex_all(src) ──────────────── 词法分析 → Vec<Token>
          │        └──> Lexer::lex_next() ── 逐 Token 扫描（含字符串/注释/关键字）
          │
          ├──> Parser::parse_tokens(toks) ── 语法分析 → (prog, fns)
          │        └──> parse_expr/parse_stmt/parse_block/parse_function 递归下降
          │
          ├──> Runtime::build(&fns) ──────── 预编译可 VM / 可 JIT 函数
          │        ├──> vm::compile_functions ── 可 VM 判定 + 闭包传播 + 字节码生成
          │        └──> jit::compile_functions ── 可 JIT 判定 + 两遍编译 + x86-64 机器码
          │
          └──> Runtime::run(&prog) ───────── 解释执行
                   └──> Runtime::exec 递归分发
                        ├──> env_get/env_set/env_bind  变量读写
                        ├──> exec_call                 函数调用
                        │     ├──> jit::invoke         JIT 函数（默认开启）
                        │     ├──> vm::invoke          VM 函数（--no-jit 或不可 JIT）
                        │     └──> 树遍历解释          其余函数
                        ├──> apply_binop              二元运算
                        ├──> array_get/array_set      数组读写
                        └──> print_val                喵叫输出
```

错误处理统一走 `RuaError { line, msg }`，由 `main()` 格式化「第 N 行：消息」到
stderr 并 `exit(1)`。

## 二、模块与 C 版对应关系

| Rust 模块 | C 版对应 | 职责 |
| --- | --- | --- |
| `src/ast.rs` | `includes/ast.h` + `includes/utf8.h`(error_at) | `Node` 枚举、`RuaError`、`Result` |
| `src/lexer.rs` | `includes/lexer.h` + `includes/utf8.h` | `TokenKind`/`Token`/`Lexer`（UTF-8 由 `char` 码点迭代承担） |
| `src/parser.rs` | `includes/parser.h` | 递归下降语法分析（优先级爬升） |
| `src/runtime.rs` | `includes/runtime.h` | `Value`/`Env`/`Ctx`、树遍历解释、二元运算 |
| `src/vm.rs` | 字节码 VM（中间层） | 纯整数快路径栈机字节码 VM（`--no-jit` 时的快路径） |
| `src/jit.rs` | `includes/jit*.h` | x86-64 JIT：AST 直编机器码（手写发射、零依赖） |
| `src/debug.rs` | `includes/debug_process.h` + `includes/bytecode.h` | `--debug`/`--dump` 输出与计时 |
| `src/main.rs` | `rua.c` | CLI 入口 |

## 三、核心数据结构

### `ast::Node`（对应 C 版通用 `struct Node`）
Rust 枚举按类型精确承载字段：

```rust
pub enum Node {
    Num { line, num: i64 },
    Str { line, text: String },
    Ident { line, name },
    Var { line, name, right },
    Array { line, name, len, right },        // 广播初始值
    Assign { line, target, right },
    Index { line, base, index },
    Binary { line, op: TokenKind, left, right },
    Call { line, name, args },
    If { line, cond, then, else_b: Option<Box<Node>> },
    While { line, cond, body },
    Return { line, value: Option<Box<Node>> },
    Block { line, stmts },
    Prog { stmts },
    Func { line, name, params, body },
}
```

`Node::kind()` / `Node::line()` 对应 C 版 `kind`/`line` 字段访问。

### `runtime::Value`（对应 C 版标签联合 `Value`）

```rust
pub enum Value {
    Num(i64),
    Str(Rc<String>),
    Arr(Rc<RefCell<Vec<Value>>>),   // 数组句柄共享 + 原地可变
}
```

数组保持 C 版「引用语义」：赋值、传参只复制句柄，多个变量共享同一份数组。

### `runtime::Env`（对应 C 版作用域链）

```rust
pub struct Env {
    parent: Option<Rc<RefCell<Env>>>,
    bindings: Vec<Binding>,          // (name, Value)
}
```

- `env_bind`：当前作用域绑定新变量；
- `env_get`：沿链向上查找（找不到报「未定义的变量」）；
- `env_set`：沿链找到即改，各层都找不到则在当前作用域新建绑定（隐式声明）。

### `ast::RuaError`

```rust
pub struct RuaError { line: usize, msg: String }
```

全项目统一，`main()` 收尾格式化后 `exit(1)`。

## 四、编译流水线细节

### 词法分析（lexer.rs）
- 中文关键字：喵 / 变量 / 如果 / 那么 / 否则 / 当 / 返回 / 喵叫 / 数组；
- 字符串字面量：支持 `""` `''` 与中文全角 `“”` `‘’`，英文引号内支持
  `\n \t \r \\ \" \'` 转义；
- 注释：`#` 单行、`#* ... *#` 多行；
- 标识符字符：英文/数字/下划线/常用汉字（U+4E00~U+9FA5）。

### 语法分析（parser.rs）
- 表达式用「优先级爬升」统一处理二元运算符（`==`/`!=`=1、比较=2、加减=3、
  乘除模=4）；
- 语句按关键字分发：变量/数组/如果/当/返回/表达式语句；
- 顶层 `喵 ...` 单独收集为函数定义（不进入程序语句列表）。

### 运行时（runtime.rs）
- `exec` 按 `Node` 变体递归求值；返回语句通过 `Ctx.returning` 标志向上传递；
- `apply_binop` 按 `TokenKind` 分发：`+` 支持字符串拼接，`==`/`!=` 支持字符串
  内容比较，其余算术/比较要求操作数为数字；
- 函数调用 `exec_call`：`喵叫` 逐参打印（空格分隔、末尾换行）；用户函数新建
  子环境绑形参，优先走 JIT（可 JIT 且启用时），否则可 VM 走 `vm::invoke`，
  其余树遍历解释。

## 五、x86-64 JIT（jit.rs）

> 对应 C 版 includes/jit*.h。把「可 JIT」的纯整数函数直接从 AST 编译成
> x86-64 机器码（SysV ABI），**手写字节发射、零第三方依赖**（可执行内存用
> `extern "C"` 直接链接 libc 的 `mmap`）。默认开启，`--no-jit` 关闭回退 VM。

### 可 JIT 判定（共享 vm.rs 的 scan 判定）
可 JIT = 可 VM（节点/引用/闭包判定，见 §六）&& **参数 ≤6**（SysV 寄存器
参数上限）&& 平台为 Linux x86-64。闭包传播同 VM（调用不可 JIT → 自己也不可 JIT）。

### 固定 vreg→物理寄存器映射（SysV）
```
v0  → RAX     结果/返回值
v1  → RBX     形参1（序言从 RDI 拷入）
v2  → R12     形参2（RSI）   v3 → R13（RDX）
v4  → R14     形参4（RCX）   v5 → R15（R8）
v6  → RSI     形参6（R9）    v7 → RDI   v8 → RDX
v9  → RCX     v10 → R8       v11 → R9   v12 → R10  v13 → R11
v14+ → 栈溢出槽 [rbp - k*8]
```
- 序言：`push rbp; mov rbp,rsp` → push 用到的 callee-saved（RBX/R12~R15）→
  `sub rsp, frame` → 清 RAX → 形参从 ABI 参数寄存器拷到 v1..vP；
- 帧对齐：入口 RSP ≡ 8 (mod 16)，`(pushed + frame) ≡ 8 (mod 16)` 反推帧大小；
- 调用/除模/打印前把 caller-saved（RSI/RDI/RDX/RCX/R8~R11）快照进帧内槽，
  调用后恢复（对应 C 版 jit_save_callersaved / jit_restore_callersaved）。

### 表达式 → 机器码
- `N_NUM` → `mov reg, imm32/64`；`N_IDENT` → 按变量表 vreg 拷贝；
- `N_BINARY`：加减乘走 `RAX` 累加 + `add/sub/imul`；`/` `%` 走 `cqo + idiv`
  （先快照 caller-saved，算完恢复，取模再 `mov rax, rdx`）；比较走 `cmp +
  setcc + movzx`；
- `N_CALL`：实参装入 ABI 参数寄存器（caller-saved 源从快照槽读）→ 经
  函数地址表间接 `call`（`rax = [table + idx*8]`）→ 结果从 `RAX` 存回 dst。

### 函数内 喵叫
- 字符串字面量实参：字面量进数据区，`mov rdi, 数据区地址`；
- 整数实参：先 `jit_itoa` 格式化进全局 scratch，再 `jit_print(msg, endl)`
  （末参换行 `\n`、非末参空格 ` `，与解释器逐字节一致）；
- 辅助函数 `jit_print` / `jit_itoa` 为 `#[no_mangle] extern "C"`，机器码直接调用。

### 两遍编译 + 函数地址表
1. 逐个 plan（可 JIT 判定 + 变量表 + 帧布局）→ 闭包传播；
2. 第一遍给可 JIT 函数在 `entries`（g_jit_table）中占位；
3. 第二遍逐个发射机器码，写入可执行内存，地址写回表。
   递归/互调在运行期查表，天然正确。

### 与 C 版 JIT 的差异
- 仅 Linux x86-64（`cfg` 隔离）；C 版另有 Windows x64；
- 除 0 由 CPU 触发 SIGFPE 终止进程（C 版 JIT 同为未定义行为；VM/解释器会报错）；
- `--debug` 对 JIT 只输出「函数清单表」（已 JIT / 未 JIT），不做机器码反汇编。

## 六、字节码 VM（vm.rs）

### 可 VM 判定（对应 C 版 JIT eligibility）
函数全部满足才编译为字节码：
1. 节点类型受限：无 `N_STR`/`N_ARRAY`/`N_INDEX`（`N_STR` 仅允许作 `喵叫` 实参）；
2. 所有 `N_IDENT` 都必须是形参或函数内 `变量` 声明的局部变量（引用全局 → 不可 VM）；
3. 被调用函数也必须可 VM（闭包传播，迭代求解）。

### 栈机指令
`Push / PushStr / LoadVar / StoreVar / Add Sub Mul Div Mod / Eq Ne Gt Lt Ge Le /
Jmp Jz / Call / CallPrint / Ret / Pop`。

### 两遍编译
1. 预扫描判定 + 变量槽位分配 + 闭包传播；
2. 给可 VM 函数按序分配索引（递归/互调编译期可引用），再逐个生成字节码。

### 执行（invoke）
- 帧栈模拟调用；`Call` 压新帧（形参入槽、局部槽补 0），`Ret` 弹帧并把结果
  压回调用方栈顶；
- `CallPrint` 对应内置 `喵叫`（整数与字符串字面量实参）；
- 递归/互调经函数索引表间接调用，运行期天然正确。

### 与 C 版 JIT 的差异
- 用安全栈机字节码替代手写 x86-64 机器码，无 `unsafe`；
- 无寄存器参数上限（放宽到 64）；
- 无显式 `返回` 的函数返回 0（与 C 版 JIT 语义一致；树遍历解释器返回函数体
  最后一条语句的值）。

## 七、CLI

```
rua <源文件.rua> [--debug] [--dump] [--no-jit]
```

| 参数 | 行为 |
| --- | --- |
| 无参数 | 打印用法，`exit(1)` |
| `-h` / `--help` | 打印用法，`exit(0)` |
| `--debug` | 输出词法 Token 明细、AST、函数表（含 JIT/VM 状态）、各阶段耗时 |
| `--dump` | 转储可 VM 函数的字节码（反汇编文本） |
| `--jit` / `--no-jit` | 开启（默认）/ 关闭 JIT，关闭后回退「VM + 解释器」 |

参数顺序无关；`--debug`/`--dump` 输出到 stderr，不污染程序 stdout。

## 八、构建与验证

```bash
cargo build               # debug
cargo build --release     # release
./target/release/rua example/example.rua        # 默认 JIT
./target/release/rua example/example.rua --no-jit   # VM + 解释器对拍
```

JIT 与 VM 两种模式的输出应逐字节一致（可用 `--no-jit` 互相参照）。

## 九、移植说明（相对 C 版的行为差异）

| 项 | C 版 | Rust 版 |
| --- | --- | --- |
| 运行时算术错误行号 | 部分固定为 0 | 精确源码行号 |
| 可编译函数无返回语句 | JIT 返回 0 | 同（JIT/VM 返回 0） |
| 数组语义 | 句柄共享 | 同（`Rc<RefCell<Vec<Value>>>`） |
| JIT | x86-64 机器码（Windows+Linux） | 手写 x86-64（仅 Linux，`cfg` 隔离） |
| JIT 除 0 | 未定义行为 | SIGFPE 终止（VM/解释器正常报错） |
| 调试输出 | `-DDEBUG` 编译宏 | `--debug`/`--dump` 命令行参数 |
