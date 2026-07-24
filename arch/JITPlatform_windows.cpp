// arch/JITPlatform_windows.cpp — Windows 平台实现

#include "JITPlatform.h"

namespace arch {

uint8_t* allocateExecutableMemory(size_t size)
{
	return detail::winAllocate(size);
}

bool protectExecutableMemory(uint8_t* ptr, size_t size, int prot)
{
	return detail::winProtect(ptr, size, prot);
}

size_t getPageSize() { return detail::winPageSize(); }

} // namespace arch
