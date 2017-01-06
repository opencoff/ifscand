/* :vim:ts=4:sw=4:
 *
 * strtrim.c -  Trim a string of leading & trailing white spaces.
 *
 * Copyright (c) 1999-2015 Sudhi Herle <sw at herle.net>
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
#include <ctype.h>
#include "utils.h"

/*
 * Remove all leading & trailing white-spaces from input string _inplace_.
 */
char *
strtrim(char *in_str)
{
    unsigned char  *str = (unsigned char *) in_str,
                   *ptr,
                   *str_begin,
                   *str_end;
    unsigned int c;

    if (!str) return 0;

    /* Remove leading spaces */
    for (str_begin = str; (c = *str); str++) {
        if (isspace (c)) continue;
        
        /* Copy from str_begin to end --skipping trailing white
         * spaces */

        str_end = str_begin;
        for (ptr = str_begin; (c = *ptr = *str); str++, ptr++) {
            if (!isspace (c))
                str_end = ptr;
        }
        *(str_end + 1) = 0;
        return (char *) str_begin;
    }


    *str_begin = 0;
    return (char *) str_begin;
}

#ifdef  TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define     fatal(msg)  do { \
        fprintf(stderr, "Test failed! --%s\n", (msg)) ;\
        exit(1) ; \
        } while (0)

int
main ()
{
    char a[32] = "abcdef";
    char b[32] = "  abcdef";
    char c[32] = "   abcdef   ";
    char d[32] = "";
    char e[32] = "    ";
    char f[32] = "  \"알림간격\"  ";

    if (strcmp ("abcdef", strtrim (a)) != 0)
        fatal ("Failed on ``a''");
    if (strcmp ("abcdef", strtrim (b)) != 0)
        fatal ("failed on ``b''");
    if (strcmp ("abcdef", strtrim (c)) != 0)
        fatal ("failed on ``c''");
    if (strcmp ("", strtrim (d)) != 0)
        fatal ("failed on ``d''");
    if (strcmp ("", strtrim (e)) != 0)
        fatal ("failed on ``e''");
    if (strcmp ("\"알림간격\"", strtrim (f)) != 0)
        fatal ("failed on ``f''");

    exit(0) ;
}

#endif
/* EOF */
