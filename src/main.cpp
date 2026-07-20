#include "REPL.h"
#include "main.h"
#include "全局内容.h"
#include "平台检测.h"

全局 全局内容;

int main(int argc, char *argv[])
{
#ifdef _DEBUG
#pragma message("当前处于DEBUG编译模式，调试功能将开启")
#endif

	全局内容.系统信息 = 平台检测();

	try
	{
		初始化窗口();

		if (argc > 1)
		{
			string 参数 = argv[1];
			if (参数 == "-h" || 参数 == "--help")
			{
				printf("Rua — 中文编程语言解释器 v0.2.0\n");
				printf("\n用法：\n");
				printf("  %s <源文件.rua>    编译并运行\n", argv[0]);
				printf("  %s -h, --help      显示此帮助\n", argv[0]);
				return 0;
			}
			运行文件(参数);
		}
		else
		{
			交互与执行();
		}
	}
	catch (...)
	{
	}
	return 0;
}
