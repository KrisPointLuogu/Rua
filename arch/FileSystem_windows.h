#pragma once
// arch/FileSystem_windows.h — Windows 平台文件读取
// 路径按 UTF-8 传入，内部转换为宽字符后使用 _wfopen 打开，
// 以支持中文等非 ASCII 路径（窄字符 fopen 无法处理）

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

namespace arch {
namespace detail {

inline bool winReadFile(const std::string& path, std::string& content)
{
	// UTF-8 → 宽字符路径
	int wLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	if (wLen <= 0) return false;

	std::vector<wchar_t> wPath(static_cast<size_t>(wLen));
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wPath.data(), wLen);

	FILE* 文件 = _wfopen(wPath.data(), L"rb");
	if (!文件) return false;

	fseek(文件, 0, SEEK_END);
	long 长度 = ftell(文件);
	fseek(文件, 0, SEEK_SET);

	content.resize(长度 < 0 ? 0 : static_cast<size_t>(长度));
	size_t 读取 = content.empty() ? 0 : fread(&content[0], 1, content.size(), 文件);
	fclose(文件);
	content.resize(读取);
	return true;
}

inline bool winWriteFile(const std::string& path, const std::string& content)
{
	// UTF-8 → 宽字符路径
	int wLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	if (wLen <= 0) return false;

	std::vector<wchar_t> wPath(static_cast<size_t>(wLen));
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wPath.data(), wLen);

	FILE* 文件 = _wfopen(wPath.data(), L"wb");
	if (!文件) return false;

	bool 成功 = true;
	if (!content.empty()) {
		成功 = fwrite(content.data(), 1, content.size(), 文件)
		      == content.size();
	}
	if (fclose(文件) != 0) 成功 = false;
	return 成功;
}

} // namespace detail
} // namespace arch
