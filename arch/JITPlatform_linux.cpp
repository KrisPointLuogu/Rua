// arch/JITPlatform_linux.cpp — Linux 平台实现

#include "JITPlatform.h"

namespace arch {

uint8_t* allocateExecutableMemory(size_t size)
{
	uint8_t* ptr = detail::linuxAllocate(size);
	if (ptr == (uint8_t*)MAP_FAILED) return nullptr;
	return ptr;
}

bool protectExecutableMemory(uint8_t* ptr, size_t size, int prot)
{
	return detail::linuxProtect(ptr, size, prot);
}

size_t getPageSize() { return detail::linuxPageSize(); }

} // namespace arch
