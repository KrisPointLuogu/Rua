#!/usr/bin/env python3
import subprocess, sys, os, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build"

def run(cmd, cwd=None):
    print(f"[RUN] {' '.join(cmd)}")
    r = subprocess.run(cmd, cwd=cwd or ROOT)
    if r.returncode != 0:
        print(f"[FAIL] exit code {r.returncode}", file=sys.stderr)
        sys.exit(r.returncode)

if __name__ == "__main__":
    run(["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR),
         "-DCMAKE_BUILD_TYPE=Debug",
         "-DCMAKE_CXX_FLAGS=-Ofast -Wall -march=native -flto -DOPTIMIZATION -D_DEBUG"])
    run(["cmake", "--build", str(BUILD_DIR), "-j", str(os.cpu_count() or 4)])
    print("[OK] 优化版编译完成，输出在 build/")
