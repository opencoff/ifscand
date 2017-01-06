/* vim: expandtab:tw=68:ts=4:sw=4:
 *
 * str2hex.c -- Decode hex string to byte stream
 *
 * Copyright (c) 2013-2015 Sudhi Herle <sw at herle.net>
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

#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/types.h>

#include "utils.h"


/*
 * Decode hex chars in string 'str' and put them into 'out'.
 *
 * Return:
 *   > 0    Number of decoded bytes
 *   < 0    -errno on failure
 */
ssize_t
str2hex(uint8_t* out, size_t outlen, const char* str)
{
    uint8_t* start = out;
    size_t n       = strlen(str);

    if (outlen < ((n+1)/2))  return -ENOMEM;

    uint8_t byte = 0;
    int shift    = 4;

    while (n--) {
        unsigned char nybble = *str++;
        switch (nybble) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                byte |= (nybble - '0') << shift;
                break;
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                byte |= (nybble - 'a' + 10) << shift;
                break;
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
                byte |= (nybble - 'A' + 10) << shift;
                break;
            default:
                return -EINVAL;
        }

        if ((shift -= 4) == -4) {
            *out++ = byte;
            byte = 0x00;
            shift = 4;
        }
    }

    if (shift != 4) *out++ = byte;

    return out - start;
}

