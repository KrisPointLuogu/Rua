#include <string>
#include <iostream>
#include "输出彩色支持.h"
#include "工具合集.h"
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

// 神经但可爱的东西...
string 喵de语录[] = {
	"[刘小黑]为什么要把我放在这个方括号里呢",
	"[刘小黑]LXH写代码累坏了唉",
	"[刘小黑]谢谢你愿意收养本喵在你的电脑里！",
	"[刘小黑]Github仓库里好黑...",
	"[刘小黑]哈哈，这是本喵的2.0！",
	"[刘小黑]Deep Seek给LXH立大功...",
	"[刘小黑]话说main是什么意思呢？",
};

void 基本界面() {
	输出文本("+——————————————————————————","青");
	输出文本("Rua—中文编程语言解释器");

#ifdef _DEBUG
	输出文本("当前处于[DEBUG/调试模式]\n请注意，这将会启用全部DEBUG功能，且存在与[RELEASE]模式行为不符的可能性","YY",true,"粗体");

	#if defined (_WIN64)
		输出文本("EXE位于 [WINDOWS x64] 系统编译","GG");
	#elif defined (__linux__)
		输出文本("EXE位于 [LINUX] 系统编译，功能可能出现不稳定或异常！","RR");
	#elif defined (__APPLE__)
		输出文本("EXE位于 [MACOS] 系统编译，功能可能出现不稳定或异常！","RR");
	#endif

#else
	输出文本("[刘小黑]"+ 获取系统用户名() + "泥嚎！");
	输出文本("喵de语录：" + 喵de语录[生成随机数(0, size(喵de语录) - 1)]);
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