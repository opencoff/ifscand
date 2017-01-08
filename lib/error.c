/* :vi:ts=4:sw=4:
 * 
 * error.c - Print error message and optionally die.
 *
 * Author Sudhi Herle <sudhi-at-herle.net>
 *
 * Copyright (c) 2016, 2017
 *  The Regents of the University of California.  All rights reserved.
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include "utils.h"


const char *program_name = 0;

void
error(int doexit, int errnum, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    ssize_t n = 0;

    va_start(ap, fmt);

    if (errnum > 0) {
        snprintf(buf, sizeof buf, "%s: %s: %s", program_name, fmt, strerror(errnum));
    } else {
        snprintf(buf, sizeof buf, "%s: %s", program_name, fmt);
    }
    n = strlen(buf);
    if (buf[n-1] != '\n') {
        buf[n++] = '\n';
        buf[n]  = 0;
    }

    fflush(stdout);
    fflush(stderr);

    vfprintf(stderr, buf, ap);

    va_end(ap);

    fflush(stderr);
    if (doexit) exit(1);
}
