/* vim: expandtab:tw=68:ts=4:sw=4:
 *
 * mkdirhier.c - portable routine to emulate 'mkdir -p'
 *
 * Copyright (c) 2006-2015 Sudhi Herle <sw at herle.net>
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

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>

#include "utils.h"

static const char*
sep(const char* p)
{
    int c;
    while ((c = *p)) {
        if (c == '/')
            return p;

        p++;
    }

    return 0;
}


int
mkdirhier(const char* path, mode_t mode)
{
    char z[PATH_MAX];
    struct stat st;

    if (0 == stat(path, &st) && S_ISDIR(st.st_mode))
        return 0;

    if (strlen(path) >= PATH_MAX)
        return -ENAMETOOLONG;


    const char *prev;
    const char *next;

    prev = path;
    if (*prev == '/')
        prev++;

    while ((next = sep(prev))) {
        size_t n = next - path;
        memcpy(z, path, n);
        z[n] = 0;

        //printf("path=%.*s  z=%s\n", n, path, z);

        int r = stat(z, &st);
        if (r == 0 && !S_ISDIR(st.st_mode))
            return -ENOTDIR;
        else if (r < 0) {
            if (errno != ENOENT)
                return -errno;

            r = mkdir(z, mode);
            if (r < 0)
                return -errno;
        }

        prev = next+1;
    }

    // Make the last and final part
    if (mkdir(path, mode) < 0)
        return -errno;

    return 0;
}

/* EOF */
