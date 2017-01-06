/* :vi:ts=4:sw=4:
 * 
 * vect.h - Typesafe, templatized Vector for "C"
 *
 * Author Sudhi Herle <sudhi-at-herle.net>
 *
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

#ifndef __VECTOR_2987113_H__
#define __VECTOR_2987113_H__

    /* Provide C linkage for symbols declared here .. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>  

#include "utils.h"

/*
 * Define a vector of type 'type'
 */
#define VECT_TYPE(nm, type)     \
    struct nm {                 \
        type * array;           \
        size_t size;            \
        size_t capacity;        \
    }


/*
 * typedef a vector of type 'type'. The typedef will have the same
 * name as the struct tag.
 */
#define VECT_TYPEDEF(nm, type)  typedef VECT_TYPE(nm,type) nm


/*
 * Initialize a vector to have at least 'cap' entries as its initial
 * capacity.
 */
#define VECT_INIT(v, cap) \
    do {                        \
        int cap_ = (cap);       \
        if ( cap_ > 0 )  {      \
            if (cap_ < 16) cap_ = 16;                       \
            (v)->array = NEWZA(typeof((v)->array[0]), cap_);\
            (v)->capacity = cap_;   \
        } else {                \
            (v)->array = 0;     \
            (v)->capacity = 0;  \
        }                       \
        (v)->size = 0;          \
    } while (0)


/*
 * Delete a vector. Don't use the container after calling this
 * macro.
 */
#define VECT_FINI(v)            \
    do {                        \
        if ((v)->array)         \
            DEL((v)->array);    \
        (v)->capacity = 0;      \
        (v)->size = 0;          \
        (v)->array = 0;         \
    } while (0)


/*
 * Swap two vectors. Type-safe.
 */
#define VECT_SWAP(a, b)         \
    do {                        \
        int c0 = (a)->capacity; \
        int c1 = (a)->size;     \
        typeof((a)->array) c2 = (a)->array; \
        (a)->capacity = (b)->capacity;  \
        (a)->size     = (b)->size;      \
        (a)->array    = (b)->array;     \
        (b)->capacity = c0;             \
        (b)->size     = c1;             \
        (b)->array    = c2;             \
    } while (0)

/*
 * Reserve at least 'want' entries in the vector.
 * The _new_ capacity will be at least this much.
 */
#define VECT_RESERVE(v,want)        \
    do {                            \
        int n_ = (v)->capacity;     \
        int want_ = want;           \
        if  ( want_ >= n_ ) {       \
            do { n_ *= 2; } while (n_ < want_); \
            (v)->array = RENEWA(typeof((v)->array[0]), (v)->array, n_);\
            memset((v)->array+(v)->size, 0, sizeof((v)->array[0]) * (n_ - (v)->size));\
            (v)->capacity = n_;     \
        }                           \
    } while (0)



/*
 * Ensure at least 'xtra' _additional_ entries in the vector.
 */
#define VECT_ENSURE(v,xtra)    VECT_RESERVE(v, (xtra)+(v)->size)


/*
 * Get the next slot in the array - assumes the caller has
 * previously reserved space by using "VECT_ENSURE(v, 1)"
 */
#define VECT_GET_NEXT(v) (v)->array[(v)->size++]


/*
 * Append a new entry 'e' to the _end_ of the vector.
 */
#define VECT_PUSH_BACK(v,e)    \
    do {                       \
        VECT_ENSURE(v,1);    \
        (v)->array[(v)->size++] = e;\
    } while (0)

/* Handy synonym for PUSH_BACK()  */
#define VECT_APPEND(v,e)  VECT_PUSH_BACK(v,e)


/*
 * Remove last element and store in 'e'
 * DO NOT call this if size is zero.
 */
#define VECT_POP_BACK(v)    ({ \
                                assert((v)->size > 0); \
                                typeof((v)->array[0])   zz = (v)->array[--(v)->size]; \
                            zz;})




/*
 * Put an entry at the front of the vector.
 */
#define VECT_PUSH_FRONT(v,e)         \
    do {                             \
        typeof(e) * p_;              \
        typeof(e) * start_;          \
        VECT_ENSURE(v,1);          \
        start_ = (v)->array + 1;     \
        p_ = start_ + (v)->size - 1; \
        for (; p_ >= start_; p_--)   \
            *p_ = *(p_ - 1);         \
        *p_ = (e);                   \
        (v)->size++;                 \
    } while (0)


/*
 * Remove an entry at the front of the vector.
 */
#define VECT_POP_FRONT(v,e)\
    do {                        \
        typeof(e) * p_ = (v)->array; \
        typeof(e) * end_ = p_ + --(v)->size;\
        (e) = *p_;              \
        for (; p_ < end_; p_++) \
            *p_ = *(p_ + 1);    \
    } while (0)



/*
 * Reset the size of the vector without freeing any memory.
 */
#define VECT_RESET(v)   \
    do { \
        (v)->size = 0; \
    } while (0)



/*
 * Copy a vector from 's' to 'd'.
 */
#define VECT_COPY(d, s) \
    do { \
        VECT_RESERVE(d, (s)->size); \
        memcpy((d)->array, (s)->array, (s)->size * sizeof((s)->array[0])); \
        (d)->size = (s)->size; \
    } while (0)


/*
 * Append another vector to the end of this vector
 */
#define VECT_APPEND_VECT(d, s) \
    do { \
        VECT_ENSURE(d, (s)->size); \
        memcpy((d)->array+(d)->size, (s)->array, (s)->size * sizeof((s)->array[0])); \
        (d)->size += (s)->size; \
    } while (0)

/*
 * Sort the vector 'v' using the sort function 'cmp'.
 * The sort function must use the same type signature as qsort()
 */
#define VECT_SORT(v, cmp) \
    do { \
        qsort((v)->array, (v)->size, sizeof((v)->array[0]), \
                (int (*)(const void*, const void*)) cmp); \
    } while (0)



/*
 * Fisher Yates Shuffle - randomize the elements.
 */
#define VECT_SHUFFLE(v, rnd) do {                                   \
                                typeof(v) _v = (v);                 \
                                typeof(_v->array) _p = _v->array;   \
                                typeof(_p[0]) _e;                   \
                                int _i;                             \
                                for (_i= _v->size-1; _i > 0; _i--){ \
                                    int _j = rnd() % (_i+1);        \
                                    _e     = _p[_i];                \
                                    _p[_i] = _p[_j];                \
                                    _p[_j] = _e;                    \
                                }                                   \
                            } while (0)


/*
 * Reservoir sampling: choose k random elements with equal probability.
 *
 * Read 'k' uniform random samples from 's' and write to 'd' - using
 * random number generator rnd(). 'rnd' is expected to generate true
 * random numbers with the full width of size_t (at least 32 bits).
 */
#define VECT_SAMPLE(d, s, k, rnd) ({    int zz = 0;                             \
                                        size_t kk = (k);                        \
                                        typeof(s) b = (s);                      \
                                        if (kk <  b->size) {                    \
                                            typeof(d) a = (d);                  \
                                            typeof(a->array) x = a->array;      \
                                            typeof(a->array) y = b->array;      \
                                            size_t i;                           \
                                            VECT_RESERVE(a, kk);                \
                                            a->size = kk;                       \
                                            for (i=0; i < kk; i++)              \
                                                x[i] = y[i];                    \
                                            for (i=kk; i < b->size; i++) {      \
                                                size_t j = rnd() % (i+1);       \
                                                if (j < kk) x[j] = y[i];        \
                                            }                                   \
                                            zz = 1;                             \
                                        }                                       \
                                        zz;})



/*
 * Return pointer to a random element uniformly chosen from the vector.
 * Use 'rnd()' as the uniform random number generator.
 * 'rnd' is expected to generate true random numbers with the full
 * width of size_t (at least 32 bits).
 */
#define VECT_RAND_ELEM(v, rnd) ({   typeof(v) a = v;            \
                                    size_t n = rnd() % a->size; \
                                 &a->array[n];})

/*
 * Iterate through vector 'v' and set each element to the pointer
 * 'p'
 */
#define VECT_FOR_EACH(v, p)     for (p = &(v)->array[0];        p < &VECT_END(v); ++p)
#define VECT_FOR_EACHi(v, i, p) for (p = &(v)->array[0], i = 0; i < (v)->size;    ++p, ++i)

/* -- Various accessors -- */

/*
 * Get the first and last element (both are valid entries).
 * No validation is done with respect to size etc.
 */
#define VECT_FIRST_ELEM(v)     ((v)->array[0])
#define VECT_LAST_ELEM(v)      ((v)->array[(v)->size-1])
#define VECT_TOP(v)            VECT_LAST_ELEM(v)

/*
 * One past the last element. Useful when iterating with vectors
 */
#define VECT_END(v)            ((v)->array[(v)->size])

/* Fetch the n'th element */
#define VECT_ELEM(v,n)  ((v)->array[(n)])

#define VECT_SIZE(v)        ((v)->size)
#define VECT_CAPACITY(v)    ((v)->capacity)


/*
 * Bit vector
 */

struct bitvect
{
    uint64_t *b;
    uint64_t  n;
};
typedef struct bitvect bitvect;


static inline void
bv_init(bitvect *b, size_t n)
{
    uint64_t words = n / 8 + (n % 8 != 0);
    b->b = NEWZA(uint64_t, words);
    b->n = n;
}

static inline void
bv_cleanup(bitvect *b)
{
    DEL(b->b);
    b->n = 0;
}

/*
 * Allocate locally on the stack. This necessarily has to be a
 * macro!
 */
#define bv_init_alloca(bv, m) \
            do { \
                uint64_t n = (m);  \
                bitvect *b = (bv); \
                uint64_t words = n / 8 + (n % 8 != 0);  \
                b->b = (uint64_t*)alloca(words * sizeof(uint64_t)); \
                b->n = n;  \
            } while (0)


static inline void
bv_set(bitvect *b, uint64_t i)
{
    assert(i < b->n);
    uint64_t w = i / 8;
    uint64_t v = i % 8;
    b->b[w] |= (1 << v);
}

static inline void
bv_clr(bitvect *b, uint64_t i)
{
    assert(i < b->n);
    uint64_t w = i / 8;
    uint64_t v = i % 8;
    b->b[w] &= ~(1 << v);
}

static inline int
bv_isset(bitvect *b, uint64_t i)
{
    assert(i < b->n);
    uint64_t w = i / 8;
    uint64_t v = i % 8;
    return !!(b->b[w] & (1 << v));
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! __VECTOR__2987113_H__ */

/* EOF */
