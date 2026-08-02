#!/usr/bin/env python3
import subprocess, sys, os, pathlib, shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
NATIVE_DIR = ROOT / "native"
BUILD_DIR = ROOT / "build_windows"
BIN_NAME = "rua.exe"
DEST_NAME = "rua_native.exe"

def run(cmd, cwd=None):
    print(f"[RUN] {' '.join(cmd)}")
    r = subprocess.run(cmd, cwd=cwd or ROOT)
    if r.returncode != 0:
        print(f"[FAIL] exit code {r.returncode}", file=sys.stderr)
        sys.exit(r.returncode)

if __name__ == "__main__":
    BUILD_DIR.mkdir(exist_ok=True)
    run(["cargo", "build"], cwd=str(NATIVE_DIR))
    src = NATIVE_DIR / "target" / "debug" / BIN_NAME
    if not src.exists():
        print(f"[FAIL] 未找到编译产物: {src}", file=sys.stderr)
        sys.exit(1)
    shutil.copy2(str(src), str(BUILD_DIR / DEST_NAME))
    print(f"[OK] Rust 版 VM (Windows Debug) 编译完成，输出在 build_windows/{DEST_NAME}")
