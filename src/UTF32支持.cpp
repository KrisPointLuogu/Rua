#include <string>
#include "信息上报.h"
#include "UTF32支持.h"

using std::string;
using std::u32string;

u32string UTF8转UTF32(const string &UTF8)
{
	/*
	 * ASCII：1字节
	 * 欧洲文字等：2字节
	 * 中文与其他：3字节
	 * 表情符号：4字节
	 */

	u32string 输出;
	size_t 位置 = 0;
	size_t 文本长度 = UTF8.length(); // 获取字节数

	while (位置 < 文本长度)
	{
		unsigned char 首字节 = static_cast<unsigned char>(UTF8[位置]);
		// 获取当前位置的首个字节内容
		char32_t 码点 = 0;
		int 剩余字节 = 0;

		// 判断当前UTF8字符的长度
		if ((首字节 & 0x80) == 0)
		{
			// ASCII
			码点 = 首字节;
			剩余字节 = 0;
			位置++;
		}
		else if ((首字节 & 0xE0) == 0xC0)
		{
			// 欧洲文字等
			码点 = 首字节 & 0x1F; // 0x1F = 0001 1111
			// 掩码，去除开头的"110"
			剩余字节 = 1;
			位置++;
		}
		else if ((首字节 & 0xF0) == 0xE0)
		{
			// 中文与其他
			码点 = 首字节 & 0x0F; // 0x0F = 0000 1111
			剩余字节 = 2;
			位置++;
		}
		else if ((首字节 & 0xF8) == 0xF0)
		{
			// 表情符号
			码点 = 首字节 & 0x07; // 0x07 = 0000 0111
			剩余字节 = 3;
			位置++;
		}
		else
		{
			// 啥也不是
			信息上报(非法UTF8起始字节, -1, -1);

			// 跳过当前位置
			位置++;
			continue;
		}

		// 这个是检查剩余字节够不够的
		if (位置 + 剩余字节 > 文本长度)
		{
			信息上报(UTF8序列不完整, -1, -1);
			break; // 字符串被截断，停止继续解析
		}

		bool flag = false;
		for (int 当前字节位置 = 0; 当前字节位置 < 剩余字节; 当前字节位置++)
		{
			unsigned char 当前字节 = static_cast<unsigned char>(UTF8[位置 + 当前字节位置]);
			// 获取当前字节

			if ((当前字节 & 0xC0) != 0x80)
			{
				// 说明当前字节不是后续字节
				信息上报(非法的UTF8续字节, -1, -1);
				flag = true;
			}

			码点 = (码点 << 6) | (当前字节 & 0x3F);
			// 写入码点
		}

		if (flag)
		{
			// 跳过当前位置
			位置 += 剩余字节;
			continue;
		}

		// 检查码点大小，不超出Unicode最大值就过
		if (码点 > 0x10FFFF)
		{
			信息上报(码点大小超出Unicode最大值, -1, -1);
			// 跳过当前位置
			位置++;
			continue;
		}

		// 检查有没有代理码点，这个在UTF32里不能有
		if (码点 >= 0xD800 && 码点 <= 0xDFFF)
		{
			信息上报(意外的代理码点, -1, -1);

			// 跳过当前位置
			位置 += 剩余字节;
			continue;
		}

		// 检查有没有过长编码
		if (
			剩余字节 == 1 && 码点 < 0x80 ||
			剩余字节 == 2 && 码点 < 0x800 ||
			剩余字节 == 3 && 码点 < 0x10000)
		{
			// 到这里说明有过长编码
			信息上报(过长编码, -1, -1);

			// 跳过
			位置 += 剩余字节;
			continue;
		}

		// 通过检查，加入输出中
		输出.push_back(码点);
		位置 += 剩余字节;
	}

	// 输出！
	return 输出;
}

string UTF32转UTF8(const u32string &UTF32)
{

	string 输出;
	输出.reserve(UTF32.length() * 4); // 预分配空间

	for (char32_t 码点 : UTF32)
	{

		if (码点 <= 0x7F)
		{
			// 1字节
			输出.push_back(static_cast<char>(码点));
			// C++风格的static_cast<char>更安全，也更规范
		}
		else if (码点 <= 0x7FF)
		{
			// 2字节
			unsigned char 字节1 = 0xC0 | ((码点 >> 6) & 0x1F);
			unsigned char 字节2 = 0x80 | (码点 & 0x3F);
			输出.push_back(static_cast<char>(字节1));
			输出.push_back(static_cast<char>(字节2));
		}
		else if (码点 <= 0xFFFF)
		{
			// 3字节
			unsigned char 字节1 = 0xE0 | ((码点 >> 12) & 0x0F);
			unsigned char 字节2 = 0x80 | ((码点 >> 6) & 0x3F);
			unsigned char 字节3 = 0x80 | (码点 & 0x3F);
			输出.push_back(static_cast<char>(字节1));
			输出.push_back(static_cast<char>(字节2));
			输出.push_back(static_cast<char>(字节3));
		}
		else if (码点 <= 0x10FFFF)
		{
			// 4字节
			unsigned char 字节1 = 0xF0 | ((码点 >> 18) & 0x07);
			unsigned char 字节2 = 0x80 | ((码点 >> 12) & 0x3F);
			unsigned char 字节3 = 0x80 | ((码点 >> 6) & 0x3F);
			unsigned char 字节4 = 0x80 | (码点 & 0x3F);
			输出.push_back(static_cast<char>(字节1));
			输出.push_back(static_cast<char>(字节2));
			输出.push_back(static_cast<char>(字节3));
			输出.push_back(static_cast<char>(字节4));
		}
		else
		{
			// 码点超出Unicode上限的情况
			信息上报(码点大小超出Unicode最大值, -1, -1);
			// 用替换字符 U+FFFD 替代
			输出.push_back(static_cast<char>(0xEF));
			输出.push_back(static_cast<char>(0xBF));
			输出.push_back(static_cast<char>(0xBD));
		}
	}
	// 完成，输出！
	return 输出;
}