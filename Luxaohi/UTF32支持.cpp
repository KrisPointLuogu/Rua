#include <string>
#include "信息上报.h"

using std::u32string;
using std::string;

u32string UTF8到UTF32(const string& UTF8) {
	/*
	* ASCII：1字节
	* 欧洲文字等：2字节
	* 中文与其他：3字节
	* 表情符号：4字节
	*/

	u32string 输出;
	size_t 位置 = 0;
	size_t 文本长度 = UTF8.length(); // 获取字节数

	while (位置 < 文本长度) {
		unsigned char 首字节  = static_cast<unsigned char>(UTF8[位置]);
		// 获取当前位置的首个字节内容
		char32_t 码点 = 0;
		int 剩余字节 = 0;

		// 判断当前UTF8字符的长度
		if ((首字节 & 0x80) == 0) {
			// ASCII
			码点 = 首字节;
			剩余字节 = 0;
			位置++;
		}
		else if ((首字节 & 0xE0) == 0xC0) {
			// 欧洲文字等
			码点 = 首字节 & 0x1F; // 0x1F = 0001 1111
			// 掩码，去除开头的"110"
			剩余字节 = 1;
			位置++;
		}
		else if ((首字节 & 0xF0) == 0xE0) {
			// 中文与其他
			码点 = 首字节 & 0x0F; // 0x0F = 0000 1111
			剩余字节 = 2;
			位置++;
		}
		else if ((首字节 & 0xF8) == 0xF0) {
			// 表情符号
			码点 = 首字节 & 0x07; // 0x07 = 0000 0111
			剩余字节 = 3;
			位置++;
		}
		else {
			// 啥也不是
			信息上报(非法UTF8起始字节, -1, 位置);

			// 跳过当前位置
			位置++;
			continue;
		}

		if (位置 + 剩余字节 > 文本长度) {
			信息上报(UTF8序列不完整, -1, 位置-1);
			break; // 字符串被截断，停止继续解析
		}

		/* 
		
		*(必填题目)请输入文本
		 [从这里继续]

		*/
	}

}