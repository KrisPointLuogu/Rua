#pragma once
// arch/Console_windows.h — Windows 平台终端控制

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef _DEBUG
#pragma comment(lib, "advapi32.lib")
#endif

namespace arch {
namespace detail {

inline void winSetTitle(const char* title)
{
	// 将 UTF-8 标题转换为宽字符
	int wLen = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
	if (wLen > 0) {
		wchar_t* wTitle
			= (wchar_t*)HeapAlloc(GetProcessHeap(), 0, wLen * sizeof(wchar_t));
		if (wTitle) {
			MultiByteToWideChar(CP_UTF8, 0, title, -1, wTitle, wLen);
			SetConsoleTitleW(wTitle);
			HeapFree(GetProcessHeap(), 0, wTitle);
		}
	}
}

inline const char* winGetUser()
{
	static char name[256] = { 0 };
	DWORD size = 256;
	if (GetUserNameA(name, &size)) return name;
	return nullptr;
}

inline void winInitConsole()
{
	// 设置控制台输出代码页为 UTF-8
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	// 启用 ANSI 转义序列支持
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut != INVALID_HANDLE_VALUE) {
		DWORD dwMode = 0;
		if (GetConsoleMode(hOut, &dwMode)) {
			dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
			SetConsoleMode(hOut, dwMode);
		}
	}
}

} // namespace detail
} // namespace arch
