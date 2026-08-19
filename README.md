# ✨Rua — 中文编程语言
> 中国人也许可以试试用母语编程吧?

> 该项目可看作 Luxaohi 完全重构版本，项目隶属于 蔚蓝源序-AzureSource 团队。
> 本仓库为 **Rust 移植版**：忠实移植 C 版词法分析、语法分析与树遍历解释器，
> 另以纯安全代码实现了一个**纯整数快路径字节码 VM** 替代原 C 版 x86-64 JIT。

## ❗特性
- **全中文关键字** - 如果、那么、变量、喵叫。
- **开箱即用** - 单个可执行文件，无需安装（纯 Rust，零第三方依赖）。
- **学习友好** - 基本语法类似现有编程语言，上手可能较易。
- **JIT 快路径** - 纯整数函数自动编译为 x86-64 机器码（手写发射、零依赖），递归/循环提速一个数量级；非 x86-64 平台自动回退字节码 VM。

## ⚡构建与运行
```txt
# 需要 Rust 工具链（rustc 1.51+）
cargo build --release

# 运行示例
./target/release/rua example/example.rua
./target/release/rua example/fib.rua

# 命令行参数
./target/release/rua <源文件.rua> [--debug] [--dump] [--no-jit]
  --debug   输出词法/AST/函数表/耗时等调试信息
  --dump    转储可 VM 函数的字节码
  --no-jit  关闭 JIT（回退字节码 VM + 解释器）
  -h, --help  显示帮助
```

## 💻简单示例
```python
变量 number = "10"
喵叫(10 + number)
```

## 🧐已知事项
- **环境兼容性提示**：Rust 版为纯标准库实现，Linux / macOS / Windows 均可编译运行，
  中文输出在 Windows 终端可能需要 `chcp 65001`（UTF-8）。
- **行为差异**：运行时算术/数组错误携带精确源码行号（C 版部分错误固定为第 0 行）；
  可 VM/JIT 函数无显式 `返回` 时返回 0（与 C 版 JIT 行为一致，与树遍历解释器略有差异）；
  JIT 模式下除 0 由 CPU 触发异常终止进程（与 C 版 JIT 的未定义行为一致，解释器/VM 模式会正常报错）。
- **文档准确性**：Rua 编程语言关键词的意思与用法等文档可能会出现些许不准确的情况，
  如果您在使用过程中发现了文档与实际不相符的问题，欢迎您进行反馈，谢谢。

## 😁贡献
- 欢迎提交 Issue 和 Pull Request！

## ✏️作者
蔚蓝源序团队-AzureSource
[ [网站]](https://www.azuresrc.com)

## 📜许可证
本项目采用 [MIT License](LICENSE) 协议，允许自由使用、修改与分发，只需保留原始版权声明即可。

## ⬇️下载
[**下载Rua最新版**](https://github.com/LXH0525/Rua/releases/latest)
