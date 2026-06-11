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
#include <cstring>
#include <locale>
#include <random>
#include <string>

using std::string;

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


string 获取系统用户名() {
#if defined(IS_WINDOWS)
    char 名字[256];
    DWORD 大小 = 256;
    // 获取并返回string类型用户名
    if (GetUserNameA(名字, &大小)) return string(名字);
    return string("Administrator");

#elif defined(IS_LINUX) || defined(IS_MACOS)
    char* 名字 = getenv("USER");

    // 返回string类型用户名
    if (名字 != nullptr && strlen(名字) > 0) return string(名字);
    return string("User");

#endif

}


int 生成随机数(const int 左边, const int 右边) {
    // 获取种子
    std::random_device 种子;

    // 用梅森旋转算法引擎
    std::mt19937 引擎(种子());
    std::uniform_int_distribution<> 配置(左边, 右边);

    // 返回int类型决定的随机数
    return int(配置(引擎));
}

返回内容 ab匹配(
    string& 代码_,
    size_t& 位置_,
    int& 行位置_,
    int& 列位置_,
    const char a边,
    const char b边
) {
    if (代码_[位置_] == a边) {
        // 找到起点边，开始匹配

        if (位置_ + 1 < 代码_.size()) {
            // 防止访问越界
            位置_++; 列位置_++;
        }
        else {
            // 如果条件不满足，那么这个匹配不可能成功
            return ab没有闭合;
        }

        bool 闭合了 = false;
        while (位置_ < 代码_.size()) {

            if (代码_[位置_] == '\n') { 行位置_++; 列位置_++; 位置_++; continue; } // 识别换行

            if (代码_[位置_] == b边) {
                // 找到终点边，匹配完成
                位置_++; 列位置_++; // 跨过终点这一个字符
                闭合了 = true;
                break;
            }

            位置_++; 列位置_++;
            // 没找到就到下一个位置等待再次检查

        }

        if (闭合了 == true) {
            return 成功;
        }
        else {
            return ab没有闭合;
        }
    }
}