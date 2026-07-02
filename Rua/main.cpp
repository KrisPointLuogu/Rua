#include "REPL.h"
#include "工具合集.h"
#include <windows.h>

int main(){

#ifdef _DEBUG
	#pragma message("当前处于DEBUG编译模式，调试功能将开启")
#endif
	try{
		初始化窗口();
		交互与执行();
	}
	catch (...){}
	// 如果乱码了，就去【项目属性 → C/C++ → 命令行 → 其他选项】里加一条"/utf-8"试试吧
	return 0;
}