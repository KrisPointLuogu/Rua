#!/usr/bin/env python3
import subprocess, sys, os, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build_windows"

def run(cmd, cwd=None):
    print(f"[RUN] {' '.join(cmd)}")
    r = subprocess.run(cmd, cwd=cwd or ROOT)
    if r.returncode != 0:
        print(f"[FAIL] exit code {r.returncode}", file=sys.stderr)
        sys.exit(r.returncode)

if __name__ == "__main__":
    run(["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR),
         "-G", "Visual Studio 17 2022", "-A", "x64",
         "-DCMAKE_BUILD_TYPE=Release"])
    run(["cmake", "--build", str(BUILD_DIR), "--config", "Release", "-j", str(os.cpu_count() or 4)])
    print("[OK] Windows Release 编译完成，输出在 build_windows/")
