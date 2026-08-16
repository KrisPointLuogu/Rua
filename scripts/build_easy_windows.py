#!/usr/bin/env python3
"""编译 easy/（C 版简易解释器）Windows 版本（MSVC cl.exe）。

需要 Visual Studio 开发环境，且在开发者命令行中运行
（保证 cl.exe、link.exe 在 PATH 中）。

用法:
    python3 scripts/build_easy_windows.py [--no-jit] [--no-debug]

    --no-jit    关闭 JIT（不加 /DOPTIMIZATION，纯解释）
    --no-debug  关闭调试输出（不加 /DDEBUG）

默认：JIT 开启（/DOPTIMIZATION）+ 调试开启（/DDEBUG）。
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "rua.c"
OUT = ROOT / "rua.exe"


def run(cmd):
    print(f"[RUN] {' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode != 0:
        print(f"[FAIL] exit code {r.returncode}", file=sys.stderr)
        sys.exit(r.returncode)


if __name__ == "__main__":
    args = sys.argv[1:]
    cflags = ["/nologo", "/std:c11", "/O2", "/W4", "/DPLATFORM_WINDOWS"]
    if "--no-jit" not in args:
        cflags.append("/DOPTIMIZATION")
    if "--no-debug" not in args:
        cflags.append("/DDEBUG")
    run(["cl", *cflags, str(SRC), f"/Fe:{OUT}"])
    print(f"[OK] Windows 版编译完成: {OUT}")
