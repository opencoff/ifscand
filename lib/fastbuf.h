/* :vi:ts=4:sw=4:
 * 
 * fastbuf.h    - a growable fast buffer.
 *
 * Copyright (c) 2005-2015 Sudhi Herle <sw at herle.net>
 * Copyright (c) 2016, 2017
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __FAST_BUF_H__
#define __FAST_BUF_H__ 1

    /* Provide C linkage for symbols declared here .. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    
#include <stdlib.h>
#include "utils.h"
    

struct fast_buf
{
    unsigned char * buf;
    size_t size;
    size_t capacity;
};
typedef struct fast_buf fast_buf;



/*
 * Initialize the fast buffer with an initial size of 'sz'.
 */
#define fast_buf_init(b,sz)   do { \
    fast_buf * b_ = (b); \
    size_t sz_ = (sz); \
    b_->capacity = sz_; \
    b_->size     = 0;   \
    b_->buf      = NEWA(unsigned char, sz_); \
} while (0)




/*
 * Finalize the fastbuf and free any storage that was
 * allocated.
 */
#define fast_buf_fini(b)      do { \
    DEL ((b)->buf);       \
} while (0)



/*
 * Internal macro to grow the buffer on demand.
 */
#define _fast_buf_expand(b,req) do { \
        fast_buf * b__ = (b); \
        size_t n__ = b__->capacity;     \
        size_t want__ = (req) + b__->size; \
        if  ( want__ >= n__ ) {       \
            do { n__ *= 2; } while (n__ < want__); \
            b__->buf = RENEWA(unsigned char, b__->buf, n__);\
            b__->capacity = n__;     \
        }                           \
    } while (0)




/*
 * Push a sequence of bytes to the end of the buffer.
 */
#define fast_buf_push(b,d,l)    do { \
    fast_buf * b_ = (b); \
    size_t l_ = (l); \
    _fast_buf_expand(b_,l_);      \
    memcpy(b_->buf+b_->size, (void *)(d), l_); \
    b_->size += l_; \
} while (0)



/*
 * Ensure that the buffer has atleast 'n' bytes of space.
 */
#define fast_buf_ensure(b,l)    _fast_buf_expand(b,l)



/*
 * Append one byte to the fastbuf.
 */
#define fast_buf_append(b,c)    do { \
    fast_buf * b_ = (b); \
    _fast_buf_expand(b_,1);      \
    b_->buf[b_->size++] = (unsigned char)(c);  \
} while (0)



/*
 * Reset a fast buf to initial conditions.
 */
#define fast_buf_reset(b)       do { \
    (b)->size = 0;\
} while (0)



/*
 * Advance a fast buf ptr by 'n' bytes.
 */
#define fast_buf_advance(b,n)   do { \
    (b)->size += (n); \
} while (0)




/*
 * Return a pointer to the beginning of the fast buffer.
 */
#define fast_buf_ptr(b)     (b)->buf


/*
 * Return a pointer to the end of the fast buffer.
 */
#define fast_buf_endptr(b)  ((b)->buf+(b)->size)


/* Return total capacity of buffer */
#define fast_buf_capacity(b)  (b)->capacity

/*
 * Return available capacity in the fast buffer.
 */
#define fast_buf_avail(b)     ((b)->capacity - (b)->size)


/*
 * Return the size of the fast buffer.
 */
#define fast_buf_size(b)    (b)->size



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! __FAST_BUF_H__ */

/* EOF */
