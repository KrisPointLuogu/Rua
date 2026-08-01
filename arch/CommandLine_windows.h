#pragma once
// arch/CommandLine_windows.h — Windows 平台命令行参数
// main 的 argv 使用 ANSI 代码页编码，中文参数会乱码，
// 故从 GetCommandLineW 取得宽字符命令行并转为 UTF-8

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace arch {
namespace detail {

inline std::vector<std::string> winCommandLine()
{
	std::vector<std::string> 参数;

	int 数量 = 0;
	LPWSTR* 宽参数 = CommandLineToArgvW(GetCommandLineW(), &数量);
	if (!宽参数) return 参数;

	for (int i = 0; i < 数量; i++) {
		int 长度
			= WideCharToMultiByte(CP_UTF8, 0, 宽参数[i], -1, nullptr, 0, nullptr, nullptr);
		if (长度 <= 0) {
			参数.emplace_back();
			continue;
		}
		std::string 文本(static_cast<size_t>(长度) - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, 宽参数[i], -1, &文本[0], 长度, nullptr, nullptr);
		参数.push_back(文本);
	}

	LocalFree(宽参数);
	return 参数;
}

} // namespace detail
} // namespace arch
