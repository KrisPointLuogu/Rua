#pragma once
#include <string>
#include <vector>

using std::string;
using std::vector;

struct 全局
{
	string 系统信息;
	string 用户名;
	bool 支持ANSI;
	vector<string> 喵de语录 = {
		// 神经但可爱的东西...
		"[刘小黑]为什么要把我放在这个方括号里呢",
		"[刘小黑]LXH写代码累坏了唉",
		"[刘小黑]谢你愿意收养本喵在你的电脑里！",
		"[刘小黑]Github仓库里好黑...",
		"[刘小黑]哈，这是本喵的2.0！",
		"[刘小黑]Deep Seek给LXH立大功...",
		"[刘小黑]话说main是什么意思呢？也许你们都知道吧",
	};
};

extern 全局 全局内容;