#pragma once
// arch/JITPlatform_windows.h — Windows 平台实现

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace arch {
namespace detail {

inline size_t winPageSize()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return (size_t)si.dwPageSize;
}

inline uint8_t* winAllocate(size_t size)
{
	return (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE,
								  PAGE_EXECUTE_READWRITE);
}

inline bool winProtect(uint8_t* ptr, size_t size, int prot)
{
	DWORD flProtect = PAGE_NOACCESS;
	if (prot == 0)
		flProtect = PAGE_NOACCESS;
	else if (prot == 1)
		flProtect = PAGE_READONLY;
	else if (prot == 2)
		flProtect = PAGE_READWRITE;
	else if (prot == 4)
		flProtect = PAGE_EXECUTE;
	else if (prot == 5)
		flProtect = PAGE_EXECUTE_READ;
	else if (prot == 7)
		flProtect = PAGE_EXECUTE_READWRITE;

	DWORD oldProtect;
	return VirtualProtect(ptr, size, flProtect, &oldProtect) != 0;
}

} // namespace detail
} // namespace arch
