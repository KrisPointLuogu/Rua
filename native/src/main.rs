//! Rua 字节码 (.rab) 原生运行时 —— 命令行入口

mod rab;
mod vm;

use std::process;

fn main() {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 2 {
        eprintln!("用法: {} <文件.rab>", args[0]);
        process::exit(2);
    }

    let path = &args[1];

    if path == "-h" || path == "--help" {
        println!("Rua 原生运行时 (Rust 解释器)");
        println!();
        println!("用法:");
        println!("  {} <文件.rab>   加载并执行 Rua 字节码文件", args[0]);
        process::exit(0);
    }

    match rab::load(path) {
        Ok(program) => {
            match vm::run(&program) {
                Ok(()) => {}
                Err(e) => {
                    eprintln!("运行时错误：{}", e.message);
                    process::exit(1);
                }
            }
        }
        Err(e) => {
            eprintln!("加载失败：{}", e.message);
            process::exit(1);
        }
    }
}
