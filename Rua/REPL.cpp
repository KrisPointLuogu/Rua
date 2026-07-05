#include <string>
#include <iostream>
#include "REPL.h"
#include "全局内容.h"
#include "输出彩色支持.h"
#include "词法分析器.h"

#ifdef _DEBUG
	#include "调试输出支持.h"
#endif

using std::string;
using std::size;
#ifdef _DEBUG
	using std::cout;
	using std::endl;
	using std::dec;
#endif

void 初始化() {

}

void 基本界面() {
	输出文本("+——————————————————————————","青");
	输出文本("Rua—中文编程语言解释器");

#ifdef _DEBUG
	输出文本("当前处于[DEBUG/调试模式]\n请注意，这将会启用全部DEBUG功能，且存在与[RELEASE]模式行为不符的可能性","YY",true,"粗体");

	// 用string::npos而不是-1
	if(全局内容.系统信息.find("Windows") != string::npos && 全局内容.系统信息.find("x86_64") != string::npos) 输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译", "GG");
	else if (全局内容.系统信息.find("Windows") != string::npos && 全局内容.系统信息.find("x86") != string::npos) 输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译，请注意，程序对此Windows版本可能存在兼容性问题", "YY");
	else if(全局内容.系统信息.find("Linux") != string::npos || 全局内容.系统信息.find("macOS") != string::npos) 输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译，当前存在不稳定或功能异常风险", "YY");
	else {
		输出文本("EXE位于 [" + 全局内容.系统信息 + "] 系统编译，如不稳定或功能异常风险导致任何形式的后果，作者概不负责", "RR");
		输出文本("[Use with caution / 谨慎使用]", "RR");
	}

#else
	输出文本("[刘小黑]"+ 全局内容.用户名 + "泥嚎！");
	输出文本("喵de语录：" + 全局内容.喵de语录[生成随机数(0, static_cast<int>(全局内容.喵de语录.size()) - 1)]);
#endif

	输出文本("+——————————————————————————","青");
}

string 输入() {
	// 输入
#ifdef _DEBUG
	输出文本("(DEBUG) ", "BB", false);
#else
	输出文本("(>ω<) ","BB",false);
#endif
	string 输入;
	std::getline(std::cin, 输入);

	if (std::cin.eof()) {
		return "EOF";
	}

	return 输入;
}

void 退出() {
	输出文本("[刘小黑]唔...先退出了嗷", "YY");
	exit(0);
}

bool 简单分析行(string& 行) {
#ifdef _DEBUG
	调试输出("输入的原始字节: ", "WW", false);
	for (unsigned char 字符 : 行) {
		std::cout << std::hex << (int)字符 << " ";
	}
	cout << std::dec << endl;
#endif
	if (行 == "") return 0;
	else if (行 == "EOF") 退出();
	return 1;
}

void 交互与执行() {
	全局内容.用户名 = 获取系统用户名();
	基本界面();
	while (1) {
		string 行;
		行 = 输入();
		if (简单分析行(行)) { 
			词法分析器类 分析实例(行);
			std::vector<词法分析器类::令牌> 令牌列表 = 分析实例.分析();
		}
	}
}