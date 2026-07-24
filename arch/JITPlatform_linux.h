#pragma once
// arch/JITPlatform_linux.h — Linux 平台实现

#include <sys/mman.h>
#include <unistd.h>

namespace arch {
namespace detail {

inline size_t linuxPageSize() { return (size_t)sysconf(_SC_PAGESIZE); }

inline uint8_t* linuxAllocate(size_t size)
{
	return (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
						  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

inline bool linuxProtect(uint8_t* ptr, size_t size, int prot)
{
	int flags = 0;
	if (prot & 1) flags |= PROT_READ;
	if (prot & 2) flags |= PROT_WRITE;
	if (prot & 4) flags |= PROT_EXEC;
	return mprotect(ptr, size, flags) == 0;
}

} // namespace detail
} // namespace arch
