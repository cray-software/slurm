/****************************************************************************\
 *  pack.c - lowest level un/pack functions
 *  NOTE: The memory buffer will expand as needed using xrealloc()
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>, 
 *             Moe Jette <jette1@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "src/common/pack.h"
#include "src/common/slurm_errno.h"
#include "src/common/xmalloc.h"
#include "src/common/macros.h"

#define BUF_SIZE 4096


/* Basic buffer management routines */
/* create_buf - create a buffer with the supplied contents, contents must
 * be xalloc'ed */
Buf create_buf(char *data, int size)
{
	Buf my_buf;

	my_buf = xmalloc(sizeof(struct slurm_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = data;

	return my_buf;
}

/* free_buf - release memory associated with a given buffer */
void free_buf(Buf my_buf)
{
	assert(my_buf->magic == BUF_MAGIC);
	if (my_buf->head)
		xfree(my_buf->head);
	xfree(my_buf);
}

/* init_buf - create an empty buffer of the given size */
Buf init_buf(int size)
{
	Buf my_buf;

	my_buf = xmalloc(sizeof(struct slurm_buf));
	my_buf->magic = BUF_MAGIC;
	my_buf->size = size;
	my_buf->processed = 0;
	my_buf->head = xmalloc(size);

	return my_buf;
}

/* xfer_buf_data - return a pointer to the buffer's data and release the
 * buffer's structure */
void *xfer_buf_data(Buf my_buf)
{
	void *data_ptr;

	assert(my_buf->magic == BUF_MAGIC);
	data_ptr = (void *) my_buf->head;
	xfree(my_buf);
	return data_ptr;
}

/*
 * Given a time_t in host byte order, promote it to int64_t, convert to
 * network byte order, store in buffer and adjust buffer acc'd'ngly
 */
void pack_time(time_t val, Buf buffer)
{
	int64_t n64 = HTON_int64((int64_t) val);

	if (remaining_buf(buffer) < sizeof(n64)) {
		buffer->size += BUF_SIZE;
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &n64, sizeof(n64));
	buffer->processed += sizeof(n64);
}

int unpack_time(time_t * valp, Buf buffer)
{
	int64_t n64;

	if (remaining_buf(buffer) < sizeof(n64))
		return SLURM_ERROR;

	memcpy(&n64, &buffer->head[buffer->processed], sizeof(n64));
	buffer->processed += sizeof(n64);
	*valp = (time_t) NTOH_int64(n64);
	return SLURM_SUCCESS;
}


/*
 * Given a 32-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack32(uint32_t val, Buf buffer)
{
	uint32_t nl = htonl(val);

	if (remaining_buf(buffer) < sizeof(nl)) {
		buffer->size += BUF_SIZE;
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &nl, sizeof(nl));
	buffer->processed += sizeof(nl);
}

/*
 * Given a buffer containing a network byte order 32-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack32(uint32_t * valp, Buf buffer)
{
	uint32_t nl;

	if (remaining_buf(buffer) < sizeof(nl))
		return SLURM_ERROR;

	memcpy(&nl, &buffer->head[buffer->processed], sizeof(nl));
	*valp = ntohl(nl);
	buffer->processed += sizeof(nl);
	return SLURM_SUCCESS;
}

/* Given a *uint32_t, it will pack an array of size_val */
void pack32_array(uint32_t * valp, uint16_t size_val, Buf buffer)
{
	int i = 0;

	pack16(size_val, buffer);

	for (i = 0; i < size_val; i++) {
		pack32(*(valp + i), buffer);
	}
}

/* Given a int ptr, it will unpack an array of size_val
 */
int unpack32_array(uint32_t ** valp, uint16_t * size_val, Buf buffer)
{
	int i = 0;

	if (unpack16(size_val, buffer))
		return SLURM_ERROR;

	*valp = xmalloc((*size_val) * sizeof(uint32_t));
	for (i = 0; i < *size_val; i++) {
		if (unpack32((*valp) + i, buffer))
			return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * Given a 16-bit integer in host byte order, convert to network byte order,
 * store in buffer and adjust buffer counters.
 */
void pack16(uint16_t val, Buf buffer)
{
	uint16_t ns = htons(val);

	if (remaining_buf(buffer) < sizeof(ns)) {
		buffer->size += BUF_SIZE;
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack16(uint16_t * valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*valp = ntohs(ns);
	buffer->processed += sizeof(ns);
	return SLURM_SUCCESS;
}

/*
 * Given a 8-bit integer in host byte order, convert to network byte order
 * store in buffer, and adjust buffer counters.
 */
void pack8(uint8_t val, Buf buffer)
{
	if (remaining_buf(buffer) < sizeof(uint8_t)) {
		buffer->size += BUF_SIZE;
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &val, sizeof(uint8_t));
	buffer->processed += sizeof(uint8_t);
}

/*
 * Given a buffer containing a network byte order 8-bit integer,
 * store a host integer at 'valp', and adjust buffer counters.
 */
int unpack8(uint8_t * valp, Buf buffer)
{
	if (remaining_buf(buffer) < sizeof(uint8_t))
		return SLURM_ERROR;

	memcpy(valp, &buffer->head[buffer->processed], sizeof(uint8_t));
	buffer->processed += sizeof(uint8_t);
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to memory (valp) and a size (size_val), convert 
 * size_val to network byte order and store at buffer followed by 
 * the data at valp. Adjust buffer counters.
 */
void packmem(char *valp, uint16_t size_val, Buf buffer)
{
	uint16_t ns = htons(size_val);

	if (remaining_buf(buffer) < (sizeof(ns) + size_val)) {
		buffer->size += (size_val + BUF_SIZE);
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);

	if (size_val) {
		memcpy(&buffer->head[buffer->processed], valp, size_val);
		buffer->processed += size_val;
	}
}


/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, return a pointer to the 
 * data string in 'valp'.  Also return the sizes of 'valp' in bytes. 
 * Adjust buffer counters.
 * NOTE: valp is set to point into the buffer bufp, a copy of 
 *	the data is not made
 */
int unpackmem_ptr(char **valp, uint16_t * size_valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohs(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = &buffer->head[buffer->processed];
		buffer->processed += *size_valp;
	} else 
		*valp = NULL;
	return SLURM_SUCCESS;
}


/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location 
 * specified by valp.  Also return the sizes of 'valp' in bytes. 
 * Adjust buffer counters.
 * NOTE: The caller is responsible for the management of valp and 
 * insuring it has sufficient size
 */
int unpackmem(char *valp, uint16_t * size_valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohs(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		memcpy(valp, &buffer->head[buffer->processed], *size_valp);
		buffer->processed += *size_valp;
	} else 
		*valp = 0;
	return SLURM_SUCCESS;
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location 
 * specified by valp.  Also return the sizes of 'valp' in bytes. 
 * Adjust buffer counters.
 * NOTE: valp is set to point into a newly created buffer, 
 *	the caller is responsible for calling xfree() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackmem_xmalloc(char **valp, uint16_t * size_valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohs(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = xmalloc(*size_valp);
		memcpy(*valp, &buffer->head[buffer->processed],
		       *size_valp);
		buffer->processed += *size_valp;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a buffer containing a network byte order 16-bit integer,
 * and an arbitrary data string, copy the data string into the location 
 * specified by valp.  Also return the sizes of 'valp' in bytes. 
 * Adjust buffer counters.
 * NOTE: valp is set to point into a newly created buffer, 
 *	the caller is responsible for calling free() on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackmem_malloc(char **valp, uint16_t * size_valp, Buf buffer)
{
	uint16_t ns;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohs(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		if (remaining_buf(buffer) < *size_valp)
			return SLURM_ERROR;
		*valp = malloc(*size_valp);
		memcpy(*valp, &buffer->head[buffer->processed],
		       *size_valp);
		buffer->processed += *size_valp;
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to array of char * (char ** or char *[] ) and a size  
 * (size_val), convert size_val to network byte order and store in the  
 * buffer followed by the data at valp. Adjust buffer counters. 
 */
void packstr_array(char **valp, uint16_t size_val, Buf buffer)
{
	int i;
	uint16_t ns = htons(size_val);

	if (remaining_buf(buffer) < sizeof(ns)) {
		buffer->size += BUF_SIZE;
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], &ns, sizeof(ns));
	buffer->processed += sizeof(ns);

	for (i = 0; i < size_val; i++) {
		packstr(valp[i], buffer);
	}

}

/*
 * Given 'buffer' pointing to a network byte order 16-bit integer
 * (size) and a array of strings  store the number of strings in
 * 'size_valp' and the array of strings in valp
 * NOTE: valp is set to point into a newly created buffer, 
 *	the caller is responsible for calling xfree on *valp
 *	if non-NULL (set to NULL on zero size buffer value)
 */
int unpackstr_array(char ***valp, uint16_t * size_valp, Buf buffer)
{
	int i;
	uint16_t ns;
	uint16_t uint16_tmp;

	if (remaining_buf(buffer) < sizeof(ns))
		return SLURM_ERROR;

	memcpy(&ns, &buffer->head[buffer->processed], sizeof(ns));
	*size_valp = ntohs(ns);
	buffer->processed += sizeof(ns);

	if (*size_valp > 0) {
		*valp = xmalloc(sizeof(char *) * (*size_valp + 1));
		for (i = 0; i < *size_valp; i++) {
			if (unpackmem_xmalloc(&(*valp)[i], &uint16_tmp, buffer))
				return SLURM_ERROR;
		}
		(*valp)[i] = NULL;	/* NULL terminated array so that execle */
		/*    can detect end of array */
	} else
		*valp = NULL;
	return SLURM_SUCCESS;
}

/*
 * Given a pointer to memory (valp), size (size_val), and buffer,
 * store the memory contents into the buffer 
 */
void packmem_array(char *valp, uint32_t size_val, Buf buffer)
{
	if (remaining_buf(buffer) < size_val) {
		buffer->size += (size_val + BUF_SIZE);
		xrealloc(buffer->head, buffer->size);
	}

	memcpy(&buffer->head[buffer->processed], valp, size_val);
	buffer->processed += size_val;
}

/*
 * Given a pointer to memory (valp), size (size_val), and buffer,
 * store the buffer contents into memory 
 */
int unpackmem_array(char *valp, uint32_t size_valp, Buf buffer)
{
	if (remaining_buf(buffer) >= size_valp) {
		memcpy(valp, &buffer->head[buffer->processed], size_valp);
		buffer->processed += size_valp;
		return SLURM_SUCCESS;
	} else {
		*valp = 0;
		return SLURM_ERROR;
	}
}
