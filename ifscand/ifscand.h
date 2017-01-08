/*  $OpenBSD: ifscand.h,v 1.330 2016/09/03 13:46:57 reyk Exp $
 *
 * ifscand.h - WiFi Interface scan/join daemon
 *
 * Copyright (c) 2016 Sudhi Herle <sudhi at herle.net>
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

#ifndef ___IFSCAND_H_9324569_1482274698__
#define ___IFSCAND_H_9324569_1482274698__ 1

    /* Provide C linkage for symbols declared here .. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <db.h>

#include "vect.h"
#include "fastbuf.h"
#include "common.h"

#define IFSCAND_INT_SCAN        60  /* Scan interval between successive scans */
#define IFSCAND_INT_RSSI        20  /* Scan interval between successive rssi measurements */
#define IFSCAND_INT_RSSI_FAST   10  /* Fast Scan interval between successive rssi measurements */


/*
 * Lowest weighted average of RSSI at which we will abandon current AP
 */
#define IFSCAND_RSSI_LOWEST     8


/* Handy formats for printing mac address */
#define sMAC(x)     x[0],x[1],x[2],x[3],x[4],x[5]
#define MACFMT      "%02x:%02x:%02x:%02x:%02x:%02x"


/*
 * AP and Preferences DB
 */
struct apdb
{
    DB *db;                // handle to open prefs DB

    char ifname[IFNAMSIZ];
};
typedef struct apdb apdb;



/* Sliding Winding of RSSI measurements. This is the window size.
 *
 * We calculate a time weighted average of these "N" measurements.
 */
#define RSSI_WS       4

struct rssi_avg
{
    uint32_t wr,        // next writable slot
             full;      // # of full slots. Clamped at RSSI_WS.
    int  val[RSSI_WS];
};
typedef struct rssi_avg rssi_avg;


static inline void
rssi_avg_init(rssi_avg *a)
{
    a->full = a->wr = 0;
}

static inline void
rssi_avg_add_sample(rssi_avg *a, int val)
{
    a->val[a->wr] = val;
    a->wr = (a->wr + 1) % RSSI_WS;
    if (a->full < RSSI_WS) a->full++;
}


/*
 * Calculate the average of the last RSSI_WS samples.
 *
 * This only returns a good result when we have a full window.
 */
static inline int
rssi_avg_value(rssi_avg *a)
{
    if (a->full < RSSI_WS) return -1;
    
    uint32_t r = a->wr;  // oldest sample
    int      s = 0,
             i = 0;

    do {
        i += 1;
        s += a->val[r];
        r  = (r + 1) % RSSI_WS;
    } while (r != a->wr);

    return s / RSSI_WS;
}




// Interface state
struct ifstate
{
    char ifname[IFNAMSIZ];

    /*
     * next 3 used by state machine.
     */
    int associated;         // flag: set if we have joined an AP
    apdata curap;           // currently associated AP
    rssi_avg  avg;          // Weighted average of RSSI_WS samples
    int      timeout;       // Timeout interval between "scans"

    int ipcfd;              // sock fd

    apdb    *db;            // persistent DB of settings and remembered APs

    /*
     * Next 3 used by ifstate_xxx routines.
     */
    int scanfd;             // scanning socket
    int down;               // set to true if we need to bring it down after scan
    struct ifreq ifr;       // interface state

    /* Allocate once and reuse everytime. */
    nodevect      nv;
};
typedef struct ifstate ifstate;



/*
 * Return normalized RSSI 
 */
#define __norm_rssi(a)      ((int)(((float)(a)->nr_rssi / (a)->nr_max_rssi) * 100))
#define RSSI(a)             (int)((a)->nr_max_rssi > 0 ? __norm_rssi(a) : (a)->nr_rssi)


/*
 * data structure for processing IPC commands and on-disk persistent
 * data.
 */
struct cmd_state
{
    int fd;     // I/O fd

    fast_buf in;  // input buffer
    fast_buf out; // output buffer

    struct sockaddr_un from;    // peer who sent the command

    // Pointer to global AP list and their relative priorities
    struct apdb *db;

    // Global interface scan state
    struct ifstate *ifs;
};
typedef struct cmd_state cmd_state;



static inline char*
copy_apname(char *dest, size_t n, struct ieee80211_nodereq *nr)
{
    size_t m = nr->nr_nwid_len > n ? n : nr->nr_nwid_len;

    memcpy(dest, nr->nr_nwid, m);
    dest[m] = 0;

    return dest;
}


/*
 * Predicate that returns true if 'exe' is a valid executable file.
 * And returns false otherwise.
 */
static inline int
valid_exe_p(const char *exe)
{
    struct stat st;

    if (0 != stat(exe, &st)) return -errno;

    return S_ISREG(st.st_mode) && (st.st_mode & 0110) ? 1 : 0;
}


/*
 * API to manage persistent info
 */
void db_init(apdb *, const char *iface);

void db_close(apdb *);

/*
 * Remember our preferred order of APs. These are always preferred
 * over other APs we have remembered.
 */
void db_set_ap_order(apdb *, char **args, int argc);

/*
 * Set global "randmac" property.
 */
void db_set_randmac(apdb *, int val);

/*
 * Set global scan interval for AP scans.
 */
void db_set_scanint(apdb *, int val);

/*
 * Store AP specific info of AP 'd' into our DB.
 */
void db_set_apdata(apdb *db, const apdata *d);

/*
 * Return the global randmac property.
 */
int db_get_randmac(apdb *);

/*
 * Return the global scan-interval property.
 */
int db_get_scanint(apdb *);

/*
 * Return a list of APs we have remembered.
 */
int db_get_all_ap(apdb *, apvect *av);

/*
 * Return our preferred AP order.
 */
strvect db_get_ap_order(apdb *);


/*
 * Delete an AP from our remembered list.
 */
int db_del_ap(apdb *db, const char *ap);


/*
 * Given a list of scanned AP names, remove ones that we haven't
 * remembered and return the result in 'av'.
 */
void db_filter_ap(apdb *db, apvect *av, nodevect *nv);


/* Describe ap info in text form that can be parsed back */
size_t db_ap_sprintf(char *buf, size_t bsiz, apdata *a);

extern int wifi_scan(ifstate *ifs);

extern int disconnect_ap(ifstate *s, apdata *ap);

/*
 * Initialize logging to syslog.
 */
void initlog(const char *ifname);

// Print a log to syslog with the right prefix, prio etc.
extern void printlog(int lev, const char *fmt, ...);

// Print a debug log only if in Debug mode.
extern void debuglog(const char *fmt, ...);

// set FD_CLOEXEC flag on 'fd'
// Return 0 on success; -errno on failure
extern int fd_set_cloexec(int fd);


extern int  ifstate_init(ifstate *ifs, const char* ifname);
extern void ifstate_close(ifstate *ifs);
ssize_t ifstate_sprintf_node(char * buf, size_t  bsiz, struct ieee80211_nodereq *nr);

/*
 * Set interface state to up/down.
 *
 * Return:
 *      0   on success
 *      -errno on failure
 */
int ifstate_set(ifstate *, int up);


int ifstate_config(ifstate *, const apdata *);
int ifstate_unconfig(ifstate *);



/*
 * Get RSSI of interface/apname
 *
 * Return:
 *    0 if can't find AP with the associated MAC
 *    > 0 rssi as a percentage
 *    < 0  -errno on error
 */
int ifstate_get_rssi(ifstate *ifs, const char* apname, const uint8_t *mac);

void ifstate_dump(ifstate *, FILE *fp);
int  ifstate_get_rssi(ifstate*, const char *ap, const uint8_t *mac);

// Returns:
//   < 0 -errno on error
//   >= 0 # of nodes visible
extern int ifstate_scan(ifstate *ifs);


/*
 * parse an IPC command or a disk file - both of which are
 * represented by 'fp'.
 *
 * Returns:
 *    0 on EOF
 *    > on suceess
 *    < 0  -errno on error
 */
extern int cmd_process(cmd_state *s);

/*
 * Global vars
 */
extern int Debug;       // set if running in debug mode
extern int Foreground;  // set if NOT running as daemon
extern int Network_cfg; // set if ifscand does network layer config

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! ___IFSCAND_H_9324569_1482274698__ */

/* EOF */
