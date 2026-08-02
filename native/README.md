# Rua 原生运行时（Rust 解释器）

Rua 字节码文件（`.rab`）的原生运行时。第 1 期：寄存器式解释器（不含 JIT）。

## 构建

```bash
cargo build --release
# 产物：target/release/rua
```

## 用法

```bash
# 先用 C++ 编译器生成 .rab 字节码文件
../build/Rua example/calc_fib.rua -c -o calc_fib.rab

# 再用本运行时加载执行
./target/release/rua calc_fib.rab
```

## 环境变量

- `RUA_TRACE=1`：逐指令打印执行跟踪，并在超过 2000 步时终止（用于调试死循环）

## 目录结构

- `src/main.rs` — CLI 入口
- `src/rab.rs` — `.rab` 二进制格式解析（格式契约见 `docs/rab-format.md`）
- `src/vm.rs` — 寄存器式解释器，对齐 C++ `VM::run` 语义

## 后续

- 第 2 期：原生 JIT codegen（暂缓）
