#include "main.h"
#include "REPL.h"
#include "Globals.h"
#include "Platform.h"
#include "CommandLine.h"
#include <vector>

全局 全局内容;

int main(int argc, char* argv[])
{
#ifdef _DEBUG
#pragma message("当前处于DEBUG编译模式，调试功能将开启")
#endif

    全局内容.系统信息 = 平台检测();

    try {
        初始化窗口();

        // Windows 下 argv 为 ANSI 编码，统一经由 arch 转为 UTF-8
        std::vector<string> 参数 = arch::getCommandLineArgs(argc, argv);

        if (参数.size() > 1) {
            string 首参 = 参数[1];
            if (首参 == "-h" || 首参 == "--help") {
                printf("Rua — 中文编程语言解释器 v0.2.0\n");
                printf("\n用法：\n");
                printf("  %s <源文件.rua>    编译并运行\n", argv[0]);
                printf("  %s -h, --help      显示此帮助\n", argv[0]);
                return 0;
            }
            运行文件(首参);
        } else {
            交互与执行();
        }
    } catch (...) {
    }
    return 0;
}
