/*
 * Copyright (c) 2016, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __INCLUDE_ALLOC__
#define __INCLUDE_ALLOC__

#include <stdlib.h>
#include <sof/string.h>
#include <stdint.h>
#include <sof/bit.h>
#include <sof/platform.h>
#include <platform/platform.h>
#include <arch/spinlock.h>
#include <uapi/ipc/topology.h>
struct sof;

/* Heap Memory Zones
 *
 * The heap has three different zones from where memory can be allocated :-
 *
 * 1) System Zone. Fixed size heap where alloc always succeeds and is never
 * freed. Used by any init code that will never give up the memory.
 *
 * 2) Runtime Zone. Main and larger heap zone where allocs are not guaranteed to
 * succeed. Memory can be freed here.
 *
 * 3) Buffer Zone. Largest heap zone intended for audio buffers.
 *
 * 4) System Runtime Zone. Heap zone intended for runtime objects allocated
 * by the kernel part of the code.
 *
 * See platform/memory.h for heap size configuration and mappings.
 */

/* heap zone types */
#define RZONE_SYS		BIT(0)
#define RZONE_RUNTIME		BIT(1)
#define RZONE_BUFFER		BIT(2)
#define RZONE_SYS_RUNTIME	BIT(3)

/* heap zone flags */
#define RZONE_FLAG_UNCACHED	BIT(4)

#define RZONE_TYPE_MASK	0xf
#define RZONE_FLAG_MASK	0xf0

struct dma_copy;
struct dma_sg_config;

struct mm_info {
	uint32_t used;
	uint32_t free;
};

struct block_hdr {
	uint16_t size;		/* size in blocks for continuous allocation */
	uint16_t used;		/* usage flags for page */
} __packed;

struct block_map {
	uint16_t block_size;	/* size of block in bytes */
	uint16_t count;		/* number of blocks in map */
	uint16_t free_count;	/* number of free blocks */
	uint16_t first_free;	/* index of first free block */
	struct block_hdr *block;	/* base block header */
	uint32_t base;		/* base address of space */
} __aligned(PLATFORM_DCACHE_ALIGN);

#define BLOCK_DEF(sz, cnt, hdr) \
	{.block_size = sz, .count = cnt, .free_count = cnt, .block = hdr, \
	 .first_free = 0}

struct mm_heap {
	uint32_t blocks;
	struct block_map *map;
	uint32_t heap;
	uint32_t size;
	uint32_t caps;
	struct mm_info info;
} __aligned(PLATFORM_DCACHE_ALIGN);

/* heap block memory map */
struct mm {
	/* system heap - used during init cannot be freed */
	struct mm_heap system[PLATFORM_HEAP_SYSTEM];
	/* system runtime heap - used for runtime system components */
	struct mm_heap system_runtime[PLATFORM_HEAP_SYSTEM_RUNTIME];
	/* general heap for components */
	struct mm_heap runtime[PLATFORM_HEAP_RUNTIME];
	/* general component buffer heap */
	struct mm_heap buffer[PLATFORM_HEAP_BUFFER];

	struct mm_info total;
	uint32_t heap_trace_updated;	/* updates that can be presented */
	spinlock_t lock;	/* all allocs and frees are atomic */
} __aligned(PLATFORM_DCACHE_ALIGN);

/* heap allocation and free */
void *_malloc(int zone, uint32_t caps, size_t bytes);
void *_zalloc(int zone, uint32_t caps, size_t bytes);
void *_balloc(int zone, uint32_t caps, size_t bytes);
void rfree(void *ptr);

#if CONFIG_DEBUG_HEAP

#define rmalloc(zone, caps, bytes)			\
	({void *_ptr;					\
	do {						\
		_ptr = _malloc(zone, caps, bytes);	\
		if (!_ptr) {				\
			trace_error(TRACE_CLASS_MEM,	\
				   "failed to alloc 0x%x bytes caps 0x%x", \
				   bytes, caps);	\
			alloc_trace_runtime_heap(zone, caps, bytes);	\
		}					\
	} while (0);					\
	_ptr; })

#define rzalloc(zone, caps, bytes)			\
	({void *_ptr;					\
	do {						\
		_ptr = _zalloc(zone, caps, bytes);	\
		if (!_ptr) {				\
			trace_error(TRACE_CLASS_MEM,	\
				   "failed to alloc 0x%x bytes caps 0x%x", \
				   bytes, caps);	\
			alloc_trace_runtime_heap(zone, caps, bytes);	\
		}					\
	} while (0);					\
	_ptr; })

#define rballoc(zone, caps, bytes)			\
	({void *_ptr;					\
	do {						\
		_ptr = _balloc(zone, caps, bytes);	\
		if (!_ptr) {				\
			trace_error(TRACE_CLASS_MEM,	\
				   "failed to alloc 0x%x bytes caps 0x%x", \
				   bytes, caps);	\
			alloc_trace_buffer_heap(zone, caps, bytes);	\
		}					\
	} while (0);					\
	_ptr; })

void alloc_trace_runtime_heap(int zone, uint32_t caps, size_t bytes);
void alloc_trace_buffer_heap(int zone, uint32_t caps, size_t bytes);

#else

#define rmalloc(zone, caps, bytes)	_malloc(zone, caps, bytes)
#define rzalloc(zone, caps, bytes)	_zalloc(zone, caps, bytes)
#define rballoc(zone, caps, bytes)	_balloc(zone, caps, bytes)

#endif

/* system heap allocation for specific core */
void *rzalloc_core_sys(int core, size_t bytes);

/* utility */
#define bzero(ptr, size) \
	arch_bzero(ptr, size)

int rstrlen(const char *s);
int rstrcmp(const char *s1, const char *s2);

/* Heap save/restore contents and context for PM D0/D3 events */
uint32_t mm_pm_context_size(void);
int mm_pm_context_save(struct dma_copy *dc, struct dma_sg_config *sg);
int mm_pm_context_restore(struct dma_copy *dc, struct dma_sg_config *sg);

/* heap initialisation */
void init_heap(struct sof *sof);

/* frees entire heap (supported for slave core system heap atm) */
void free_heap(int zone);

/* status */
void heap_trace_all(int force);
void heap_trace(struct mm_heap *heap, int size);

#endif
