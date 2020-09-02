/*
 * Based on John the Ripper and modified to integrate with aircrack
 *
 * 	John the Ripper copyright and license.
 *
 * John the Ripper password cracker,
 * Copyright (c) 1996-2013 by Solar Designer.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * As a special exception to the GNU General Public License terms,
 * permission is hereby granted to link the code of this program, with or
 * without modification, with any version of the OpenSSL library and/or any
 * version of unRAR, and to distribute such linked combinations.  You must
 * obey the GNU GPL in all respects for all of the code used other than
 * OpenSSL and unRAR.  If you modify this program, you may extend this
 * exception to your version of the program, but you are not obligated to
 * do so.  (In other words, you may release your derived work under pure
 * GNU GPL version 2 or later as published by the FSF.)
 *
 * (This exception from the GNU GPL is not required for the core tree of
 * John the Ripper, but arguably it is required for -jumbo.)
 *
 * 	Relaxed terms for certain components.
 *
 * In addition or alternatively to the license above, many components are
 * available to you under more relaxed terms (most commonly under cut-down
 * BSD license) as specified in the corresponding source files.
 *
 * For more information on John the Ripper licensing please visit:
 *
 * http://www.openwall.com/john/doc/LICENSE.shtml
 *
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-98,2010,2012 by Solar Designer
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h> /* for isprint() */
#if HAVE_MEMALIGN && HAVE_MALLOC_H
#include <malloc.h>
#endif

#include "aircrack-ng/ce-wpa/arch.h"
#include "aircrack-ng/ce-wpa/misc.h"
#include "aircrack-ng/ce-wpa/memory.h"
#include "aircrack-ng/ce-wpa/jcommon.h"
#include "aircrack-ng/ce-wpa/johnswap.h"

unsigned int mem_saving_level = 0;

// Add 'cleanup' methods for the mem_alloc_tiny.  VERY little cost, but
// allows us to check for mem leaks easier.
struct rm_list
{
	void * mem;
	struct rm_list * next;
};
static struct rm_list * mem_alloc_tiny_memory;

static void add_memory_link(void * v)
{
	struct rm_list * p = (struct rm_list *) mem_alloc(sizeof(struct rm_list));
	p->next = mem_alloc_tiny_memory;
	p->mem = v;
	mem_alloc_tiny_memory = p;
}
// call at program exit.
void cleanup_tiny_memory(void)
{
	struct rm_list *p = mem_alloc_tiny_memory, *p2;
	for (;;)
	{
		if (!p) return;
		free(p->mem);
		p2 = p->next;
		free(p);
		p = p2;
	}
}

void * mem_alloc_func(size_t size)
{
	void * res;

	if (!size) return NULL;
	res = malloc(size);
	if (!res)
	{
		fprintf(stderr,
				"mem_alloc(): %s trying to allocate " Zu " bytes\n",
				strerror(ENOMEM),
				size);
		perror("mem_alloc");
	}

	return res;
}

void * mem_calloc_func(size_t count, size_t size)
{
	void * res;

	if (!count || !size) return NULL;
	res = calloc(count, size);
	if (!res)
	{
		fprintf(stderr,
				"mem_calloc(): %s trying to allocate " Zu " bytes\n",
				strerror(ENOMEM),
				count * size);
		perror("mem_calloc");
	}

	return res;
}

/*
 * if -DDEBUG we turn mem_alloc_tiny() to essentially be just a malloc()
 * with additional alignment. The reason for this is it's way easier to
 * trace bugs that way.
 * Also, with -DDEBUG we always return exactly the requested
 * alignment, in order to trigger bugs!
 */
#ifdef DEBUG
#undef MEM_ALLOC_SIZE
#define MEM_ALLOC_SIZE 0
#endif
void * mem_alloc_tiny_func(size_t size, size_t align)
{
	static char * buffer = NULL;
	static size_t bufree = 0;
	size_t mask;
	char * p;

#ifdef DEBUG
	/*
	 * We may be called with size zero, for example from ldr_load_pw_line()
	 * that calls mem_alloc_copy() with format->params.salt_size as size.
	 * This causes problems with -DDEBUG without this fix because we never
	 * get out of the while loop when MEM_ALLOC_SIZE is zero too. The
	 * previous fix for this was returning NULL but that lead to other
	 * problems that I did not bother digging into. This fix should be
	 * 100% safe.
	 */
	if (size == 0) size = 1;
#endif

#if ARCH_ALLOWS_UNALIGNED
	if (mem_saving_level > 2 && align < MEM_ALIGN_SIMD) align = MEM_ALIGN_NONE;
#endif

	mask = align - 1;

	do
	{
		if (buffer)
		{
			size_t need = size + mask - (((size_t) buffer + mask) & mask);
			if (bufree >= need)
			{
				p = buffer;
				p += mask;
				p -= (size_t) p & mask;
				bufree -= need;
				buffer = p + size;
#if defined(DEBUG)
				/* Ensure alignment is no better than requested */
				if (((size_t) p & ((mask << 1) + 1)) == 0) p += align;
#endif
				return p;
			}
		}

		if (size + mask > MEM_ALLOC_SIZE || bufree > MEM_ALLOC_MAX_WASTE) break;
		buffer = (char *) mem_alloc(MEM_ALLOC_SIZE);
		add_memory_link((void *) buffer);
		bufree = MEM_ALLOC_SIZE;
	} while (1);

	p = (char *) mem_alloc(size + mask);
	if (p == NULL) abort();
	add_memory_link((void *) p);
	p += mask;
	p -= (size_t) p & mask;
#if defined(DEBUG)
	/* Ensure alignment is no better than requested */
	if (((size_t) p & ((mask << 1) + 1)) == 0) p += align;
#endif
	return p;
}

void * mem_calloc_tiny_func(size_t size, size_t align)
{
	char * cp = (char *) mem_alloc_tiny(size, align);
	memset(cp, 0, size);
	return cp;
}

void * mem_alloc_copy_func(void * src, size_t size, size_t align)
{
	return memcpy(mem_alloc_tiny(size, align), src, size);
}

void * mem_alloc_align_func(size_t size, size_t align)
{
	void * ptr = NULL;
#if HAVE_POSIX_MEMALIGN
	if (posix_memalign(&ptr, align, size)) pexit("posix_memalign");
#elif HAVE_ALIGNED_ALLOC
	/* According to the Linux man page, "size should be a multiple of
	   alignment", whatever they mean with "should"... This does not
	   make any sense whatsoever but we round it up to comply. */
	size = ((size + (align - 1)) / align) * align;
	if (!(ptr = aligned_alloc(align, size))) pexit("aligned_alloc");
#elif HAVE_MEMALIGN
/* Let's just pray this implementation can actually free it */
#if defined(__sparc__) || defined(__sparc) || defined(sparc)                   \
	|| defined(__sparcv9)
	if (!(ptr = memalign(align, size)))
#else
	if (!(ptr = memalign(&ptr, align, size)))
#endif
		perror("memalign");
#elif HAVE___MINGW_ALIGNED_MALLOC
	if (!(ptr = __mingw_aligned_malloc(size, align)))
		perror("__mingw_aligned_malloc");
#elif HAVE__ALIGNED_MALLOC
	if (!(ptr = _aligned_malloc(size, align))) perror("aligned_malloc");

#elif AC_BUILT
#error No suitable aligned alloc found, please report to john-dev mailing list (state your OS details).

/* we need an aligned alloc function for legacy builds */
#elif _ISOC11_SOURCE
	size = ((size + (align - 1)) / align) * align;
	if (!(ptr = aligned_alloc(align, size))) perror("aligned_alloc");
#else
	if (posix_memalign(&ptr, align, size)) perror("posix_memalign");
#endif
	return ptr;
}

void * mem_calloc_align_func(size_t count, size_t size, size_t align)
{
	void * ptr = mem_alloc_align_func(size * count, align);

	memset(ptr, 0, size * count);
	return ptr;
}

char * str_alloc_copy_func(char * src)
{
	size_t size;

	if (!src) return "";
	if (!*src) return "";

	size = strlen(src) + 1;
	return (char *) memcpy(mem_alloc_tiny(size, MEM_ALIGN_NONE), src, size);
}

void dump_text(void * in, int len)
{
	unsigned char * p = (unsigned char *) in;

	while (len--)
	{
		fputc(isprint(*p) ? *p : '.', stdout);
		p++;
	}
	fputc('\n', stdout);
}

void dump_stuff_noeol(void * x, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) x)[i]);
		if ((i % 4) == 3) printf(" ");
	}
}
void dump_stuff(void * x, unsigned int size)
{
	dump_stuff_noeol(x, size);
	printf("\n");
}
void dump_stuff_msg(const void * msg, void * x, unsigned int size)
{
	printf("%s : ", (char *) msg);
	dump_stuff(x, size);
}
void dump_stuff_msg_sepline(const void * msg, void * x, unsigned int size)
{
	printf("%s :\n", (char *) msg);
	dump_stuff(x, size);
}

void dump_stuff_be_noeol(void * x, unsigned int size)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) x)[i ^ 3]);
		if ((i % 4) == 3) printf(" ");
	}
}
void dump_stuff_be(void * x, unsigned int size)
{
	dump_stuff_be_noeol(x, size);
	printf("\n");
}
void dump_stuff_be_msg(const void * msg, void * x, unsigned int size)
{
	printf("%s : ", (char *) msg);
	dump_stuff_be(x, size);
}
void dump_stuff_be_msg_sepline(const void * msg, void * x, unsigned int size)
{
	printf("%s :\n", (char *) msg);
	dump_stuff_be(x, size);
}

void alter_endianity(void * _x, unsigned int size)
{
	ARCH_WORD_32 * x = (ARCH_WORD_32 *) _x;

	// size is in octets
	size >>= 2;

#if !ARCH_ALLOWS_UNALIGNED
	if (is_aligned(x, sizeof(ARCH_WORD_32)))
	{
#endif
		while (size--)
		{
			*x = JOHNSWAP(*x);
			x++;
		}
#if !ARCH_ALLOWS_UNALIGNED
	}
	else
	{
		unsigned char *cpX, c;

		cpX = (unsigned char *) x;
		while (size--)
		{
			c = *cpX;
			*cpX = cpX[3];
			cpX[3] = c;
			c = cpX[1];
			cpX[1] = cpX[2];
			cpX[2] = c;
			cpX += 4;
		}
	}
#endif
}

#if defined(SIMD_COEF_32) || defined(NT_X86_64) || defined(SIMD_PARA_MD5)      \
	|| defined(SIMD_PARA_MD4) || defined(SIMD_PARA_SHA1)
#ifndef SIMD_COEF_32
#define SIMD_COEF_32 4
#endif
#ifndef SIMD_COEF_64
#define SIMD_COEF_64 2
#endif
#ifndef SIMD_COEF_32
#define SIMD_COEF_32 4
#endif

// These work for standard SIMD_COEF_32 buffers, AND for SSEi MMX_PARA multiple
// SIMD_COEF_32 blocks, where index will be mod(X * SIMD_COEF_32) and not simply
// mod(SIMD_COEF_32)
#define SHAGETPOS(i, index)                                                    \
	((index & (SIMD_COEF_32 - 1)) * 4                                          \
	 + ((i) & (0xffffffff - 3)) * SIMD_COEF_32                                 \
	 + (3 - ((i) &3))                                                          \
	 + (unsigned int) index / SIMD_COEF_32 * SHA_BUF_SIZ * 4                   \
		   * SIMD_COEF_32) // for endianity conversion
#define SHAGETOUTPOS(i, index)                                                 \
	((index & (SIMD_COEF_32 - 1)) * 4                                          \
	 + ((i) & (0xffffffff - 3)) * SIMD_COEF_32                                 \
	 + (3 - ((i) &3))                                                          \
	 + (unsigned int) index / SIMD_COEF_32 * 20                                \
		   * SIMD_COEF_32) // for endianity conversion
// for MD4/MD5 or any 64 byte LE SSE interleaved hash
#define GETPOS(i, index)                                                       \
	((index & (SIMD_COEF_32 - 1)) * 4                                          \
	 + ((i) & (0xffffffff - 3)) * SIMD_COEF_32                                 \
	 + ((i) &3)                                                                \
	 + (unsigned int) index / SIMD_COEF_32 * 64 * SIMD_COEF_32)
#define GETOUTPOS(i, index)                                                    \
	((index & (SIMD_COEF_32 - 1)) * 4                                          \
	 + ((i) & (0xffffffff - 3)) * SIMD_COEF_32                                 \
	 + ((i) &3)                                                                \
	 + (unsigned int) index / SIMD_COEF_32 * 16 * SIMD_COEF_32)
// for SHA384/SHA512 128 byte BE interleaved hash (arrays of 16 8 byte ints)
#define SHA64GETPOS(i, index)                                                  \
	((index & (SIMD_COEF_64 - 1)) * 8                                          \
	 + ((i) & (0xffffffff - 7)) * SIMD_COEF_64                                 \
	 + (7 - ((i) &7))                                                          \
	 + (unsigned int) index / SIMD_COEF_64 * SHA_BUF_SIZ * 8 * SIMD_COEF_64)
#define SHA64GETOUTPOS(i, index)                                               \
	((index & (SIMD_COEF_64 - 1)) * 8                                          \
	 + ((i) & (0xffffffff - 7)) * SIMD_COEF_64                                 \
	 + (7 - ((i) &7))                                                          \
	 + (unsigned int) index / SIMD_COEF_64 * 64 * SIMD_COEF_64)

// for SHA384/SHA512 128 byte FLAT interleaved hash (arrays of 16 8 byte ints),
// but we do not BE interleave.
#define SHA64GETPOSne(i, index)                                                \
	((index & (SIMD_COEF_64 - 1)) * 8                                          \
	 + ((i) & (0xffffffff - 7)) * SIMD_COEF_64                                 \
	 + ((i) &7)                                                                \
	 + (unsigned int) index / SIMD_COEF_64 * SHA_BUF_SIZ * 8 * SIMD_COEF_64)

void dump_stuff_mmx_noeol(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[GETPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
}
void dump_stuff_mmx(void * buf, unsigned int size, unsigned int index)
{
	dump_stuff_mmx_noeol(buf, size, index);
	printf("\n");
}
void dump_stuff_mmx_msg(const void * msg,
						void * buf,
						unsigned int size,
						unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_stuff_mmx(buf, size, index);
}
void dump_stuff_mmx_msg_sepline(const void * msg,
								void * buf,
								unsigned int size,
								unsigned int index)
{
	printf("%s :\n", (char *) msg);
	dump_stuff_mmx(buf, size, index);
}
void dump_out_mmx_noeol(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[GETOUTPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
}
void dump_out_mmx(void * buf, unsigned int size, unsigned int index)
{
	dump_out_mmx_noeol(buf, size, index);
	printf("\n");
}
void dump_out_mmx_msg(const void * msg,
					  void * buf,
					  unsigned int size,
					  unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_out_mmx(buf, size, index);
}
void dump_out_mmx_msg_sepline(const void * msg,
							  void * buf,
							  unsigned int size,
							  unsigned int index)
{
	printf("%s :\n", (char *) msg);
	dump_out_mmx(buf, size, index);
}

#if defined(SIMD_PARA_MD5)
#define GETPOSMPARA(i, index)                                                  \
	((index & (SIMD_COEF_32 - 1)) * 4                                          \
	 + (((i) & (0xffffffff - 3)) % 64) * SIMD_COEF_32                          \
	 + (i / 64) * SIMD_COEF_32 * SIMD_PARA_MD5 * 64                            \
	 + ((i) &3)                                                                \
	 + (unsigned int) index / SIMD_COEF_32 * 64 * SIMD_COEF_32)
// multiple para blocks
void dump_stuff_mpara_mmx_noeol(void * buf,
								unsigned int size,
								unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[GETPOSMPARA(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
}
void dump_stuff_mpara_mmx(void * buf, unsigned int size, unsigned int index)
{
	dump_stuff_mpara_mmx_noeol(buf, size, index);
	printf("\n");
}
// obuf has to be at lease size long.  This function will unwind the SSE-para
// buffers into a flat.
void getbuf_stuff_mpara_mmx(unsigned char * oBuf,
							void * buf,
							unsigned int size,
							unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
		*oBuf++ = ((unsigned char *) buf)[GETPOSMPARA(i, index)];
}
void dump_stuff_mpara_mmx_msg(const void * msg,
							  void * buf,
							  unsigned int size,
							  unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_stuff_mpara_mmx(buf, size, index);
}
void dump_stuff_mpara_mmx_msg_sepline(const void * msg,
									  void * buf,
									  unsigned int size,
									  unsigned int index)
{
	printf("%s :\n", (char *) msg);
	dump_stuff_mpara_mmx(buf, size, index);
}
#endif

void dump_stuff_shammx(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[SHAGETPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
	printf("\n");
}
void dump_stuff_shammx_msg(const void * msg,
						   void * buf,
						   unsigned int size,
						   unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_stuff_shammx(buf, size, index);
}
void dump_out_shammx(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[SHAGETOUTPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
	printf("\n");
}
void dump_out_shammx_msg(const void * msg,
						 void * buf,
						 unsigned int size,
						 unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_out_shammx(buf, size, index);
}

void dump_stuff_shammx64(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[SHA64GETPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
	printf("\n");
}
void dump_stuff_shammx64_msg(const void * msg,
							 void * buf,
							 unsigned int size,
							 unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_stuff_shammx64(buf, size, index);
}
void dump_stuff_mmx64(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[SHA64GETPOSne(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
	printf("\n");
}
void dump_stuff_mmx64_msg(const void * msg,
						  void * buf,
						  unsigned int size,
						  unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_stuff_mmx64(buf, size, index);
}

void dump_out_shammx64(void * buf, unsigned int size, unsigned int index)
{
	unsigned int i;
	for (i = 0; i < size; i++)
	{
		printf("%.2x", ((unsigned char *) buf)[SHA64GETOUTPOS(i, index)]);
		if ((i % 4) == 3) printf(" ");
	}
	printf("\n");
}
void dump_out_shammx64_msg(const void * msg,
						   void * buf,
						   unsigned int size,
						   unsigned int index)
{
	printf("%s : ", (char *) msg);
	dump_out_shammx64(buf, size, index);
}
#endif

void alter_endianity_w(void * _x, unsigned int count)
{
	int i = -1;
	ARCH_WORD_32 * x = (ARCH_WORD_32 *) _x;
#if ARCH_ALLOWS_UNALIGNED
	while (++i < (int) count)
	{
		x[i] = JOHNSWAP(x[i]);
	}
#else
	unsigned char *cpX, c;
	if (is_aligned(x, sizeof(ARCH_WORD_32)))
	{
		// we are in alignment.
		while (++i < (int) count)
		{
			x[i] = JOHNSWAP(x[i]);
		}
		return;
	}
	// non-aligned data :(
	cpX = (unsigned char *) x;
	while (++i < (int) count)
	{
		c = *cpX;
		*cpX = cpX[3];
		cpX[3] = c;
		c = cpX[1];
		cpX[1] = cpX[2];
		cpX[2] = c;
		cpX += 4;
	}
#endif
}

void alter_endianity_w64(void * _x, unsigned int count)
{
	int i = -1;
	ARCH_WORD_64 * x = (ARCH_WORD_64 *) _x;
#if ARCH_ALLOWS_UNALIGNED
	while (++i < (int) count)
	{
		x[i] = JOHNSWAP64(x[i]);
	}
#else
	unsigned char *cpX, c;
	if (is_aligned(x, sizeof(ARCH_WORD_64)))
	{
		// we are in alignment.
		while (++i < (int) count)
		{
			x[i] = JOHNSWAP64(x[i]);
		}
		return;
	}
	// non-aligned data :(
	cpX = (unsigned char *) x;
	while (++i < (int) count)
	{
		c = *cpX;
		*cpX = cpX[7];
		cpX[7] = c;
		c = cpX[1];
		cpX[1] = cpX[6];
		cpX[6] = c;
		c = cpX[2];
		cpX[2] = cpX[5];
		cpX[5] = c;
		c = cpX[3];
		cpX[3] = cpX[4];
		cpX[4] = c;
		cpX += 8;
	}
#endif
}
