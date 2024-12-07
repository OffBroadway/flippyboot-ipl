#include <stdint.h>
#include <gctypes.h>

void DI_InitQueue();

int DI_Open(const char *path, uint8_t type, uint8_t flags);
int DI_Read(void* dst, unsigned int len, uint64_t offset, unsigned int fd);
