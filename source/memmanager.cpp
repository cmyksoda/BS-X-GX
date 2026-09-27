/****************************************************************************
 * Snes9x GX
 *
 * Daryl Borth 2026
 *
 * memmanager.cpp
 *
 * Memory manager
 ***************************************************************************/

#include <ogc/system.h>
#include <malloc.h>
#include "snes9xgx.h"
#include "memmanager.h"
#include "snes9x/memmap.h"

#define MEM2_SIZE		(42*1024*1024)

static mspace extmem_space = NULL;
u8 * romPtr = NULL;

void InitMemManager ()
{
	void *base_ptr = NULL;
	size_t capacity = 0;

	base_ptr = SYS_AllocArenaMem2Hi(MEM2_SIZE, 32);
	capacity = MEM2_SIZE;

	extmem_space = create_mspace_with_base(base_ptr, capacity, 0);
	mspace_set_footprint_limit(extmem_space, capacity);

	romPtr = (uint8 *) memalign(32, Memory.MAX_ROM_SIZE + 0x200 + 0x8000);
}

void* extmem_malloc(u32 size)
{
	return mspace_malloc(extmem_space, size);
}

char* extmem_strdup(const char *s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *dup = (char *)extmem_malloc(len);

    if (dup)
        memcpy(dup, s, len);

    return dup;
}

void extmem_free(void *ptr)
{
	mspace_free(extmem_space, ptr);
}

