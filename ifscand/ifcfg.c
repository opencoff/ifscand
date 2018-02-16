/* vim: expandtab:tw=68:ts=4:sw=4:
 *
 * ifcfg.c - Interface configuration
 *
 * Some parts liberally borrowed from OpenBSD ifconfig(8).
 *
 * Cleanups and other bits: Sudhi Herle <sw at herle.net>
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
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <errno.h>
#include <util.h>
#include <unistd.h>
#include <sys/param.h>  // isset()

#include "ifscand.h"
#include "utils.h"

static int setnwid(ifstate *ifs, const char *nwid);
static int setwepkey(ifstate *ifs, const char *inval, int nokey);
static int setwpakey(ifstate *ifs, const char *key, const char *apname, int nokey);
static int setmacaddr(ifstate *ifs, const uint8_t *mac, int rand);
static int splitstr(char **v, int nv, char *str, int tok);

static int wait_config(ifstate *ifs, apdata *z);
static int wait_up(ifstate *);
static int wait_media(ifstate *);
static int wait_bssid(ifstate *, uint8_t *bssid);
static int get_rssi(ifstate *s, const char *apname, const uint8_t *mac, struct ieee80211_nodereq *nr);


/*
 * Initialize a wifi interface for scanning. Bring it up as needed.
 */
int
ifstate_init(ifstate *ifs, const char* ifname)
{
    struct ifreq *ifr = &ifs->ifr;
    uint32_t flags = 0;
    int r;

    memset(ifs, 0, sizeof *ifs);

    VECT_INIT(&ifs->nv, 16);

    strlcpy(ifr->ifr_name, ifname, sizeof ifr->ifr_name);
    strlcpy(ifs->ifname,   ifname, sizeof ifs->ifname);

    ifs->scanfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ifs->scanfd < 0) return -errno;

    if ((r = fd_set_cloexec(ifs->scanfd)) < 0) return r;

    if (ioctl(ifs->scanfd, SIOCGIFFLAGS, (caddr_t)ifr) < 0) return -errno;

    flags = ifr->ifr_flags & 0xffff;

    // Bring up the interface if needed
    if ((flags & IFF_UP) == 0) {
        ifstate_set(ifs, 1);
        ifs->down = 1;
    }

    /* Make sure we have permission to scan */
    if (ioctl(ifs->scanfd, SIOCS80211SCAN, (caddr_t)ifr) != 0) return -errno;

    return 0;
}


/*
 * Set interface state to up/down.
 *
 * Return:
 *      0   on success
 *      -errno on failure
 */
int
ifstate_set(ifstate *ifs, int up)
{
    struct ifreq nn;

    memset(&nn, 0, sizeof nn);
    strlcpy(nn.ifr_name, ifs->ifname, sizeof nn.ifr_name);

    if (ioctl(ifs->scanfd, SIOCGIFFLAGS, (caddr_t)&nn) < 0) return -errno;

    if (up)
        nn.ifr_flags |= IFF_UP;
    else
        nn.ifr_flags &= ~IFF_UP;

    if (ioctl(ifs->scanfd, SIOCSIFFLAGS, (caddr_t)&nn) < 0) return -errno;
    return 0;
}


/*
 * Close an interface previously opened for scanning. Restore the
 * interface state if we brought it up.
 */
void
ifstate_close(ifstate *ifs)
{
    if (ifs->down) ifstate_set(ifs, 0);

    close(ifs->scanfd);
    VECT_FINI(&ifs->nv);
    memset(ifs, 0, sizeof *ifs);
}


/*
 * Returns an integer less than, equal to, or greater than zero if nr1's
 * RSSI is respectively greater than, equal to, or less than nr2's RSSI.
 */
static int
rssicmp(const void *nr1, const void *nr2)
{
    const struct ieee80211_nodereq *x = nr1, *y = nr2;
    int rx = RSSI(x),
        ry = RSSI(y);

    return ry < rx ? -1 : ry > rx;
}



/*
 * Scan the given interface and populate ifs->nv
 *
 * Returns:
 *   < 0 -errno on error
 *   >= 0 # of nodes scanned
 */
int
ifstate_scan(ifstate *ifs)
{
    int i;
    struct ieee80211_nodereq_all na;
    struct ieee80211_nodereq nr[512];
    nodevect *nv = &ifs->nv;

    VECT_RESET(nv);
    
    memset(&na, 0, sizeof na);
    memset(nr,  0, sizeof nr);

    na.na_node = nr;
    na.na_size = sizeof nr;
    strlcpy(na.na_ifname, ifs->ifname, sizeof na.na_ifname);

    if (ioctl(ifs->scanfd, SIOCG80211ALLNODES, &na) != 0) return -errno;

    if (na.na_nodes == 0) return 0;

    VECT_RESERVE(nv, na.na_nodes);
    for (i = 0; i < na.na_nodes; i++) {
        VECT_APPEND(nv, nr[i]);
    }

    VECT_SORT(nv, rssicmp);
    return na.na_nodes;
}


/*
 * Get RSSI of interface/apname
 *
 * Return:
 *    0 if can't find AP with the associated MAC
 *    > 0 rssi
 *    < 0  -errno on error
 */
int
ifstate_get_rssi(ifstate *ifs, const char* apname, const uint8_t *mac)
{
    struct ieee80211_nodereq nr;
    int r = get_rssi(ifs, apname, mac, &nr);

    return r < 0 ? r : RSSI(&nr);
}


/*
 * Printable form of scanned result
 */
ssize_t
ifstate_sprintf_node(char * buf, size_t  bsiz, struct ieee80211_nodereq *nr)
{
    size_t orig = bsiz;
    int i;

#define PR(a, ...)   do { \
                        ssize_t m = snprintf(buf, bsiz, a, ##__VA_ARGS__); \
                        buf  += m; \
                        bsiz -= m; \
                    } while (0)

    if (nr->nr_flags & IEEE80211_NODEREQ_AP ||
        nr->nr_capinfo & IEEE80211_CAPINFO_IBSS) {

        uint8_t *mac = nr->nr_bssid;
        char zz[IEEE80211_NWID_LEN+1];

        copy_apname(zz, IEEE80211_NWID_LEN, nr);

        PR("nwid \"%s\" chan %u bssid " MACFMT, zz, nr->nr_channel, sMAC(mac));
    }

    if ((nr->nr_flags & IEEE80211_NODEREQ_AP) == 0) {
        uint8_t *mac = nr->nr_macaddr;
        PR(" lladdr " MACFMT, sMAC(mac));
    }

    if (nr->nr_max_rssi)
        PR(" %u%% ", IEEE80211_NODEREQ_RSSI(nr));
    else
        PR(" %ddBm ", nr->nr_rssi);

    if (nr->nr_pwrsave) PR(" powersave");

    if ((nr->nr_flags & (IEEE80211_NODEREQ_AP)) == 0) {
#if 0
        if (nr->nr_flags & IEEE80211_NODEREQ_HT) {
            PR("HT-MCS%d ", nr->nr_txmcs);
        } else
#endif
            if (nr->nr_nrates) {
                PR(" %uM ",
                        (nr->nr_rates[nr->nr_txrate] & IEEE80211_RATE_VAL) / 2);
            }
    } else if (nr->nr_max_rxrate) {
        PR(" %uM HT ", nr->nr_max_rxrate);
    } else if (nr->nr_rxmcs[0] != 0) {
        for (i = IEEE80211_HT_NUM_MCS - 1; i >= 0; i--) {
            if (isset(nr->nr_rxmcs, i))
                break;
        }
        PR(" HT-MCS%d ", i);
    } else if (nr->nr_nrates) {
        PR(" %uM ", (nr->nr_rates[nr->nr_nrates - 1] & IEEE80211_RATE_VAL) / 2);
    }

    /* ESS is the default, skip it */
    nr->nr_capinfo &= ~IEEE80211_CAPINFO_ESS;
    if (nr->nr_capinfo) {
        //printb_status(nr->nr_capinfo, IEEE80211_CAPINFO_BITS);
        if (nr->nr_capinfo & IEEE80211_CAPINFO_PRIVACY) {
            if (nr->nr_rsnciphers & IEEE80211_WPA_CIPHER_CCMP)
                PR(" wpa2");
            else if (nr->nr_rsnciphers & IEEE80211_WPA_CIPHER_TKIP)
                PR(" wpa1");
            else
                PR(" wep");

            if (nr->nr_rsnakms & IEEE80211_WPA_AKM_8021X ||
                nr->nr_rsnakms & IEEE80211_WPA_AKM_SHA256_8021X)
                PR(",802.1x");
        }
    }
#if 0
    if ((nr->nr_flags & IEEE80211_NODEREQ_AP) == 0)
        printb_status(IEEE80211_NODEREQ_STATE(nr->nr_state),
            IEEE80211_NODEREQ_STATE_BITS);
#endif

    return orig - bsiz;
}


/*
 * Configure the wifi interface:
 *
 * - set lladdr
 * - set nwid
 * - set nwkey
 * - set status "up"
 *
 * If 'newap' is non-nil, set it to the currently joined AP info
 * (after configuration).
 *
 * Return:
 *  0      on success
 *  -errno on failure
 */
int
ifstate_config(ifstate *ifs, const apdata *ap, apdata *newap)
{
    struct ieee80211_nodereq nr;
    apdata z;
    int r = 1;

    if (ap->flags & AP_MYMAC) {
        int rmac = 0;
        if (ap->flags & AP_RANDMAC) rmac = 1;

        r = setmacaddr(ifs, ap->mymac, rmac);

    } else if (db_get_randmac(ifs->db)) {
        r = setmacaddr(ifs, 0, 1);
    }

    if (r < 0) return r;

    r = setnwid(ifs, ap->apname);
    if (r < 0) return r;

    if (ap->flags & AP_WEPKEY)
        r = setwepkey(ifs, ap->key, 0);
    else if (ap->flags & AP_WPAKEY)
        r = setwpakey(ifs, ap->key, ap->apname, 0);

    if (r < 0) return r;

    r = ifstate_set(ifs, 1);
    if (r < 0) return r;

    memset(&z, 0, sizeof z);

    /*
     * Finally, retrieve the current state of the interface along
     * with the associated BSSID etc.
     *
     * In many cases, a single ESSID is used for 2.4G and 5G bands;
     * and OpenBSD doesn't give an easy way to tell it which BSSID
     * we want to associate.. the current method requires
     * complicated media+mediaopt settings to pick a preferred band.
     *
     * We wait until the interface comes up and then fetch the
     * relevant data.
     */
    if ((r = wait_config(ifs, &z)) < 0) return r;


    /*
     * Finally, fetch the latest RSSI values.
     */
    if ((r = get_rssi(ifs, ifs->ifname, z.nr_bssid, &nr)) < 0) return r;

    z.nr_rssi     = nr.nr_rssi;
    z.nr_max_rssi = nr.nr_max_rssi;

    if (newap) *newap = z;
    return 0;
}


/*
 * Unconfigure an interface.
 */
int
ifstate_unconfig(ifstate *ifs)
{
    setnwid(ifs, 0);
    setwepkey(ifs, "", 0);
    setwpakey(ifs, "", "", 1);
    return 0;
}




/*
 * Return 0 on success, -errno on failure
 */
static int
setnwid(ifstate *ifs, const char *id)
{
    struct ieee80211_nwid nwid;
    
    if (!id) {
        memset(&nwid, 0, sizeof nwid);
    } else {
        strlcpy(nwid.i_nwid, id, sizeof nwid.i_nwid);
        nwid.i_len = strlen(nwid.i_nwid);
    }

    struct ifreq ifr;

    strlcpy(ifr.ifr_name, ifs->ifname, sizeof ifr.ifr_name);
    ifr.ifr_data     = (caddr_t)&nwid;

    if (ioctl(ifs->scanfd, SIOCS80211NWID, (caddr_t)&ifr) < 0) return -errno;

    return 0;
}


/*
 * Return 0 on success, -errno on failure
 */
static int
setwepkey(ifstate *ifs, const char *inval, int nokey)
{
    int i, len;
    struct ieee80211_nwkey nwkey;
    struct foo {
        uint8_t keybuf[16];
    } wepk[IEEE80211_WEP_NKID];
    char keys[256];
    char *val = keys;

    strlcpy(keys, inval, sizeof keys);

    bzero(&nwkey, sizeof nwkey);
    bzero(&wepk, sizeof wepk);

    nwkey.i_wepon  = IEEE80211_NWKEY_WEP;
    nwkey.i_defkid = 1;
    if (nokey) {
        /* disable WEP encryption */
        nwkey.i_wepon = 0;
        i = 0;
    } else {
        if (isdigit((unsigned char)val[0]) && val[1] == ':') {
            /* specifying a full set of four keys */
            nwkey.i_defkid = val[0] - '0';
            val += 2;
            char *keyv[4];
            len = splitstr(keyv, 4, val, ',');
            if (len != 4) return -EINVAL;
            for (i = 0; i < IEEE80211_WEP_NKID; i++) {
                len = str2hex(wepk[i].keybuf, sizeof wepk[i].keybuf, keyv[i]);
                if (len <= 0) return -EINVAL;

                nwkey.i_key[i].i_keylen = len;
                nwkey.i_key[i].i_keydat = wepk[i].keybuf;
            }
        } else {
            /*
             * length of each key must be either a 5
             * character ASCII string or 10 hex digits for
             * 40 bit encryption, or 13 character ASCII
             * string or 26 hex digits for 128 bit
             * encryption.
             */
            size_t vlen = strlen(val);
            int    hex  = 0;
            switch (vlen) {
                case 5: case 13: /* ASCII keys */
                    break;

                case 12:
                    val += 2;
                case 10:
                    hex = 5;
                    break;

                case 28:
                    val += 2;
                case 26:
                    hex  = 13;
                    break;

                default:
                    return -EINVAL;
            }
            if (hex) {
                len = str2hex(wepk[0].keybuf, sizeof wepk[0].keybuf, val);
                if (len != hex) return -EINVAL;
            } else {
                strlcpy(wepk[0].keybuf, val, sizeof wepk[0].keybuf);
                len = vlen;
            }

            nwkey.i_key[0].i_keylen = len;
            nwkey.i_key[0].i_keydat = wepk[0].keybuf;
            i = 1;
        }
    }
    strlcpy(nwkey.i_name, ifs->ifname, sizeof(nwkey.i_name));
    if (ioctl(ifs->scanfd, SIOCS80211NWKEY, (caddr_t)&nwkey) == -1) return -errno;

    return 0;
}


/*
 * Return 0 on success, -errno on failure
 */
static int
setwpakey(ifstate *ifs, const char *val, const char *apname, int nokey)
{
    struct ieee80211_wpaparams wpa;
    struct ieee80211_wpapsk psk;
    int passlen;
    int r;

    memset(&psk, 0, sizeof(psk));
    if (!nokey) {
        if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
            r = str2hex(psk.i_psk, sizeof psk.i_psk, val+2);
            if (r < 0) return r;
            if (r != sizeof psk.i_psk) return -EINVAL;
        } else {
            /* Parse a WPA passphrase */ 
            passlen = strlen(val);
            if (passlen < 8 || passlen > 63) return -E2BIG;
            if (pkcs5_pbkdf2(val, passlen, apname, strlen(apname),
                        psk.i_psk, sizeof(psk.i_psk), 4096) != 0)
                return -EINVAL; // XXX ??
        }
        psk.i_enabled = 1;
    } else
        psk.i_enabled = 0;

    strlcpy(psk.i_name, ifs->ifname, sizeof(psk.i_name));
    if (ioctl(ifs->scanfd, SIOCS80211WPAPSK, (caddr_t)&psk) < 0) return -errno;

    /* And ... automatically enable or disable WPA */
    memset(&wpa, 0, sizeof(wpa));
    strlcpy(wpa.i_name, ifs->ifname, sizeof(wpa.i_name));
    if (ioctl(ifs->scanfd, SIOCG80211WPAPARMS, (caddr_t)&wpa) < 0) return -errno;

    wpa.i_enabled = psk.i_enabled;
    if (ioctl(ifs->scanfd, SIOCS80211WPAPARMS, (caddr_t)&wpa) < 0) return -errno;
    return 0;
}



/*
 * Return 0 on success, -errno on failure
 */
static int
setmacaddr(ifstate *ifs, const uint8_t *mac, int randmac)
{
    /* Xen, VMware and Parallels OUIs */
    static const uint8_t prefix[][3] = {
        {0x00, 0x05, 0x69},
        {0x00, 0x0c, 0x29},
        {0x00, 0x1c, 0x14},
        {0x00, 0x50, 0x56},
        {0x00, 0x1c, 0x42},
        {0x00, 0x16, 0x3e}
    };
#define NPREF   ((sizeof prefix) / (sizeof prefix[0]))

    struct ifreq z;
    struct ifreq *ifr = &z;
    uint8_t *addr     = ifr->ifr_addr.sa_data;

    strlcpy(ifr->ifr_name, ifs->ifname, sizeof ifr->ifr_name);
    ifr->ifr_addr.sa_len    = 6;
    ifr->ifr_addr.sa_family = AF_LINK;

    if (randmac) {
        uint32_t i = arc4random_uniform(NPREF);
        memcpy(addr, prefix[i], 3);
        arc4random_buf(addr+3, 3);
    } else if (mac) {
        memcpy(addr, mac, 6);
    }

    if (ioctl(ifs->scanfd, SIOCSIFLLADDR, (caddr_t)ifr) < 0)
        return -errno;

    return 0;
}

/*
 * Wait for interface to be up and running.
 * - wait for chan, media to be available
 * - then fetch bssid (SIOCG90211BSSID)
 *
 * Return:
 *      0 on success
 *      -errno on failure
 */
static int
wait_config(ifstate *ifs, apdata *z)
{
    struct ieee80211_nwid n;
    struct ifreq ii;
    int r;

    if ((r = wait_media(ifs)) < 0)   return r;

    memset(&n, 0, sizeof n);
    memset(&ii, 0, sizeof ii);

    strlcpy(ii.ifr_name, ifs->ifname, sizeof ii.ifr_name);
    ii.ifr_data = (caddr_t)&n;

    if (ioctl(ifs->scanfd, SIOCG80211NWID, &ii) < 0) return -errno;
    strlcpy(z->apname, n.i_nwid, sizeof z->apname);

    if ((r = wait_bssid(ifs, z->nr_bssid)) < 0) return r;

    if ((r = wait_up(ifs))    < 0)   return r;

    printlog(LOG_INFO, "Connected to AP \"%s\" with BSSID " MACFMT, z->apname, sMAC(z->nr_bssid));
    return 0;
}


#define IFUP_WAIT_MS    100
#define MEDIA_WAIT_MS   500
#define BSSID_WAIT_MS   150


/*
 * Wait until interface is truly up.
 *
 * Return:
 *      0 on success
 *      -errno on failure
 */
static int
wait_up(ifstate *ifs)
{
    struct ifreq z ;
    int tries = 0;

    memset(&z, 0, sizeof z);
    strlcpy(z.ifr_name, ifs->ifname, sizeof z.ifr_name);

    ifstate_set(ifs, 1);

    for (tries = 0; tries < 5; tries++) {
        if (ioctl(ifs->scanfd, SIOCGIFFLAGS, &z) < 0) return -errno;

        if ( (IFF_UP|IFF_RUNNING) == ((IFF_UP|IFF_RUNNING) & z.ifr_flags)) {
            debuglog("interface is up after %u ms", tries * IFUP_WAIT_MS);
            return 1;
        }

        // wait for .5 seconds before trying again
        usleep(IFUP_WAIT_MS * 1000);
    }
    return -ENETDOWN;
}


/*
 * Wait for media to be configured.
 *
 * Return:
 *      0 on success
 *      -errno on failure
 */
static int
wait_media(ifstate *ifs)
{
    struct ifmediareq mr;
    int tries;

    memset(&mr, 0, sizeof mr);
    strlcpy(mr.ifm_name, ifs->ifname, sizeof mr.ifm_name);

    for (tries = 0; tries < 5; tries++) {
        if (ioctl(ifs->scanfd, SIOCGIFMEDIA, &mr) < 0) return -errno;
        if (mr.ifm_count > 0) {
            debuglog("media configured after %u ms", tries * MEDIA_WAIT_MS);
            return 1;
        }
        usleep(MEDIA_WAIT_MS * 1000);
    }
    return -ENETDOWN;
}


/*
 * Wait for BSSID to be available
 *
 * Return:
 *      0 on success
 *      -errno on failure
 */
static int
wait_bssid(ifstate *ifs, uint8_t *bssid)
{
    static const uint8_t Zeroes[] = { 0,0,0, 0,0,0 };
    struct ieee80211_bssid b;
    int tries;

    memset(&b, 0, sizeof b);
    strlcpy(b.i_name, ifs->ifname, sizeof b.i_name);

    for (tries = 0; tries < 50; tries++) {
        if (ioctl(ifs->scanfd, SIOCG80211BSSID, &b) < 0) return -errno;

        if (0 != memcmp(b.i_bssid, Zeroes, 6)) {
            debuglog("bssid is avail after %u ms", tries * BSSID_WAIT_MS);

            memcpy(bssid, b.i_bssid, 6);
            return 1;
        }

        // wait for .5 seconds before trying again
        usleep(BSSID_WAIT_MS * 1000);
    }
    return -ENETDOWN;
}


/*
 * Fill RSSI info in 'nr'.
 *
 * Return:
 *   0 on success
 *   -errno on failure
 */
static int
get_rssi(ifstate *ifs, const char *apname, const uint8_t *mac, struct ieee80211_nodereq *nr)
{
    size_t aplen = strlen(apname);

    memset(nr, 0, sizeof *nr);

    strlcpy(nr->nr_ifname, ifs->ifname, sizeof nr->nr_ifname);
    strlcpy(nr->nr_nwid,   apname,      sizeof nr->nr_nwid);
    memcpy(nr->nr_macaddr, mac,         sizeof nr->nr_macaddr);

    nr->nr_nwid_len = aplen;

    if (ioctl(ifs->scanfd, SIOCG80211NODE, nr) != 0) return -errno;

    return 0;
}


static int
splitstr(char **v, int nv, char *str, int tok)
{
    int i = 0;
    int c;
    char *p = 0;
    for (p = str; (c = *str); str++) {
        if (c == tok) {
            if (i >= nv) return -ENOMEM;

            *str   = 0;
            v[i++] = p;
            p      = str+1;
        }
    }
    if (*p) {
        if (i >= nv) return -ENOMEM;
        v[i++] = p;
    }

    return i;
}

/* EOF */
