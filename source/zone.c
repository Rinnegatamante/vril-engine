/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// zone.c

#include "nzportable_def.h"

// cypress -- who the fuck needs a 250kB zone block?? what?? restoring to 50kB.
#define DYNAMIC_SIZE	0xc000

#define	ZONEID	0x1d4a11
#define MINFRAGMENT	64

typedef struct memblock_s
{
	int	size;		// including the header and possibly tiny fragments
	int	tag;		// a tag of 0 is a free block
	int	id;		// should be ZONEID
	int	pad;		// pad to 64 bit boundary
	struct	memblock_s	*next, *prev;
} memblock_t;

typedef struct
{
	int		size;		// total bytes malloced, including header
	memblock_t	blocklist;	// start / end cap for linked list
	memblock_t	*rover;
} memzone_t;

void Cache_FreeLow (int new_low_hunk);
void Cache_FreeHigh (int new_high_hunk);

#ifdef PSP_VFPU
void* memcpy_vfpu( void* dst, void* src, unsigned int size )
{
	u8* src8 = (u8*)src;
	u8* dst8 = (u8*)dst;

	// < 8 isn't worth trying any optimisations...
	if (size<8) goto bytecopy;

	// < 64 means we don't gain anything from using vfpu...
	if (size<64)
	{
		// Align dst on 4 bytes or just resume if already done
		while (((((u32)dst8) & 0x3)!=0) && size) {
			*dst8++ = *src8++;
			size--;
		}
		if (size<4) goto bytecopy;

		// We are dst aligned now and >= 4 bytes to copy
		u32* src32 = (u32*)src8;
		u32* dst32 = (u32*)dst8;
		switch(((u32)src8)&0x3)
		{
			case 0:
				while (size&0xC)
				{
					*dst32++ = *src32++;
					size -= 4;
				}
				if (size==0) return (dst);		// fast out
				while (size>=16)
				{
					*dst32++ = *src32++;
					*dst32++ = *src32++;
					*dst32++ = *src32++;
					*dst32++ = *src32++;
					size -= 16;
				}
				if (size==0) return (dst);		// fast out
				src8 = (u8*)src32;
				dst8 = (u8*)dst32;
				break;
			default:
				{
					register u32 a, b, c, d;
					while (size>=4)
					{
						a = *src8++;
						b = *src8++;
						c = *src8++;
						d = *src8++;
						*dst32++ = (d << 24) | (c << 16) | (b << 8) | a;
						size -= 4;
					}
					if (size==0) return (dst);		// fast out
					dst8 = (u8*)dst32;
				}
				break;
		}
		goto bytecopy;
	}

	// Align dst on 16 bytes to gain from vfpu aligned stores
	while ((((u32)dst8) & 0xF)!=0 && size) {
		*dst8++ = *src8++;
		size--;
	}

	// We use uncached dst to use VFPU writeback and free cpu cache for src only
	u8* udst8 = (u8*)((u32)dst8 | 0x40000000);
	// We need the 64 byte aligned address to make sure the dcache is invalidated correctly
	u8* dst64a = (u8*)((u32)dst8&~0x3F);
	// Invalidate the first line that matches up to the dst start
	if (size>=64)
	asm(".set	push\n"					// save assembler option
		".set	noreorder\n"			// suppress reordering
		"cache 0x1B, 0(%0)\n"
		"addiu	%0, %0, 64\n"
		"sync\n"
		".set	pop\n"
		:"+r"(dst64a));
	switch(((u32)src8&0xF))
	{
		// src aligned on 16 bytes too? nice!
		case 0:
			while (size>=64)
			{
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"cache	0x1B,  0(%2)\n"			// Dcache writeback invalidate
					"lv.q	c000,  0(%1)\n"
					"lv.q	c010, 16(%1)\n"
					"lv.q	c020, 32(%1)\n"
					"lv.q	c030, 48(%1)\n"
					"sync\n"						// Wait for allegrex writeback
					"sv.q	c000,  0(%0), wb\n"
					"sv.q	c010, 16(%0), wb\n"
					"sv.q	c020, 32(%0), wb\n"
					"sv.q	c030, 48(%0), wb\n"
					// Lots of variable updates... but get hidden in sv.q latency anyway
					"addiu  %3, %3, -64\n"
					"addiu	%2, %2, 64\n"
					"addiu	%1, %1, 64\n"
					"addiu	%0, %0, 64\n"
					".set	pop\n"					// restore assembler option
					:"+r"(udst8),"+r"(src8),"+r"(dst64a),"+r"(size)
					:
					:"memory"
					);
			}
			if (size>16)
			{
				// Invalidate the last cache line where the max remaining 63 bytes are
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"cache	0x1B, 0(%0)\n"
					"sync\n"
					".set	pop\n"					// restore assembler option
					::"r"(dst64a));
				while (size>=16)
				{
					asm(".set	push\n"					// save assembler option
						".set	noreorder\n"			// suppress reordering
						"lv.q	c000, 0(%1)\n"
						"sv.q	c000, 0(%0), wb\n"
						// Lots of variable updates... but get hidden in sv.q latency anyway
						"addiu	%2, %2, -16\n"
						"addiu	%1, %1, 16\n"
						"addiu	%0, %0, 16\n"
						".set	pop\n"					// restore assembler option
						:"+r"(udst8),"+r"(src8),"+r"(size)
						:
						:"memory"
						);
				}
			}
			asm(".set	push\n"					// save assembler option
				".set	noreorder\n"			// suppress reordering
				"vflush\n"						// Flush VFPU writeback cache
				".set	pop\n"					// restore assembler option
				);
			dst8 = (u8*)((u32)udst8 & ~0x40000000);
			break;
		// src is only qword unaligned but word aligned? We can at least use ulv.q
		case 4:
		case 8:
		case 12:
			while (size>=64)
			{
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"cache	0x1B,  0(%2)\n"			// Dcache writeback invalidate
					"ulv.q	c000,  0(%1)\n"
					"ulv.q	c010, 16(%1)\n"
					"ulv.q	c020, 32(%1)\n"
					"ulv.q	c030, 48(%1)\n"
					"sync\n"						// Wait for allegrex writeback
					"sv.q	c000,  0(%0), wb\n"
					"sv.q	c010, 16(%0), wb\n"
					"sv.q	c020, 32(%0), wb\n"
					"sv.q	c030, 48(%0), wb\n"
					// Lots of variable updates... but get hidden in sv.q latency anyway
					"addiu  %3, %3, -64\n"
					"addiu	%2, %2, 64\n"
					"addiu	%1, %1, 64\n"
					"addiu	%0, %0, 64\n"
					".set	pop\n"					// restore assembler option
					:"+r"(udst8),"+r"(src8),"+r"(dst64a),"+r"(size)
					:
					: "memory",
					"f0", "f1", "f2", "f3" // clobber fpu registers due to ulv corruption bug, we use 000, 010, 020, 030, if we used 100, it would be f4, 110 -> f5, etc
					);
			}
			if (size>16)
			// Invalidate the last cache line where the max remaining 63 bytes are
			asm(".set	push\n"					// save assembler option
				".set	noreorder\n"			// suppress reordering
				"cache	0x1B, 0(%0)\n"
				"sync\n"
				".set	pop\n"					// restore assembler option
				::"r"(dst64a));
			while (size>=16)
			{
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"ulv.q	c000, 0(%1)\n"
					"sv.q	c000, 0(%0), wb\n"
					// Lots of variable updates... but get hidden in sv.q latency anyway
					"addiu	%2, %2, -16\n"
					"addiu	%1, %1, 16\n"
					"addiu	%0, %0, 16\n"
					".set	pop\n"					// restore assembler option
					:"+r"(udst8),"+r"(src8),"+r"(size)
					:
					:"memory",
					"f0" // clobber fpu register due to ulv corruption bug
					);
			}
			asm(".set	push\n"					// save assembler option
				".set	noreorder\n"			// suppress reordering
				"vflush\n"						// Flush VFPU writeback cache
				".set	pop\n"					// restore assembler option
				);
			dst8 = (u8*)((u32)udst8 & ~0x40000000);
			break;
		// src not aligned? too bad... have to use unaligned reads
		default:
			while (size>=64)
			{
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"cache 0x1B,  0(%2)\n"

					"lwr	 $8,  0(%1)\n"			//
					"lwl	 $8,  3(%1)\n"			// $8  = *(s + 0)
					"lwr	 $9,  4(%1)\n"			//
					"lwl	 $9,  7(%1)\n"			// $9  = *(s + 4)
					"lwr	$10,  8(%1)\n"			//
					"lwl	$10, 11(%1)\n"			// $10 = *(s + 8)
					"lwr	$11, 12(%1)\n"			//
					"lwl	$11, 15(%1)\n"			// $11 = *(s + 12)
					"mtv	 $8, s000\n"
					"mtv	 $9, s001\n"
					"mtv	$10, s002\n"
					"mtv	$11, s003\n"

					"lwr	 $8, 16(%1)\n"
					"lwl	 $8, 19(%1)\n"
					"lwr	 $9, 20(%1)\n"
					"lwl	 $9, 23(%1)\n"
					"lwr	$10, 24(%1)\n"
					"lwl	$10, 27(%1)\n"
					"lwr	$11, 28(%1)\n"
					"lwl	$11, 31(%1)\n"
					"mtv	 $8, s010\n"
					"mtv	 $9, s011\n"
					"mtv	$10, s012\n"
					"mtv	$11, s013\n"

					"lwr	 $8, 32(%1)\n"
					"lwl	 $8, 35(%1)\n"
					"lwr	 $9, 36(%1)\n"
					"lwl	 $9, 39(%1)\n"
					"lwr	$10, 40(%1)\n"
					"lwl	$10, 43(%1)\n"
					"lwr	$11, 44(%1)\n"
					"lwl	$11, 47(%1)\n"
					"mtv	 $8, s020\n"
					"mtv	 $9, s021\n"
					"mtv	$10, s022\n"
					"mtv	$11, s023\n"

					"lwr	 $8, 48(%1)\n"
					"lwl	 $8, 51(%1)\n"
					"lwr	 $9, 52(%1)\n"
					"lwl	 $9, 55(%1)\n"
					"lwr	$10, 56(%1)\n"
					"lwl	$10, 59(%1)\n"
					"lwr	$11, 60(%1)\n"
					"lwl	$11, 63(%1)\n"
					"mtv	 $8, s030\n"
					"mtv	 $9, s031\n"
					"mtv	$10, s032\n"
					"mtv	$11, s033\n"

					"sync\n"
					"sv.q 	c000,  0(%0), wb\n"
					"sv.q 	c010, 16(%0), wb\n"
					"sv.q 	c020, 32(%0), wb\n"
					"sv.q 	c030, 48(%0), wb\n"
					// Lots of variable updates... but get hidden in sv.q latency anyway
					"addiu	%3, %3, -64\n"
					"addiu	%2, %2, 64\n"
					"addiu	%1, %1, 64\n"
					"addiu	%0, %0, 64\n"
					".set	pop\n"					// restore assembler option
					:"+r"(udst8),"+r"(src8),"+r"(dst64a),"+r"(size)
					:
					:"$8","$9","$10","$11","memory"
					);
			}
			if (size>16)
			// Invalidate the last cache line where the max remaining 63 bytes are
			asm(".set	push\n"					// save assembler option
				".set	noreorder\n"			// suppress reordering
				"cache	0x1B, 0(%0)\n"
				"sync\n"
				".set	pop\n"					// restore assembler option
				::"r"(dst64a));
			while (size>=16)
			{
				asm(".set	push\n"					// save assembler option
					".set	noreorder\n"			// suppress reordering
					"lwr	 $8,  0(%1)\n"			//
					"lwl	 $8,  3(%1)\n"			// $8  = *(s + 0)
					"lwr	 $9,  4(%1)\n"			//
					"lwl	 $9,  7(%1)\n"			// $9  = *(s + 4)
					"lwr	$10,  8(%1)\n"			//
					"lwl	$10, 11(%1)\n"			// $10 = *(s + 8)
					"lwr	$11, 12(%1)\n"			//
					"lwl	$11, 15(%1)\n"			// $11 = *(s + 12)
					"mtv	 $8, s000\n"
					"mtv	 $9, s001\n"
					"mtv	$10, s002\n"
					"mtv	$11, s003\n"

					"sv.q	c000, 0(%0), wb\n"
					// Lots of variable updates... but get hidden in sv.q latency anyway
					"addiu	%2, %2, -16\n"
					"addiu	%1, %1, 16\n"
					"addiu	%0, %0, 16\n"
					".set	pop\n"					// restore assembler option
					:"+r"(udst8),"+r"(src8),"+r"(size)
					:
					:"$8","$9","$10","$11","memory"
					);
			}
			asm(".set	push\n"					// save assembler option
				".set	noreorder\n"			// suppress reordering
				"vflush\n"						// Flush VFPU writeback cache
				".set	pop\n"					// restore assembler option
				);
			dst8 = (u8*)((u32)udst8 & ~0x40000000);
			break;
	}

bytecopy:
	// Copy the remains byte per byte...
	while (size--)
	{
		*dst8++ = *src8++;
	}

	return (dst);
}
#endif //PSP_VFPU

/*
===================
Q_malloc
Use it instead of malloc so that if memory allocation fails,
the program exits with a message saying there's not enough memory
instead of crashing after trying to use a NULL pointer
===================
*/
void *Q_malloc (size_t size)
{
	void	*p;

	if (!(p = malloc(size)))
		Sys_Error ("Not enough memory free; check disk space");

	return p;
}

/*
===================
Q_calloc
===================
*/
void *Q_calloc (size_t n, size_t size)
{
	void	*p;

	if (!(p = calloc(n, size)))
		Sys_Error ("Not enough memory free; check disk space");

	return p;
}

/*
===================
Q_realloc
===================
*/
void *Q_realloc (void *ptr, size_t size)
{
	void	*p;

	if (!(p = realloc(ptr, size)))
		Sys_Error ("Not enough memory free; check disk space");

	return p;
}

/*
===================
Q_strdup
===================
*/
void *Q_strdup (const char *str)
{
	char	*p;

	if (!(p = strdup(str)))
		Sys_Error ("Not enough memory free; check disk space");

	return p;
}


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

static memzone_t	*mainzone;


/*
========================
Z_Free
========================
*/
void Z_Free (void *ptr)
{
	memblock_t	*block, *other;

	if (!ptr)
		Sys_Error ("NULL pointer");

	block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		Sys_Error ("freed a pointer without ZONEID");
	if (block->tag == 0)
		Sys_Error ("freed a freed pointer");

	block->tag = 0;		// mark as free

	other = block->prev;
	if (!other->tag)
	{	// merge with previous free block
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;
		if (block == mainzone->rover)
			mainzone->rover = other;
		block = other;
	}

	other = block->next;
	if (!other->tag)
	{	// merge the next free block onto the end
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;
		if (other == mainzone->rover)
			mainzone->rover = block;
	}
}


void *Z_TagMalloc (int size, int tag)
{
	int		extra;
	memblock_t	*start, *rover, *newblock, *base;

	if (!tag)
		Sys_Error ("tried to use a 0 tag");

//
// scan through the block list looking for the first free block
// of sufficient size
//
	size += sizeof(memblock_t);	// account for size of block header
	size += 4;					// space for memory trash tester
	size = (size + 7) & ~7;		// align to 8-byte boundary

	base = rover = mainzone->rover;
	start = base->prev;

	do
	{
		if (rover == start)	// scaned all the way around the list
			return NULL;
		if (rover->tag)
			base = rover = rover->next;
		else
			rover = rover->next;
	} while (base->tag || base->size < size);

//
// found a block big enough
//
	extra = base->size - size;
	if (extra >  MINFRAGMENT)
	{	// there will be a free fragment after the allocated block
		newblock = (memblock_t *) ((byte *)base + size );
		newblock->size = extra;
		newblock->tag = 0;			// free block
		newblock->prev = base;
		newblock->id = ZONEID;
		newblock->next = base->next;
		newblock->next->prev = newblock;
		base->next = newblock;
		base->size = size;
	}

	base->tag = tag;				// no longer a free block

	mainzone->rover = base->next;	// next allocation will start looking here

	base->id = ZONEID;

// marker for memory trash testing
	*(int *)((byte *)base + base->size - 4) = ZONEID;

	return (void *) ((byte *)base + sizeof(memblock_t));
}

/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap (void)
{
	memblock_t	*block;

	for (block = mainzone->blocklist.next ; ; block = block->next)
	{
		if (block->next == &mainzone->blocklist)
			break;			// all blocks have been hit
		if ( (byte *)block + block->size != (byte *)block->next)
			Sys_Error ("block size does not touch the next block");
		if ( block->next->prev != block)
			Sys_Error ("next block doesn't have proper back link");
		if (!block->tag && !block->next->tag)
			Sys_Error ("two consecutive free blocks");
	}
}


/*
========================
Z_Malloc
========================
*/
void *Z_Malloc (int size)
{
	void	*buf;

	Z_CheckHeap ();	// DEBUG
	buf = Z_TagMalloc (size, 1);
	if (!buf)
		Sys_Error ("failed on allocation of %i bytes",size);
	Q_memset (buf, 0, size);

	return buf;
}

/*
========================
Z_Realloc
========================
*/
void *Z_Realloc(void *ptr, int size)
{
	int old_size;
	void *old_ptr;
	memblock_t *block;

	if (!ptr)
		return Z_Malloc (size);

	block = (memblock_t *) ((byte *) ptr - sizeof (memblock_t));
	if (block->id != ZONEID)
		Sys_Error ("realloced a pointer without ZONEID");
	if (block->tag == 0)
		Sys_Error ("realloced a freed pointer");

	old_size = block->size;
	old_size -= (4 + (int)sizeof(memblock_t));	/* see Z_TagMalloc() */
	old_ptr = ptr;

	Z_Free (ptr);
	ptr = Z_TagMalloc (size, 1);
	if (!ptr)
		Sys_Error ("failed on allocation of %i bytes", size);

	if (ptr != old_ptr)
		memmove (ptr, old_ptr, MIN(old_size, size));
	if (old_size < size)
		memset ((byte *)ptr + old_size, 0, size - old_size);

	return ptr;
}

char *Z_Strdup (char *s)
{
	size_t sz = strlen(s) + 1;
	char *ptr = (char *) Z_Malloc (sz);
	memcpy (ptr, s, sz);
	return ptr;
}


/*
========================
Z_Print
========================
*/
void Z_Print (memzone_t *zone)
{
	memblock_t	*block;

	Con_Printf ("zone size: %i  location: %p\n",mainzone->size,mainzone);

	for (block = zone->blocklist.next ; ; block = block->next)
	{
		Con_Printf ("block:%p    size:%7i    tag:%3i\n",
			block, block->size, block->tag);

		if (block->next == &zone->blocklist)
			break;			// all blocks have been hit
		if ( (byte *)block + block->size != (byte *)block->next)
			Con_Printf ("ERROR: block size does not touch the next block\n");
		if ( block->next->prev != block)
			Con_Printf ("ERROR: next block doesn't have proper back link\n");
		if (!block->tag && !block->next->tag)
			Con_Printf ("ERROR: two consecutive free blocks\n");
	}
}


//============================================================================

#define	HUNK_SENTINEL	0x1df001ed

#define HUNKNAME_LEN	24
typedef struct
{
	int		sentinel;
	int		size;		// including sizeof(hunk_t), -1 = not allocated
	char	name[HUNKNAME_LEN];
} hunk_t;

byte	*hunk_base;
int		hunk_size;

int		hunk_low_used;
int		hunk_high_used;

qboolean	hunk_tempactive;
int		hunk_tempmark;

/*
==============
Hunk_Check

Run consistancy and sentinel trahing checks
==============
*/
void Hunk_Check (void)
{
	hunk_t	*h;

	for (h = (hunk_t *)hunk_base ; (byte *)h != hunk_base + hunk_low_used ; )
	{
		if (h->sentinel != HUNK_SENTINEL)
			Sys_Error ("trashed sentinel");
		if (h->size < (int) sizeof(hunk_t) || h->size + (byte *)h - hunk_base > hunk_size)
			Sys_Error ("bad size");
		h = (hunk_t *)((byte *)h+h->size);
	}
}

/*
==============
Hunk_Print

If "all" is specified, every single allocation is printed.
Otherwise, allocations with the same name will be totaled up before printing.
==============
*/
void Hunk_Print (qboolean all)
{
	hunk_t	*h, *next, *endlow, *starthigh, *endhigh;
	int		sum;
	int		totalblocks;
	char	name[HUNKNAME_LEN];

	sum = 0;
	totalblocks = 0;

	h = (hunk_t *)hunk_base;
	endlow = (hunk_t *)(hunk_base + hunk_low_used);
	starthigh = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);
	endhigh = (hunk_t *)(hunk_base + hunk_size);

	Con_Printf ("          :%8i total hunk size\n", hunk_size);
	Con_Printf ("-------------------------\n");

	while (1)
	{
	//
	// skip to the high hunk if done with low hunk
	//
		if ( h == endlow )
		{
			Con_Printf ("-------------------------\n");
			Con_Printf ("          :%8i REMAINING\n", hunk_size - hunk_low_used - hunk_high_used);
			Con_Printf ("-------------------------\n");
			h = starthigh;
		}

	//
	// if totally done, break
	//
		if ( h == endhigh )
			break;

	//
	// run consistancy checks
	//
		if (h->sentinel != HUNK_SENTINEL)
			Sys_Error ("trashed sentinel");
		if (h->size < (int) sizeof(hunk_t) || h->size + (byte *)h - hunk_base > hunk_size)
			Sys_Error ("bad size");

		next = (hunk_t *)((byte *)h+h->size);
		totalblocks++;
		sum += h->size;

	//
	// print the single block
	//
		memcpy (name, h->name, HUNKNAME_LEN);
		if (all)
			Con_Printf ("%8p :%8i %8s\n",h, h->size, name);

	//
	// print the total
	//
		if (next == endlow || next == endhigh ||
		    strncmp (h->name, next->name, HUNKNAME_LEN - 1))
		{
			if (!all)
				Con_Printf ("          :%8i %8s (TOTAL)\n",sum, name);
			sum = 0;
		}

		h = next;
	}

	Con_Printf ("-------------------------\n");
	Con_Printf ("%8i total blocks\n", totalblocks);

}

/*
===================
Hunk_Print_f -- johnfitz -- console command to call hunk_print
===================
*/
void Hunk_Print_f (void)
{
	Hunk_Print (false);
}

/*
===================
Hunk_AllocName
===================
*/
void *Hunk_AllocName (int size, char *name)
{
	hunk_t	*h;

#ifdef PARANOID
	Hunk_Check ();
#endif

	if (size < 0)
		Sys_Error ("bad size: %i", size);

	size = sizeof(hunk_t) + ((size+15)&~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
		Sys_Error ("failed on %i bytes",size);

	h = (hunk_t *)(hunk_base + hunk_low_used);
	hunk_low_used += size;

	Cache_FreeLow (hunk_low_used);

	memset (h, 0, size);

	h->size = size;
	h->sentinel = HUNK_SENTINEL;
	strlcpy(h->name, name, HUNKNAME_LEN);

	return (void *)(h+1);
}

/*
===================
Hunk_Alloc
===================
*/
void *Hunk_Alloc (int size)
{
	return Hunk_AllocName (size, "unknown");
}

int	Hunk_LowMark (void)
{
	return hunk_low_used;
}

void Hunk_FreeToLowMark (int mark)
{
	if (mark < 0 || mark > hunk_low_used)
		Sys_Error ("bad mark %i", mark);
	memset (hunk_base + mark, 0, hunk_low_used - mark);
	hunk_low_used = mark;
}

int	Hunk_HighMark (void)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark (hunk_tempmark);
	}

	return hunk_high_used;
}

void Hunk_FreeToHighMark (int mark)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark (hunk_tempmark);
	}
	if (mark < 0 || mark > hunk_high_used)
		Sys_Error ("bad mark %i", mark);
	memset (hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
	hunk_high_used = mark;
}


/*
===================
Hunk_HighAllocName
===================
*/
void *Hunk_HighAllocName (int size, char *name)
{
	hunk_t	*h;

	if (size < 0)
		Sys_Error ("bad size: %i", size);

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark (hunk_tempmark);
		hunk_tempactive = false;
	}

#ifdef PARANOID
	Hunk_Check ();
#endif

	size = sizeof(hunk_t) + ((size+15)&~15);

	if (hunk_size - hunk_low_used - hunk_high_used < size)
	{
		Con_Printf ("Hunk_HighAlloc: failed on %i bytes\n",size);
		return NULL;
	}

	hunk_high_used += size;
	Cache_FreeHigh (hunk_high_used);

	h = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);

	memset (h, 0, size);
	h->size = size;
	h->sentinel = HUNK_SENTINEL;
	strlcpy (h->name, name, HUNKNAME_LEN);

	return (void *)(h+1);
}


/*
=================
Hunk_TempAlloc

Return space from the top of the hunk
=================
*/
void *Hunk_TempAlloc (int size)
{
	void	*buf;

	size = (size+15)&~15;

	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark (hunk_tempmark);
		hunk_tempactive = false;
	}

	hunk_tempmark = Hunk_HighMark ();

	buf = Hunk_HighAllocName (size, "temp");

	hunk_tempactive = true;

	return buf;
}

char *Hunk_Strdup (char *s, char *name)
{
	size_t sz = strlen(s) + 1;
	char *ptr = (char *) Hunk_AllocName (sz, name);
	memcpy (ptr, s, sz);
	return ptr;
}

/*
===============================================================================

CACHE MEMORY

===============================================================================
*/

#define CACHENAME_LEN	32
typedef struct cache_system_s
{
	int			size;		// including this header
	cache_user_t		*user;
	char			name[CACHENAME_LEN];
	struct cache_system_s	*prev, *next;
	struct cache_system_s	*lru_prev, *lru_next;	// for LRU flushing
} cache_system_t;

cache_system_t *Cache_TryAlloc (int size, qboolean nobottom);

cache_system_t	cache_head;

/*
===========
Cache_Move
===========
*/
void Cache_Move ( cache_system_t *c)
{
	cache_system_t		*new_cs;

// we are clearing up space at the bottom, so only allocate it late
	new_cs = Cache_TryAlloc (c->size, true);
	if (new_cs)
	{
//		Con_Printf ("cache_move ok\n");

		Q_memcpy ( new_cs+1, c+1, c->size - sizeof(cache_system_t) );
		new_cs->user = c->user;
		Q_memcpy (new_cs->name, c->name, sizeof(new_cs->name));
		Cache_Free (c->user);
		new_cs->user->data = (void *)(new_cs+1);
	}
	else
	{
//		Con_Printf ("cache_move failed\n");

		Cache_Free (c->user); // tough luck...
	}
}

/*
============
Cache_FreeLow

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeLow (int new_low_hunk)
{
	cache_system_t	*c;

	while (1)
	{
		c = cache_head.next;
		if (c == &cache_head)
			return;		// nothing in cache at all
		if ((byte *)c >= hunk_base + new_low_hunk)
			return;		// there is space to grow the hunk
		Cache_Move ( c );	// reclaim the space
	}
}

/*
============
Cache_FreeHigh

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeHigh (int new_high_hunk)
{
	cache_system_t	*c, *prev;

	prev = NULL;
	while (1)
	{
		c = cache_head.prev;
		if (c == &cache_head)
			return;		// nothing in cache at all
		if ( (byte *)c + c->size <= hunk_base + hunk_size - new_high_hunk)
			return;		// there is space to grow the hunk
		if (c == prev)
			Cache_Free (c->user);	// didn't move out of the way
		else
		{
			Cache_Move (c);	// try to move it
			prev = c;
		}
	}
}

void Cache_UnlinkLRU (cache_system_t *cs)
{
	if (!cs->lru_next || !cs->lru_prev)
		Sys_Error ("NULL link");

	cs->lru_next->lru_prev = cs->lru_prev;
	cs->lru_prev->lru_next = cs->lru_next;

	cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU (cache_system_t *cs)
{
	if (cs->lru_next || cs->lru_prev)
		Sys_Error ("active link");

	cache_head.lru_next->lru_prev = cs;
	cs->lru_next = cache_head.lru_next;
	cs->lru_prev = &cache_head;
	cache_head.lru_next = cs;
}

/*
============
Cache_TryAlloc

Looks for a free block of memory between the high and low hunk marks
Size should already include the header and padding
============
*/
cache_system_t *Cache_TryAlloc (int size, qboolean nobottom)
{
	cache_system_t	*cs, *new_cs;

// is the cache completely empty?

	if (!nobottom && cache_head.prev == &cache_head)
	{
		if (hunk_size - hunk_high_used - hunk_low_used < size)
			Sys_Error ("%i is greater then free hunk", size);

		new_cs = (cache_system_t *) (hunk_base + hunk_low_used);
		memset (new_cs, 0, sizeof(*new_cs));
		new_cs->size = size;

		cache_head.prev = cache_head.next = new_cs;
		new_cs->prev = new_cs->next = &cache_head;

		Cache_MakeLRU (new_cs);
		return new_cs;
	}

// search from the bottom up for space

	new_cs = (cache_system_t *) (hunk_base + hunk_low_used);
	cs = cache_head.next;

	do
	{
		if (!nobottom || cs != cache_head.next)
		{
			if ( (byte *)cs - (byte *)new_cs >= size)
			{	// found space
				memset (new_cs, 0, sizeof(*new_cs));
				new_cs->size = size;

				new_cs->next = cs;
				new_cs->prev = cs->prev;
				cs->prev->next = new_cs;
				cs->prev = new_cs;

				Cache_MakeLRU (new_cs);

				return new_cs;
			}
		}

	// continue looking
		new_cs = (cache_system_t *)((byte *)cs + cs->size);
		cs = cs->next;

	} while (cs != &cache_head);

// try to allocate one at the very end
	if ( hunk_base + hunk_size - hunk_high_used - (byte *)new_cs >= size)
	{
		memset (new_cs, 0, sizeof(*new_cs));
		new_cs->size = size;

		new_cs->next = &cache_head;
		new_cs->prev = cache_head.prev;
		cache_head.prev->next = new_cs;
		cache_head.prev = new_cs;

		Cache_MakeLRU (new_cs);

		return new_cs;
	}

	return NULL;		// couldn't allocate
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush (void)
{
	while (cache_head.next != &cache_head)
		Cache_Free ( cache_head.next->user); // reclaim the space
}

/*
============
Cache_Print

============
*/
void Cache_Print (void)
{
	cache_system_t	*cd;

	for (cd = cache_head.next ; cd != &cache_head ; cd = cd->next)
	{
		Con_Printf ("%8i : %s\n", cd->size, cd->name);
	}
}

/*
============
Cache_Report

============
*/
void Cache_Report (void)
{
	Con_DPrintf ("%4.1f megabyte data cache\n", (double)(hunk_size - hunk_high_used - hunk_low_used) / (double)(1024*1024) );
}

/*
============
Cache_Init

============
*/
void Cache_Init (void)
{
	cache_head.next = cache_head.prev = &cache_head;
	cache_head.lru_next = cache_head.lru_prev = &cache_head;

	Cmd_AddCommand ("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		Sys_Error ("not allocated");

	cs = ((cache_system_t *)c->data) - 1;

	cs->prev->next = cs->next;
	cs->next->prev = cs->prev;
	cs->next = cs->prev = NULL;

	c->data = NULL;

	Cache_UnlinkLRU (cs);
}



/*
==============
Cache_Check
==============
*/
void *Cache_Check (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		return NULL;

	cs = ((cache_system_t *)c->data) - 1;

// move to head of LRU
	Cache_UnlinkLRU (cs);
	Cache_MakeLRU (cs);

	return c->data;
}


/*
==============
Cache_Alloc
==============
*/
void *Cache_Alloc (cache_user_t *c, int size, char *name)
{
	cache_system_t	*cs;

	if (c->data)
		Sys_Error ("already allocated");

	if (size <= 0)
		Sys_Error ("size %i", size);

	size = (size + sizeof(cache_system_t) + 15) & ~15;

// find memory for it
	while (1)
	{
		cs = Cache_TryAlloc (size, false);
		if (cs)
		{
			strlcpy (cs->name, name, CACHENAME_LEN);
			c->data = (void *)(cs+1);
			cs->user = c;
			break;
		}

	// free the least recently used cahedat
		if (cache_head.lru_prev == &cache_head)
			Sys_Error ("out of memory"); // not enough memory at all

		Cache_Free (cache_head.lru_prev->user);
	}

	return Cache_Check (c);
}

//============================================================================


void Memory_InitZone (memzone_t *zone, int size)
{
	memblock_t	*block;

// set the entire zone to one free block

	zone->blocklist.next = zone->blocklist.prev = block =
		(memblock_t *)( (byte *)zone + sizeof(memzone_t) );
	zone->blocklist.tag = 1;	// in use block
	zone->blocklist.id = 0;
	zone->blocklist.size = 0;
	zone->rover = block;

	block->prev = block->next = &zone->blocklist;
	block->tag = 0;			// free block
	block->id = ZONEID;
	block->size = size - sizeof(memzone_t);
}

/*
========================
Memory_Init
========================
*/
void Memory_Init (void *buf, int size)
{
	int p;
	int zonesize = DYNAMIC_SIZE;

	hunk_base = (byte *) buf;
	hunk_size = size;
	hunk_low_used = 0;
	hunk_high_used = 0;

	Cache_Init ();
	p = COM_CheckParm ("-zone");
	if (p)
	{
		if (p < com_argc-1)
			zonesize = Q_atoi (com_argv[p+1]) * 1024;
		else
			Sys_Error ("you must specify a size in KB after -zone");
	}
	mainzone = (memzone_t *) Hunk_AllocName (zonesize, "zone" );
	Memory_InitZone (mainzone, zonesize);

	Cmd_AddCommand ("hunk_print", Hunk_Print_f); //johnfitz
}

