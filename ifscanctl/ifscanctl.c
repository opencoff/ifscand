/* :vim:ts=4:sw=4:
 * 
 * ifscand-control.c - unprivileged control program for ifscand
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
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>

#include "utils.h"
#include "common.h"


/*
 * Global vars
 */

static size_t arg2str(char *dest, size_t bsiz, int argc, const char *argv[]);
static int hasws(const char *s);
static void fullwrite(int fd, void *buf, size_t n);
static ssize_t fullread(int fd, void *buf, size_t n);

int
main(int argc, const char *argv[])
{
    char sockfile[PATH_MAX];
    struct sockaddr_un un;

    program_name = argv[0];
    if (argc < 3) error(1, 0, "Usage: %s [options] ifname command\n", argv[0]);

    const char* ifname = argv[1];
    snprintf(sockfile, sizeof sockfile, "%s.%s", IFSCAND_SOCK, ifname);
    size_t n = 1 + strlen(sockfile);    // +1 for trailing null

    if (n > sizeof un.sun_path) error(1, 0, "socket path %s too long?", sockfile);

    struct sockaddr_un loc;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) error(1, errno, "can't create control socket");

    loc.sun_family = AF_UNIX;
    snprintf(loc.sun_path, sizeof loc.sun_path, "/tmp/.ifscand-control-%d-%u", getpid(), arc4random());
    if (bind(fd, (struct sockaddr *)&loc, sizeof loc) < 0)
        error(1, errno, "can't bind to socket %s", loc.sun_path);

    un.sun_family = AF_UNIX;
    memcpy(un.sun_path, sockfile, n);

    if (connect(fd, (struct sockaddr *)&un, sizeof un) < 0) 
        error(1, errno, "can't connect to %s", sockfile);

    char buf[65536];
    n = arg2str(buf, sizeof buf, argc-2, &argv[2]);

    fullwrite(fd, buf, n);
    n = fullread(fd, buf, (sizeof buf)-1);
    if (n > 0) {
        buf[n] = 0;
        fputs(buf, stdout);
        if (buf[n-1] != '\n') fputc('\n', stdout);
    }

    close(fd);
    unlink(loc.sun_path);

    return 0;
}


/*
 * Return true if the string 's' has white space.
 */
static int
hasws(const char *s)
{
    int c;
    for (; (c = *s); s++) {
        if (isspace(c)) return 1;
    }
    return 0;
}


static size_t
arg2str(char *buf, size_t bsiz, int argc, const char *argv[])
{
    int i;
    char *p  = buf;
    size_t n = bsiz;
    ssize_t m;
    const char *s;

    for (i = 0; i < argc-1; i++) {
        s = argv[i];
        snprintf(p, n, hasws(s) ? "\"%s\" " : "%s ", s);
        m  = strlen(p);
        p += m;
        n -= m;
    }

    s = argv[i];
    snprintf(p, n, hasws(s) ? "\"%s\"" : "%s", s);
    m  = strlen(p);
    p += m;
    n -= m;

    return bsiz - n;
}



static void
fullwrite(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;

    while (n > 0) {
        ssize_t m = write(fd, p, n);
        if (m == -1) {
            if (errno == EINTR || errno == EAGAIN) continue;
            error(1, errno, "I/O error");
        }

        p += m;
        n -= m;
    }
}


static ssize_t
fullread(int fd, void *buf, size_t n)
{
    struct sockaddr_un un;
    socklen_t len = sizeof un;
    while (1) {
        ssize_t m = recvfrom(fd, buf, n, MSG_DONTWAIT, (struct sockaddr*)&un, &len);
        if (m == -1) {
            if (errno == EINTR || errno == EAGAIN) continue;
            error(1, errno, "I/O error");
        }
        return m;
    }
}

