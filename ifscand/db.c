/* $OpenBSD: ifscand.c,v 1.330 2016/09/03 13:46:57 reyk Exp $
 *
 * db.c - AP list and preferences management
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
 *
 *
 * Notes
 * =====
 *
 * * Preferences for _ALL_ interfaces (and all daemon instances) are
 *   in a single NDBM file (/etc/ifscand/prefs.db)
 *
 * * Locking and multi-process access is mediated by NDBM.
 *
 * * All preference keys are stored with prefix "prefs."
 *
 * * All AP name keys are stored with prefix "ap."
 *
 * * Global preferences are per-interface. In such cases, the NDBM
 *   key is of the form "prefs.$key.$iface" e.g., "prefs.randmac.iwm0".
 *
 * * AP Names and properties are global for all interfaces. To
 *   minimize cache-coherency issues, we always go back to NDBM to
 *   filter or arrange entries in the way we want.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <db.h>
#include <libgen.h>

#include "vect.h"
#include "utils.h"
#include "ifscand.h"

static void make_dir(const char *fn);


#define _u32(x) ((uint32_t)x)

static inline uint8_t*
enc_LE_u32(uint8_t* buf, uint32_t v) {
    *buf++ = 0xff & v;
    *buf++ = 0xff & (v >> 8);
    *buf++ = 0xff & (v >> 16);
    *buf++ = 0xff & (v >> 24);
    return buf;
}

static inline uint32_t
dec_LE_u32(const uint8_t * p) {
    return         p[0]
           | (_u32(p[1]) << 8)
           | (_u32(p[2]) << 16)
           | (_u32(p[3]) << 24);
}

void
db_init(apdb *db, const char *iface)
{
    char fn[PATH_MAX];
    snprintf(fn, sizeof fn, "%s.db", IFSCAND_PREFS);

    make_dir(fn);

    DB *d = dbopen(fn, O_RDWR|O_CREAT|O_SYNC|O_SHLOCK, 0600, DB_BTREE, 0);
    if (!d) error(1, errno, "can't open %s", fn);

    db->db = d;
    strlcpy(db->ifname, iface, sizeof db->ifname);

    // get and set default values
    unsigned int v = 0;
    if (!db_get_uint(db, "scan-int", &v))       db_set_uint(db, "scan-int",      IFSCAND_INT_SCAN);
    if (!db_get_uint(db, "rssi-scan-int", &v))  db_set_uint(db, "rssi-scan-int", IFSCAND_INT_RSSI_FAST);
}


void
db_close(apdb *db)
{
    db->db->close(db->db);
}

static void
db_put(apdb *db, const char * rkey, DBT *val)
{
    char key[128];
    int r;

    snprintf(key, sizeof key, "prefs.%s.%s", rkey, db->ifname);

    size_t n = strlen(key);
    DBT k    = { .data = key, .size = n };

    r = db->db->put(db->db, &k, val, 0);
    if (r != 0) {
        printlog(LOG_ERR, "can't store %s: %s", key, strerror(errno));
        error(1, errno, "fatal: DB store of %s failed", key);
    }

    db->db->sync(db->db, 0);
}


static DBT
db_get(apdb *db, const char* rkey)
{
    char key[128];

    snprintf(key, sizeof key, "prefs.%s.%s", rkey, db->ifname);

    DBT k = { .data = key, .size = strlen(key) };
    DBT v = { 0, 0 };

    db->db->get(db->db, &k, &v, 0);
    return v;
}


static int
db_get_int(apdb *db, const char *rkey)
{
    DBT d = db_get(db, rkey);

    // XXX Yes. Ugly.
    if (d.data) {
        int v;
        memcpy(&v, d.data, sizeof(int));
        return v;
    }
    return 0;
}

static int
db_get_strvect(apdb *db, strvect *sv, const char *rkey)
{
    DBT d = db_get(db, rkey);
    if (!d.data) return 0;

    char *p  = d.data;
    size_t n = d.size;

    VECT_RESET(sv);
    VECT_RESERVE(sv, 8);

    while (n > 0) {
        size_t m = 1 +strlen(p);

        VECT_APPEND(sv, p);
        p += m;
        n -= m;
    }

    return VECT_SIZE(sv);
}



/*
 * Serialize apdata into durable storage.
 */
static ssize_t
pack_apdata(uint8_t *buf, size_t bsiz, const apdata *d)
{
    assert(bsiz >= sizeof *d);

    memcpy(buf, d, sizeof *d);
    return sizeof *d;
}


static int
unpack_apdata(apdata *d, uint8_t *buf, size_t bsiz)
{
    assert(bsiz >= sizeof *d);
    memcpy(d, buf, sizeof *d);
    return sizeof *d;
}

/*
 * Store a given AP's info in the DB.
 */
void
db_set_apdata(apdb *db, const apdata *d)
{
    uint8_t buf[1024];
    char key[256];

    snprintf(key, sizeof key, "ap.%s", d->apname);

    DBT k = { .data = key, .size = strlen(key) };
    DBT v = { .data = buf };

    v.size = pack_apdata(buf, sizeof buf, d);

    int r = db->db->put(db->db, &k, &v, 0);
    if (r != 0) {
        printlog(LOG_ERR, "can't store %s: %s", key, strerror(errno));
        error(1, errno, "fatal: DB store of %s failed", key);
    }
    db->db->sync(db->db, 0);
}


/*
 * Given a list of scanned AP names, remove ones that we haven't
 * remembered and return the result in 'av'.
 */
void
db_filter_ap(apdb *db, apvect *av, nodevect *nv)
{
    char nw[IEEE80211_NWID_LEN+1];
    char key[256];
    struct ieee80211_nodereq *nr;
    DBT k = { .data = key };

    strvect sv; // Our ordered list of AP names (if any)

    VECT_RESET(av);

    VECT_INIT(&sv, 8);
    VECT_RESERVE(av, 8);

    db_get_strvect(db, &sv, "aporder");

    VECT_FOR_EACH(nv, nr) {
        DBT v = { 0, 0};
        apdata d;

        copy_apname(nw, IEEE80211_NWID_LEN, nr);

        snprintf(key, sizeof key, "ap.%s", nw);
        k.size = strlen(key);
        if (0 != db->db->get(db->db, &k, &v, 0)) continue;

        unpack_apdata(&d, v.data, v.size);

        /*
         * If the MAC address is pinned, make sure what we are
         * seeing is the same as what we expect.
         */
        if (d.flags & AP_BSSID) {
            if (0 != memcmp(d.apmac, nr->nr_bssid, 6)) {
                char exp[16];
                char saw[16];
                snprintf(exp, sizeof exp, MACFMT, sMAC(d.apmac));
                snprintf(saw, sizeof saw, MACFMT, sMAC(nr->nr_bssid));

                printlog(LOG_WARNING, "AP %s: MAC mismatch; exp %s, saw %s",
                        d.apname, exp, saw);

                continue;
            }
        }

        debuglog("scan: shortlisted known AP %s [" MACFMT "]..\n", d.apname, sMAC(nr->nr_bssid));
        memcpy(d.nr_bssid, nr->nr_bssid, 6);
        d.nr_rssi     = nr->nr_rssi;
        d.nr_max_rssi = nr->nr_max_rssi;
        VECT_APPEND(av, d);       
    }

    if (VECT_SIZE(&sv) == 0) goto end;


    debuglog("scan: filtering based on ap-order ..");

    /*
     * Now, we put the APs we want at the top.
     */
    apvect fv;
    char **p;
    apdata *d;
    bitvect bs;
    size_t i;

    bv_init_alloca(&bs, VECT_SIZE(av));

    VECT_INIT(&fv, 8);
    VECT_FOR_EACH(&sv, p) {
        char *s = *p;
        VECT_FOR_EACHi(av, i, d) {
            if (0 != strcmp(d->apname, s)) continue;

            bv_set(&bs, i);
            VECT_APPEND(&fv, *d);
            debuglog("scan: selecting preferred AP %s..\n", d->apname);
        }
    }

    // add back the ones we haven't ordered.
    VECT_FOR_EACHi(av, i, d) {
        if (!bv_isset(&bs, i)) {
            VECT_APPEND(&fv, *d);
            debuglog("scan: adding remaining AP %s..\n", d->apname);
        }
    }

    VECT_SWAP(&fv, av);
    VECT_FINI(&fv);

    debuglog("scan: Final %d APs in candidate set; top %s\n", VECT_SIZE(av),
                VECT_SIZE(av) > 0 ? VECT_ELEM(av, 0).apname : "");

end:
    VECT_FINI(&sv);
}



void
db_set_ap_order(apdb *db, char **args, int argc)
{
    int i;
    char buf[128 * argc];
    char *p  = buf;
    size_t n = sizeof buf;

    /*
     * Create a serialized version for storing in the DB.
     * We separate each AP_name by '\0'
     */

    for (i = 0; i < argc; i++) {
        char *s  = args[i];

        snprintf(p, n, "%s", s);

        ssize_t m = 1 + strlen(p);
        p += m;
        n -= m;
    }

    DBT d = { .data = buf, .size = (sizeof buf)-n };

    db_put(db, "aporder", &d);
}


strvect
db_get_ap_order(apdb *db)
{
    strvect v;

    VECT_INIT(&v, 8);

    db_get_strvect(db, &v, "aporder");
    return v;
}


int
db_get_all_ap(apdb *db, apvect *av)
{
    DBT k;
    DBT v;
    DB *d = db->db;
    int r;

    VECT_RESET(av);
    VECT_RESERVE(av, 8);


    for (r = d->seq(d, &k, &v, R_FIRST); r == 0; r = d->seq(d, &k, &v, R_NEXT)) {
        assert(k.data);
        if (0 != memcmp("ap.", k.data, 3)) continue;

        if (v.data) {
            apdata a;

            unpack_apdata(&a, v.data, v.size);
            VECT_APPEND(av, a);       
        }
    }

    return VECT_SIZE(av);
}


int
db_del_ap(apdb *db, const char *ap)
{
    char key[256];
    snprintf(key, sizeof key, "ap.%s", ap);

    DBT k = { .data = key, .size = strlen(key) };

    db->db->del(db->db, &k, 0);
    db->db->sync(db->db, 0);
    return 1;
}



void
db_set_randmac(apdb *db, int val)
{
    DBT d = { .data = &val, .size = sizeof val };

    db_put(db, "randmac", &d);
}



int
db_get_randmac(apdb *db)
{
    return db_get_int(db, "randmac");
}


/*
 * Get a uint preference named 'rkey'.
 *
 * Return True on suceess, False on failure.
 */
int
db_get_uint(apdb *db, const char* rkey, unsigned int *p_res)
{
    assert(p_res);
    DBT d = db_get(db, rkey);

    if (d.data) {
        memcpy(p_res, d.data, sizeof(int));
        return 1;
    }

    return 0;
}

// Update an integer preference
void
db_set_uint(apdb *db, const char* rkey, unsigned int val)
{
    DBT d = { .data = &val, .size = sizeof val };
    db_put(db, rkey, &d);
}



static ssize_t
fmt_ipmask(char *buf, size_t bsiz, char *fmt, int af, void *addr, void *mask)
{
    char a[128];
    char m[128];

    inet_ntop(af, addr, a, sizeof a);
    inet_ntop(af, mask, m, sizeof m);

    snprintf(buf, bsiz, fmt, a, m);
    return strlen(buf);
}

static ssize_t
fmt_ip(char *buf, size_t bsiz, char *fmt, int af, void *addr)
{
    char a[128];

    inet_ntop(af, addr, a, sizeof a);

    snprintf(buf, bsiz, fmt, a);
    return strlen(buf);
}


size_t
db_ap_sprintf(char *buf, size_t bsiz, apdata *a)
{
    size_t orig = bsiz;
    snprintf(buf, bsiz, "nwid \"%s\"", a->apname);

    ssize_t n = strlen(buf);
    buf  += n;
    bsiz -= n;

    if (a->flags & AP_MYMAC) {
        if (a->flags & AP_RANDMAC) {
            snprintf(buf, bsiz, " lladdr random");
        } else {
            uint8_t *m = &a->mymac[0];
            snprintf(buf, bsiz, " lladdr " MACFMT, sMAC(m));
        }
        n = strlen(buf);
        buf  += n;
        bsiz -= n;
    }


    if (a->flags & AP_BSSID) {
        uint8_t *m = &a->apmac[0];
        snprintf(buf, bsiz, " bssid " MACFMT, sMAC(m));
        n = strlen(buf);
        buf  += n;
        bsiz -= n;
    }

    // XXX Do we show the key or not?
    if (a->flags & AP_WPAKEY) {
        snprintf(buf, bsiz, " using \"%s\"", a->key);
        n = strlen(buf);
        buf  += n;
        bsiz -= n;
    } else if (a->flags & AP_WEPKEY) {
        snprintf(buf, bsiz, " nwkey \"%s\"", a->key);
        n = strlen(buf);
        buf  += n;
        bsiz -= n;
    }

    if (a->flags & (AP_IN4|AP_IN4DHCP)) {
        if (a->flags & AP_IN4DHCP) {
            snprintf(buf, bsiz, " inet dhcp");
            n = strlen(buf);
            buf  += n;
            bsiz -= n;
        } else {
            n = fmt_ipmask(buf, bsiz, " inet %s/%s", AF_INET, &a->in4, &a->mask4);
            buf  += n;
            bsiz -= n;

            if (a->flags & AP_GW4) {
                n = fmt_ip(buf, bsiz, " gw %s", AF_INET, &a->gw4);
                buf  += n;
                bsiz -= n;
            }
        }
    }

    if (a->flags & AP_IN6) {
        n = fmt_ipmask(buf, bsiz, " inet6 %s/%s", AF_INET6, &a->in6, &a->mask6);
        buf  += n;
        bsiz -= n;
    }

    if (a->flags & AP_GW6) {
        n = fmt_ip(buf, bsiz, " gw6 %s", AF_INET6, &a->gw6);
        buf  += n;
        bsiz -= n;
    }

    return orig - bsiz;
}


/*
 * Make dir if needed
 */
static void
make_dir(const char *fn)
{
    extern int mkdirhier(const char * path, mode_t m);
    char *dn = dirname(fn); 
    int   r  = mkdirhier(dn, 0700);

    if (r != 0) error(1, -r, "can't mkdir %s", dn);
}

/* EOF */
