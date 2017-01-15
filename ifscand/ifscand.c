/*  $OpenBSD: ifconfig.c,v 1.330 2016/09/03 13:46:57 reyk Exp $ */
/*  $NetBSD: ifconfig.c,v 1.40 1997/10/01 02:19:43 enami Exp $  */

/*
 * Copyright (c) 1983, 1993
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
#include <poll.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <signal.h>

#include <net/if.h>

#include "ifscand.h"
#include "db.h"


volatile uint32_t   Quit = 0;
volatile uint32_t   Sig  = 0;

int  Debug       = 0;
int  Foreground  = 0;
int  Network_cfg = 1;    // default

static int opensock(const char *fn);
static int sockready(int fd, int wr, int delay);

static ssize_t sockread(int fd, fast_buf* b, struct sockaddr_un *);
static ssize_t sockwrite(int fd, fast_buf* b, struct sockaddr_un *);

extern void parse_opts(int argc, char *argv[]);

/*
 * Long and short options.
 */
static const struct option Lopt[] = {
    {"help",        no_argument, 0, 'h'},
    {"debug",       no_argument, 0, 'd'},
    {"foreground",  no_argument, 0, 'f'},
    {"no-network",  no_argument, 0, 'N'},
    {0, 0, 0, 0}
};
static const char Sopt[] = "hdfN";

static void
sighandle(int sig)
{
    Quit = 1;
    Sig  = sig;
}

static void
sigignore(int sig)
{
    (void)sig;
}


static void
usage()
{
    printf("%s - Scan for known WiFi access points and automatically join\n"
           "Usage: %s [options] INTERFACE\n"
           "\n"
           "Options:\n"
           "  --debug, -d       Run in debug mode (extra logs)\n"
           "  --foreground, -f  Don't daemonize into the background\n"
           "  --no-network, -N  Don't do any network configuration\n"
           "  --help, -h        Show this help message and quit\n",
           program_name, program_name);

    exit(0);
}


int
main(int argc, const char* argv[])
{
    char sockfile[PATH_MAX];
    int c;

    program_name = argv[0];

    while ((c = getopt_long(argc, argv, Sopt, Lopt, 0)) != EOF) {
        switch (c) {
            case 0:
                break;

            default:
            case 'h':
                usage();
                break;

            case 'd':
                Debug = 1;
                break;

            case 'f':
                Foreground = 1;
                break;

            case 'N':
                Network_cfg = 0;
                break;
        }
    }

    if (optind >= argc) error(1, 0, "Insufficient arguments. Try '%s --help'", program_name);

    const char* ifname = argv[optind];

    snprintf(sockfile, sizeof sockfile, "%s.%s", IFSCAND_SOCK, ifname);

    apdb db;
    ifstate ifs;

    initlog(ifname);

    db_init(&db, ifname);

    int r = ifstate_init(&ifs, ifname);
    if (r < 0) error(1, -r, "can't initialize %s", ifname);

    // Daemonize now.
    if (!Foreground) {
        r = daemon(0, Debug ? 1 : 0);
        if (r != 0) error(1, errno, "can't daemonize");
    }

    ifs.db      = &db;
    ifs.ipcfd   = opensock(sockfile);
    ifs.timeout = IFSCAND_INT_SCAN;

    signal(SIGINT,  sighandle);
    signal(SIGTERM, sighandle);
    signal(SIGHUP,  sighandle);
    signal(SIGPIPE, sigignore);

    // Pledge and reduce privileges
    // XXX Except - when forking dhclient and ifconfig we need
    //     to open /dev/null etc.
    //if (pledge("stdio rpath ioctl") < 0) error(1, errno, "can't pledge");

    printlog(LOG_INFO, "starting daemon for %s %s network-config..", ifname,
                Network_cfg ? "with" : "WITHOUT");
    printlog(LOG_INFO, "Listening on %s, prefs in %s.db", sockfile, IFSCAND_PREFS);


    /* Run state machine on startup -- scan and setup before
     * anything else.
     */
    wifi_scan(&ifs);

    /*
     * Now, run every scan-interval seconds
     */
    int delay   = ifs.timeout;
    int fd      = ifs.ipcfd;
    int errs    = 0;
    cmd_state s = { .fd = fd, .db  = &db, .ifs = &ifs };

    fast_buf_init(&s.in, 2048);
    fast_buf_init(&s.out, 65536);

    printlog(LOG_INFO, "scanning %s every %d seconds ...", ifname, delay);

    while (1) {
        r = sockready(fd, 0, delay);

        if (Quit) break;

        if (r > 0) { // fd  is valid and we have a new connection.
            ssize_t m = sockread(fd, &s.in, &s.from);
            if (m > 0) {
                debuglog("processing I/O from control program..");
                cmd_process(&s);
                if (fast_buf_size(&s.out) > 0) sockwrite(fd, &s.out, &s.from);

                fast_buf_reset(&s.in);
                fast_buf_reset(&s.out);
            }

            /*
             * We may have added a new AP. So, scan regardless.
             */
        } 

#define MAXERRS 5
        if (wifi_scan(&ifs) < 0) {
            if (++errs >= MAXERRS) {
                printlog(LOG_ERR, "Too many consecutive errors; aborting!");
                break;
            }
        } else {
            errs  = 0;
            delay = ifs.timeout;
        }

        /*
         * Check after we handle any commands and/or statemachine
         * stuff. e.g., we may have received a "down" command.
         */
        if (Quit) break;
    }
    
    if (Sig > 0)
        printlog(LOG_INFO, "Caught signal %d; quitting ..", Sig);
    else
        printlog(LOG_INFO, "Ending daemon for %s..", ifname);

    close(fd);
    disconnect_ap(&ifs, &ifs.curap);
    ifstate_close(&ifs);
    db_close(&db);
    unlink(sockfile);
    
    return 0;
}




void
initlog(const char *ifname)
{
    if (Foreground) return;

    static char buf[64];    // openlog() requires this to be constant! Grr.
    snprintf(buf, sizeof buf, "ifscand.%s", ifname);

    openlog(buf, LOG_CONS|LOG_NDELAY|LOG_PID, LOG_DAEMON);
}

void
printlog(int level, const char *fmt, ...)
{
    char buf[4096];

    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (Foreground) {
        size_t n = strlen(buf);
        fputs(buf, stderr);
        if (buf[n-1] != '\n') fputc('\n', stderr);
    } else {
        syslog(LOG_DAEMON|level, buf);
    }
}



void
debuglog(const char *fmt, ...)
{
    if (!Debug) return;

    char buf[4096];

    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (Foreground) {
        size_t n = strlen(buf);
        fputs(buf, stderr);
        if (buf[n-1] != '\n') fputc('\n', stderr);
    } else {
        syslog(LOG_DAEMON|LOG_DEBUG, buf);
    }
}


/*
 * set FD_CLOEXEC on the given fd.
 *
 * Return:
 *  0 on success
 *  -errno on failure
 */
int
fd_set_cloexec(int fd)
{
    int r = fcntl(fd, F_GETFD, 0);
    if (r < 0) return -errno;
    if (r & FD_CLOEXEC) return 0;

    r = fcntl(fd, F_SETFD, r | FD_CLOEXEC);
    if (r < 0) return -errno;

    return 0;
}


/*
 * Open a AF_UNIX socket for communication from the client.
 */
static int
opensock(const char *fn)
{
    int r;

    unlink(fn);
    
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) error(1, errno, "can't open socket %s", fn);

    struct sockaddr_storage ss;
    struct sockaddr_un *un = (struct sockaddr_un *)&ss;

    un->sun_family = AF_UNIX;
    snprintf(un->sun_path, sizeof un->sun_path, "%s", fn);

    if (bind(fd, (struct sockaddr *)un, sizeof *un) < 0)
        error(1, errno, "can't bind to socket %s", fn);

    if (listen(fd, 5) < 0) error(1, errno, "can't listen on socket %s", fn);

    /*
     * Make it writable atleast by the group.
     *
     * XXX Which group?
     */
    if (chmod(fn, 0660) < 0) error(1, errno, "can't change permissions on %s", fn);

    r = fd_set_cloexec(fd);
    if (r < 0) error(1, -r, "can't set FD_CLOEXEC on fd");

    return fd;
}


/*
 * Wait for 'fd' to be I/O ready. If 'wr' is true, then wait for
 * IO-WRITE-READY, else IO-READ_READY. In either case, don't wait
 * for more than 'delay' SECONDS.
 *
 * Return:
 *   0 on timeout
 *   > 0 on ready
 *   EOF on socket close
 */
static int
sockready(int fd, int wr, int delay)
{
    struct pollfd fds[1];
    struct pollfd *f = &fds[0];
    int r;

    f->fd      = fd;
    f->events  = POLLHUP | (wr ? POLLOUT: POLLIN);
    f->revents = 0;

    r = poll(fds, 1, delay *1000);
    if (r == 0) return 0;   // timeout
    if (r < 0)  return -errno;

    if (f->revents & POLLHUP) return EOF;  // socket closed

    return 1;
}



/*
 * Read from a DGRAm socket into fastbuf 'b'.
 *
 * Will implicitly read from b->ptr.
 *
 * Return:
 *     0 on EOF
 *     > 0  number of bytes read
 *     -errno on error
 */
static ssize_t
sockread(int fd, fast_buf *b, struct sockaddr_un *from)
{
    uint8_t *p = fast_buf_endptr(b);
    size_t   n = fast_buf_avail(b) - 1; // null terminate the string
    socklen_t len = sizeof *from;

    ssize_t m = recvfrom(fd, p, n, MSG_DONTWAIT, (struct sockaddr *)from, &len);
    if (m == -1) return -errno;

    p[m] = 0;
    fast_buf_advance(b, m);

    return m;
}


/*
 * Write to a DGRAm socket with data in 'b' to destination 'to'
 *
 * Will implicitly read from b->ptr.
 *
 * Return:
 *     0 on EOF
 *     > 0  number of bytes read
 *     -errno on error
 */
static ssize_t
sockwrite(int fd, fast_buf *b, struct sockaddr_un *to)
{
    uint8_t *p = fast_buf_ptr(b);
    size_t   n = fast_buf_size(b);

    while (n > 0) {
        ssize_t m = sendto(fd, p, n, MSG_NOSIGNAL, (struct sockaddr *)to, sizeof *to);
        if (m == -1) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return -errno;
        }
        p += m;
        n -= m;
    }

    return fast_buf_size(b);
}
