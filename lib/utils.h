/* vim: expandtab:tw=68:ts=4:sw=4:
 *
 * utils.h - Simple utility functions
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

#ifndef ___UTILS_H_3492484_1482328197__
#define ___UTILS_H_3492484_1482328197__ 1

    /* Provide C linkage for symbols declared here .. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <stdlib.h>

/* Macro to allocate a zero initialized item of type 'ty_' */
#define NEWZ(ty_)           (ty_ *) calloc(1, sizeof (ty_))

/* Macro to allocate a zero initialized array of type 'ty_' items */
#define NEWZA(ty_,n)        (ty_ *) calloc((n), sizeof (ty_))
#define NEWA(ty_,n)         (ty_ *) malloc((n)*sizeof (ty_))

/* Macro to 'realloc' an array 'ptr' of items of type 'ty_' */
#define RENEWA(ty_,ptr,n)   (ty_ *) realloc(ptr, (n) * sizeof(ty_))

/*
 * Corresponding macro to "free" an item obtained via NEWx macros.
 */
#define DEL(ptr)            do { \
                                if (ptr) free(ptr); \
                                ptr = 0; \
                            } while (0)

#define ARRAY_SIZE(a)       ((sizeof(a))/sizeof(a[0]))


/*
 * Remove leading & trailing whitespaces - inplace.
 */
extern char *strtrim(char *);


/*
 * Remove any leading & trailing quote from 's'; return the unquoted string
 * in 'p_ret'.
 *
 * Returns:
 *    0     if no quote found
 *    < 0   if missing closing quote or mismatched quote
 *    > 0   the actual quote char we removed.
 */
int strunquote(char *s, char **p_ret);


/**
 * Split a string into tokens just as shell would; handle quoted
 * words as a single word. Uses whitespace (' ', '\t') as delimiter
 * tokens. Delimited tokens are stuffed in 'args'. 
 *
 * Note: args points to locations within the original string 's'.
 * Thus, 's' is modified.
 *
 * Returns:
 *
 *    >= 0  number of parsed tokens
 *    < 0   on error:
 *          -ENOSPC if array is too small
 *          -EINVAL if ending quote is missing
 */
extern int strsplitargs(char **args, size_t n, char *s);

/*
 * Decode hex chars in string 'str' and put them into 'out'.
 *
 * Return:
 *   > 0    Number of decoded bytes
 *   < 0    -errno on failure
 */
ssize_t str2hex(uint8_t* out, size_t outlen, const char* str);


/*
 * Safe mkdir -p
 *
 * Returns:
 *  0       on success
 *  -errno  on failure
 */
extern int mkdirhier(const char *dn, mode_t m);

/*
 * Show an error message to stderr. If 'doexit' is true, then exit
 * the program as well. If 'errnum' is non-zero, treat it as 'errno'
 * and fetch the string description via strerror(3).
 */
extern void error(int doexit, int errnum, const char *fmt, ...);
extern const char *program_name;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! ___UTILS_H_3492484_1482328197__ */

/* EOF */
