/*  $OpenBSD: ifscand.c,v 1.330 2016/09/03 13:46:57 reyk Exp $
 *
 * scan.c - WiFi Scanning Logic
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
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "ifscand.h"

static void do_scan(ifstate *s, int low_rssi);
static void start_dhcp(ifstate *ifs);
static void stop_dhcp(void);
static void check_dhcp(ifstate *);
static void cleanup_state(ifstate *ifs);
static void reopen_std_fds(void);
static int connect_ap(ifstate *s, const apdata *ap);

/*
 * Measure RSSI and compare against previous values to determine if
 * we need to abandon our current AP and look for another one.
 *
 * Return:
 *      True    if all is well
 *      False   otherwise
 *      < 0     on any error (-errno)
 */
static int check_rssi(ifstate *s);


/*
 * Return true if the two AP are the same.
 */
static inline int
same_ap(const apdata *a, const apdata *b)
{
    size_t n = strlen(a->apname);
    size_t m = strlen(b->apname);

    if (n != m) return 0;

    return 0 == memcmp(a->apname, b->apname, n);
}


/*
 * Scan for WiFi or measure RSSI.
 *
 * Return:
 *      0 on success
 *      -errno on failure
 */
int
wifi_scan(ifstate *ifs)
{
    int r;
    int low_rssi = 0;

    if (ifs->associated) {
        apdata *ap = &ifs->curap;

        // Verify DHCP is running
        if (ap->flags & AP_IN4DHCP) {
            check_dhcp(ifs);
        }

        r = check_rssi(ifs);
        if (r < 0 || r > 0) return r;

        // RSSI is at critical point. Scan.
        low_rssi = 1;
    } 

    do_scan(ifs, low_rssi);

    return 0;
}


static void
do_scan(ifstate *ifs, int low_rssi)
{
    int r = ifstate_scan(ifs);
    if (r < 0) {
        printlog(LOG_ERR, "can't scan: %s", strerror(-r));
        error(1, -r, "can't scan %s", ifs->ifname);
    }

    apvect av;

    VECT_INIT(&av, 8);

    // Filter out the nodes we don't want.
    db_filter_ap(ifs->db, &av, &ifs->nv);

    if (VECT_SIZE(&av) == 0) {
        if (ifs->associated) disconnect_ap(ifs, &ifs->curap);

        db_get_uint(ifs->db, "scan-int", &ifs->timeout);
        ifs->associated = 0;
        goto end;
    }


    /* Pick the first node in the list.
     * However, this might be the same one we are currently
     * associated with _and_ in LOW_RSSI situation..
     */
    apdata *d = &VECT_ELEM(&av, 0);

    if (ifs->associated) {
        apdata *ap = &ifs->curap;

        if (same_ap(ap, d)) {
            if (!low_rssi)           goto end;
            if (VECT_SIZE(&av) == 1) goto end;

            d = &VECT_ELEM(&av, 1);
            debuglog("Cur AP %s: Low RSSI; picking next AP %s", ap->apname, d->apname);
        }
        disconnect_ap(ifs, ap);
    }

    r = connect_ap(ifs, d);
    if (r > 0) {
        ifs->associated = 1;

        db_get_uint(ifs->db, "rssi-scan-int", &ifs->timeout);
        rssi_avg_init(&ifs->avg);
        rssi_avg_add_sample(&ifs->avg, RSSI(&ifs->curap));
    } else {
        printlog(LOG_ERR, "can't connect to AP '%s': %s", d->apname, strerror(-r));

        ifs->associated = 0;
        db_get_uint(ifs->db, "scan-int", &ifs->timeout);
    }

end:
    VECT_FINI(&av);
}


static int
check_rssi(ifstate *ifs)
{
    apdata *cur = &ifs->curap;

    int r = ifstate_get_rssi(ifs, cur->apname, cur->nr_bssid);
    if (r < 0) {
        uint8_t *m = cur->nr_bssid;
        printlog(LOG_ERR, "%s: can't measure rssi of '%s' (" MACFMT "): %s",
                ifs->ifname, cur->apname, sMAC(m), strerror(-r));
        return r;
    }

    rssi_avg_add_sample(&ifs->avg, r);
    int avg = rssi_avg_value(&ifs->avg);

    debuglog("AP %s: RSSI %d, AVG %d", cur->apname, r, avg);
    return avg >= 0 && avg < IFSCAND_RSSI_LOWEST ? 0 : 1;
}



/*
 * Run an external program.
 *
 * Return:
 *  True    on success
 *  False   on failure
 */
static int
run_prog(ifstate *ifs, char * const argv[], char * const env[])
{
    const char * const exe = argv[0];
    int                  r = valid_exe_p(exe);
    if (r <= 0) {
        printlog(LOG_ERR, "%s is not a file or is not executable", exe);
        return 0;
    }

    pid_t pid = fork();
    if (pid == -1) {
        r = errno;
        printlog(LOG_ERR, "can't fork: %s", strerror(r));
        error(1, r, "can't fork %s", exe);
        return 0;
    }

    if (pid == 0) { // child
        cleanup_state(ifs);
        reopen_std_fds();

        chdir("/tmp");
        execve(exe, argv, env);
        printlog(LOG_ERR, "can't exec %s: %s", exe, strerror(errno));
    } else {
        // parent. Wait for child to finish.
        r = 0;
        waitpid(pid, &r, 0);

        debuglog("%s: exit code %d", exe, r);

        if (WIFEXITED(r)) {
            int x = WEXITSTATUS(r);
            if (x != 0) {
                printlog(LOG_ERR, "'%s' exited abnormally with %d",
                        exe, x);
            }
        } else if (WIFSIGNALED(r)) {
            int sig = WTERMSIG(r);
            printlog(LOG_ERR, "'%s' caught signal %d and aborted",
                    exe, sig);
        }
    }
    return 1;
}


/*
 * Run ifconfig(8) and route(8) to configure the interface.
 *
 * We clean the environment - and only set PATH. All other ENV vars
 * are stripped.
 */
static void
ifconfig_up(ifstate *s, const apdata *ap)
{
    char * const env[] = { "PATH=/sbin:/usr/sbin:/bin:/usr/bin", 0 };
    char *args[16];

    /*
     * IP Address configuration
     */
    char gw[128];

    args[0] = "/sbin/ifconfig";
    args[1] = s->ifname;

    if (ap->flags & AP_IN4) {
        char rip4[32];
        char rm4[32];
        char ip4[128];

        inet_ntop(AF_INET, &ap->in4,   rip4, sizeof rip4);
        inet_ntop(AF_INET, &ap->mask4, rm4,  sizeof rm4);

        snprintf(ip4, sizeof ip4, "%s/%s", rip4, rm4);

        args[2] = "inet";
        args[3] = ip4;
        args[4] = "up";
        args[5] = 0;

        if (!run_prog(s, args, env)) return;
    }

    if (ap->flags & AP_IN6) {
        char rip6[64];
        char rm6[64];
        char ip6[256];
        inet_ntop(AF_INET6, &ap->in6,   rip6, sizeof rip6);
        inet_ntop(AF_INET6, &ap->mask6, rm6,  sizeof rm6);

        snprintf(ip6, sizeof ip6, "%s/%s", rip6, rm6);

        args[2] = "inet6";
        args[3] = ip6;
        args[4] = "up";
        args[5] = 0;

        if (!run_prog(s, args, env)) return;
    }

    if (ap->flags & AP_GW4) {
        inet_ntop(AF_INET, &ap->gw4, gw, sizeof gw);

        args[0] = "/sbin/route";
        args[1] = "add";
        args[2] = "-inet";
        args[3] = "default";
        args[4] = gw;
        args[5] = 0;

        if (!run_prog(s, args, env)) return;
    }

    if (ap->flags & AP_GW6) {
        inet_ntop(AF_INET6, &ap->gw6, gw, sizeof gw);

        args[0] = "/sbin/route";
        args[1] = "add";
        args[2] = "-inet6";
        args[3] = "default";
        args[4] = gw;
        args[5] = 0;

        if (!run_prog(s, args, env)) return;
    }
}


/*
 * Return true if successful, false otherwise.
 *
 * XXX do a SIOCG80211NODE to see which BSSID the kernel driver
 * connected to..
 */
static int
connect_ap(ifstate *s, const apdata *ap)
{
    int r;
    printlog(LOG_INFO, "connecting to AP \"%s\"", ap->apname);

    r = ifstate_config(s, ap, &s->curap);
    if (r < 0) {
        printlog(LOG_INFO, "can't configure interface for AP '%s': %s",
                    ap->apname, strerror(-r));
        return 0;
    }

    /*
     * If we are asked to only configure link layer, we forego
     * configuring IP Addresses
     */
    if (Linklayer) {
        printlog(LOG_INFO, "skipping IP address configuration for %s..", ap->apname);
        return 1;
    }

    if (ap->flags & AP_IN4DHCP) {
        start_dhcp(s);
        return 1;
    }

    if ((ap->flags & (AP_IN4|AP_IN6))) {
        ifconfig_up(s, ap);
    }

    return 1;
}


/*
 * Disconnect from 'ap'.
 */
int
disconnect_ap(ifstate *s, apdata *ap)
{
    if (0 == strlen(ap->apname)) return 1;

    printlog(LOG_INFO, "disconnecting from AP \"%s\"", ap->apname);

    ifstate_unconfig(s);

    if (ap->flags & AP_IN4DHCP) {
        stop_dhcp();
    } else if ((ap->flags & (AP_IN4|AP_IN6))) {
        ifstate_set(s, 0);
    }

    memset(ap, 0, sizeof *ap);
    return 1;
}




static pid_t Dhpid = -1;

static void
start_dhcp(ifstate *ifs)
{
    /* Lets try HUP'ing it */
    if (Dhpid > 0) {
        debuglog("Restarting (SIGHUP) existing dhclient %d..", Dhpid);
        kill(Dhpid, SIGINT);
    }

    const char *exe =  "/sbin/dhclient";
    int         r   = valid_exe_p(exe);
    if (r <= 0) {
        printlog(LOG_ERR, "%s is not a file or is not executable", exe);
        return;
    }

    char * const argv[] = { (char *const)exe,  "-d", ifs->ifname, 0 };
    char * const envp[] = { "PATH=/sbin:/usr/sbin:/bin:/usr/bin", 0 };

    pid_t pid = fork();
    if (pid == -1) {
        r = errno;
        printlog(LOG_ERR, "can't fork %s: %s", exe, strerror(r));
        error(1, r, "can't fork %s", exe);
    }


    if (pid == 0) { // child
        cleanup_state(ifs);
        reopen_std_fds();

        chdir("/tmp");
        execve(exe, argv, envp);
        printlog(LOG_ERR, "can't exec %s: %s", exe, strerror(errno));
    }

    debuglog("Started dhclient %s: PID %d", ifs->ifname, pid);

    // Parent
    Dhpid = pid;
}


static void
stop_dhcp()
{
    if (Dhpid > 0) {
        int r;

        kill(Dhpid, SIGINT);

        waitpid(Dhpid, &r, 0);
        debuglog("Stopped dhclient pid %d", Dhpid);

        Dhpid = -1;
    }
}


static void
check_dhcp(ifstate *s)
{
    if (Dhpid < 0) {
        printlog(LOG_ERR, "dhclient pid is missing?");
        return;
    }

    int r   = 0;
    pid_t k = waitpid(Dhpid, &r, WNOHANG);

    if (k < 0)  return;
    if (k == 0) return; // means Dhclient is alive and well

    if (k != Dhpid)
        printlog(LOG_ERR, "expected dhclient PID to be %d, saw %d; status %d",
                    Dhpid, k, r);

    if (WIFEXITED(r)) {
        int x = WEXITSTATUS(r);
        if (x != 0) {
            printlog(LOG_ERR, "dhclient exited abnormally with %d", x);
        }
    } else if (WIFSIGNALED(r)) {
        int sig = WTERMSIG(r);
        printlog(LOG_ERR, "dhclient caught signal %d and aborted", sig);
    }
}


// call after fork() before exec() -- in the child.
static void
cleanup_state(ifstate *ifs)
{
    closelog(); // syslog
    close(ifs->scanfd);
    db_close(ifs->db);
}

// redirect 0,1,2 to /dev/null
// XXX Errors here are fatal?
static void
reopen_std_fds()
{
    int rfd, wfd;

    if ((rfd = open("/dev/null", O_RDONLY)) < 0) {
        printlog(LOG_ERR, "can't re-open /dev/null for reading: %s", strerror(errno));
        return;
    }

    if ((wfd = open("/dev/null", O_WRONLY)) < 0) {
        printlog(LOG_ERR, "can't re-open /dev/null for writing: %s", strerror(errno));
        goto err1;
    }

    if (dup2(rfd, 0) < 0) {
        printlog(LOG_ERR, "can't dup2 stdin: %s", strerror(errno));
        goto err2;
    }

    if (dup2(wfd, 1) < 0) {
        printlog(LOG_ERR, "can't dup2 stdout: %s", strerror(errno));
        goto err2;
    }

    if (dup2(wfd, 2) < 0) {
        printlog(LOG_ERR, "can't dup2 stderr: %s", strerror(errno));
        goto err2;
    }


    // finally, close the new FDs and return
err2:
    close(wfd);
    
err1:
    close(rfd);
}
