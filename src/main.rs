// src/main.rs — Rua 简易解释器入口
//
// 对应 C 版 rua.c。编译流水线：
//   读取文件 -> 词法分析 -> 语法分析 -> 预编译可 VM 函数 -> 解释执行。
//
// 用法： rua <源文件.rua> [--debug] [--dump]
//   - 无参数：打印用法，退出码 1（与 C 版一致）；
//   - -h/--help：打印用法，退出码 0（与 C 版一致）；
//   - --debug：输出词法 Token 明细、AST、函数表与可 VM 状态、各阶段耗时
//     （对应 C 版 -DDEBUG 的 debug_process.h 输出，改为运行时开关）；
//   - --dump：转储可 VM 函数的字节码（对应 C 版 bytecode.h 的文本转储）。
//   --debug / --dump 输出到 stderr，不污染程序 stdout。
//
// 错误处理：run() 返回 Result<RuaError>，main() 收尾格式化
// 「第 N 行：消息」到 stderr 并以退出码 1 结束（对应 C 版 error_at）。

mod ast;
mod debug;
mod lexer;
mod parser;
mod runtime;
mod vm;

use std::process::exit;

use ast::{Result, RuaError};
use lexer::lex_all;
use parser::Parser;
use runtime::Runtime;

/**
 * 打印用法帮助（对应 C 版 main 的帮助输出）。
 *
 * @param prog 程序名（用于「用法：」行）
 */
fn usage(prog: &str) {
    println!("Rua 简易解释器");
    println!("用法： {} <源文件.rua> [--debug] [--dump]", prog);
    println!("  --debug   输出词法/AST/函数表/耗时等调试信息");
    println!("  --dump    转储可 VM 函数的字节码");
    println!("  -h, --help  显示本帮助");
}

/**
 * 读取整个文件到内存（UTF-8）。
 *
 * 对应 C 版 read_file()。打开失败或非 UTF-8 时报错（RuaError）。
 *
 * @param path 文件路径
 * @return 文件内容字符串
 */
fn read_file(path: &str) -> Result<String> {
    let bytes =
        std::fs::read(path).map_err(|_| RuaError::new(0, format!("无法打开文件：{}", path)))?;
    String::from_utf8(bytes).map_err(|_| RuaError::new(0, "文件不是有效的 UTF-8"))
}

/**
 * 主流程：解析命令行参数 -> 读取文件 -> 编译 -> 执行。
 *
 * @param args 去掉程序名后的命令行参数（顺序无关，最后一个位置参数为文件路径）
 * @return 成功返回 Ok(())，失败返回 RuaError（由 main 统一格式化）
 */
fn run(args: Vec<String>) -> Result<()> {
    let mut debug_on = false;
    let mut dump = false;
    let mut file: Option<String> = None;

    // 参数顺序无关：--debug/--dump/-h/--help 是开关，其余视为文件路径。
    for a in args {
        match a.as_str() {
            "--debug" => debug_on = true,
            "--dump" => dump = true,
            "-h" | "--help" => {
                usage("rua");
                return Ok(());
            }
            _ => file = Some(a),
        }
    }

    let path = match file {
        Some(p) => p,
        None => {
            usage("rua");
            exit(1);
        }
    };

    let src = read_file(&path)?;
    if debug_on {
        eprintln!("[调试] 读取文件：{}（{} 字节）", path, src.len());
    }

    let toks = lex_all(&src)?;
    if debug_on {
        debug::dump_tokens(&toks);
    }

    let (prog, fns) = Parser::parse_tokens(toks)?;
    if debug_on {
        debug::dump_prog(&prog);
    }

    let build_t = if debug_on {
        Some(debug::Timer::new("VM 预编译"))
    } else {
        None
    };
    let rt = Runtime::build(&fns);
    drop(build_t);

    if debug_on {
        debug::dump_functions(&rt.fns, &rt.vm);
    }
    if dump {
        debug::dump_bytecode(&rt.vm);
    }

    let run_t = if debug_on {
        Some(debug::Timer::new("运行时间"))
    } else {
        None
    };
    let _ = rt.run(&prog)?;
    drop(run_t);

    Ok(())
}

/**
 * 程序入口（对应 C 版 main）。
 *
 * 收集命令行参数交给 run()；错误统一格式化「第 N 行：消息」到 stderr
 * 并以退出码 1 结束（对应 C 版 error_at）。
 */
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if let Err(e) = run(args) {
        eprintln!("{}", e);
        exit(1);
    }
}
