# easy/Makefile — Rua 简易版解释器
#
# 用法：
#   make             默认：JIT + 调试（-DOPTIMIZATION -DDEBUG）
#   make release     JIT、无调试输出（-DOPTIMIZATION）
#   make interp      纯解释（无 JIT、无调试）
#   make run         编译后运行示例 easy/example/fib.rua
#   make clean       删除构建产物
#
# 说明：
#   - 三种模式每次都重新编译（切换模式无需手动清理，产物均为 easy/rua）；
#   - 本 Makefile 面向 Linux / macOS（用 cc/gcc）；Windows 请用
#     easy/scripts/build_easy_windows.py（MSVC）。

CC     ?= cc
CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -DPLATFORM_LINUX
SRC     = rua.c
OUT     = rua

HDRS = includes/utf8.h includes/lexer.h includes/ast.h includes/parser.h   \
       includes/runtime.h includes/debug.h includes/jit.h                   \
       includes/jit/jit_base.h includes/jit/jit_emit.h                      \
       includes/jit/jit_analyze.h includes/jit/jit_codegen.h                \
       includes/jit/jit_debug.h

.PHONY: all release interp run clean

all:     CFLAGS += -DOPTIMIZATION -DDEBUG
release: CFLAGS += -DOPTIMIZATION

all release interp:
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

run: all
	./$(OUT) example/fib.rua

clean:
	rm -f $(OUT) a.out
