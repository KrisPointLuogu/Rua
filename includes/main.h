#pragma once
#include <clocale>
#include <cstdio>
#include <string>
#include "Console.h"
#include "Globals.h"

#define 窗口名称 "Rua—X.X.X解释器"

void 初始化窗口()
{
    // 初始化终端（UTF-8、ANSI 转义等）
    arch::initConsole();

    // 设置窗口标题
    arch::setConsoleTitle(窗口名称);
    fflush(stdout);

    // 设置 ANSI 支持标志
#if defined(_WIN32)
    // Windows 需要检测 ANSI 支持
    全局内容.支持ANSI = true; // initConsole 已启用
#elif defined(_LINUX)
    全局内容.支持ANSI = true; // Linux/macOS 默认支持
#endif
}
