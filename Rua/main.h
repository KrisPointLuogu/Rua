#pragma once
#include <cstdio>
#include <clocale>
#include <string>
#include "全局内容.h"

#ifdef _WIN32
    #include <windows.h>
#endif

#define 窗口名称 "Rua—X.X.X解释器"

void 初始化窗口() {
#if defined(_WIN32)
    // 窗口标题
    SetConsoleTitle(TEXT(窗口名称));
    // 输出与输入UTF8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 拿控制台句柄
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    // 启动彩色输出支持
    if (GetConsoleMode(hOut, &dwMode)) {
        // 兼容 Windows7/8沉余写法，尝试启动ANSI支持
        DWORD dwNewMode = dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING; //打开ANSI功能
        if (SetConsoleMode(hOut, dwNewMode)) {
            全局内容.支持ANSI = true;
        }
        else 全局内容.支持ANSI = false; // 系统不支持
    }
    else 全局内容.支持ANSI = false; // 控制台模式信息都没获取到

#elif defined(__linux__) || defined(__APPLE__)
    全局内容.支持ANSI = true; // linux和macos默认支持ANSI
    // 窗口标题
    printf("\033]0;%s\007", 窗口名称);
    fflush(stdout);
    // 输出与输入UTF8
    std::setlocale(LC_ALL, "");

#endif

}