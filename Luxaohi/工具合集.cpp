#define _CRT_SECURE_NO_WARNINGS

#if defined(_WIN64)
#define IS_WINDOWS 1
#include <windows.h>

#elif defined(__linux__)
#define IS_LINUX 1
#include <cstdio>

#elif defined(__APPLE__)
#define IS_MACOS 1
#include <cstdio>

#else
    #error "不支持的平台-Unsupported_platforms"

#endif


#include "工具合集.h"
#include <locale>
#include <codecvt>
#include <iostream>
#include <vector>

void 初始化窗口() {

#if defined(IS_WINDOWS)
    // 窗口标题
    SetConsoleTitle(L"Luxaohi—X.X.X解释器");
    // 输出与输入UTF8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 启动彩色输出支持
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

#elif defined(IS_LINUX) || defined(IS_MACOS)
    // 窗口标题
    printf("\033]0;Luxaohi—X.X.X解释器\007");
    fflush(stdout);
    // 输出与输入UTF8
    std::setlocale(LC_ALL, "");

#endif

}