/* $OpenBSD: ifscand.c,v 1.330 2016/09/03 13:46:57 reyk Exp $
 *
 * cmds.c - parse and handle commands from ifscanctl.
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
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "utils.h"
#include "ifscand.h"

// Handle command and send response to 'fp'
typedef int cmd_handler_func(cmd_state *s, char **args, int argc);

// Command name -to- handler mapping
struct cmdpair
{
    const char *name;
    cmd_handler_func *handler;
};
typedef struct cmdpair cmdpair;

// "add" keyword parser
typedef int kwparser(apdata *d, char* val);

struct kwpair
{
    const char *name;
    kwparser *fp;
};
typedef struct kwpair kwpair;


/*
 * Command handlers.
 */
static int cmd_add(cmd_state *s,  char **args, int argc);
static int cmd_del(cmd_state *s,  char **args, int argc);
static int cmd_list(cmd_state *s, char **args, int argc);
static int cmd_scan(cmd_state *s, char **args, int argc);
static int cmd_down(cmd_state *s, char **args, int argc);
static int cmd_set(cmd_state *s,  char **args, int argc);
static int cmd_get(cmd_state *s,  char **args, int argc);


static const cmdpair Commands[] = {
      {"add",  cmd_add}
    , {"del",  cmd_del}
    , {"list", cmd_list}
    , {"scan", cmd_scan}
    , {"down", cmd_down}
    , {"set",  cmd_set}
    , {"get",  cmd_get}
    , {0, 0}
};


/*
 * "add" keyword parsers.
 */
static int parse_apmac(apdata *d, char *val);
static int parse_mymac(apdata *d, char *val);
static int parse_in4mask(apdata *d, char *val);
static int parse_in6mask(apdata *d, char *val);
static int parse_gw4(apdata *d, char *val);
static int parse_gw6(apdata *d, char *val);

static int parse_wpakey(apdata *d, char *val);
static int parse_wepkey(apdata *d, char *val);
static int parse_nwid(apdata *d, char *val);


static const kwpair Add_kw[] = {
      {"nwid",      parse_nwid}
    , {"lladdr",    parse_mymac}
    , {"wpakey",    parse_wpakey}
    , {"nwkey",     parse_wepkey}
    , {"bssid",     parse_apmac}
    , {"inet",      parse_in4mask}
    , {"inet6",     parse_in6mask}
    , {"gw",        parse_gw4}
    , {"gw6",       parse_gw6}
    , {0, 0}
};



static int
cmd_error(cmd_state *s, const char *fmt, ...)
{
    char buf[1024];
    char out[1024];
    va_list ap;

    va_start(ap, fmt);
    snprintf(buf, sizeof buf, "ERROR: %s", fmt);
    vsnprintf(out, sizeof out, buf, ap);
    va_end(ap);

    fast_buf_push(&s->out, out, strlen(out));
    return -EINVAL;
}


static void
cmd_response_ok(cmd_state *s)
{
    char out[128];

    snprintf(out, sizeof out, "OK");

    fast_buf_push(&s->out, out, strlen(out));
}

static int
parse_mac(unsigned char *dest, char *s)
{
    int i, k;
    char *p;

    for (k = i = 0; i < 6; i++) {
        int v = strtol(s, &p, 16);
        if (p == s || v > 0xff || v < 0) return 0;
        switch (*p) {
            case ':':
                if (i == 5) return 0;
                break;
            case 0:
                if (i != 5) return 0;
                break;
            default:
                return 0;
        }

        *dest++ = 0xff & v;
        s = p + 1;
    }

    return 1;
}


static int
parse_in4mask(apdata *d, char *s)
{
    if (0 == strcmp(s, "dhcp")) {
        d->flags |= AP_IN4DHCP;
        return 1;
    }

    char *mask = strchr(s, '/');
    if (mask) {
        *mask = 0;

        // see if it looks like an IP address.
        if (1 != inet_pton(AF_INET, mask+1, &d->mask4)) {
            const char *err;
            int v = strtonum(mask+1, 0, 32, &err);
            if (err)     return 0;

            d->mask4.s_addr = htonl(~((1 << (32 -v))-1));
        }

    } else {
        d->mask4.s_addr = 0xffffffff;
    }

    if (1 != inet_pton(AF_INET, s, &d->in4)) return 0;

    d->flags |= AP_IN4;
    return 1;
}

static int
parse_gw4(apdata *d, char *s)
{
    if (1 == inet_pton(AF_INET, s, &d->gw4)) {
        d->flags |= AP_GW4;
        return 1;
    }

    return 0;
}


static int
parse_in6mask(apdata *d, char *s)
{
    char *mask = strchr(s, '/');
    if (mask) {
        *mask = 0;
        if (1 != inet_pton(AF_INET6, mask+1, &d->mask6)) {
            const char *err;
            int v = strtonum(mask+1, 0, 128, &err);
            if (err)    return 0;
            if (v == 0 || v == 128) {
                memset(&d->mask6.s6_addr, 0xff, sizeof d->mask6.s6_addr);
            } else {
                uint8_t *p;
                memset(&d->mask6.s6_addr, 0x00, sizeof d->mask6.s6_addr);
                for (p = (uint8_t*)&d->mask6.s6_addr; v > 7; v -= 8) {
                    *p++ = 0xff;
                }
                if (v)    *p = (0xff << (8-v));
            }
        }
    } else {
        memset(&d->mask6.s6_addr, 0xff, sizeof d->mask6.s6_addr);
    }

    if (1 != inet_pton(AF_INET6, s, &d->in6)) return 0;

    d->flags |= AP_IN6;
    return 1;
}


static int
parse_gw6(apdata *d, char *s)
{
    if (1 == inet_pton(AF_INET6, s, &d->gw6)) {
        d->flags |= AP_GW6;
        return 1;
    }

    return 0;
}


static int
parse_apmac(apdata *d, char *s)
{
    if (parse_mac(d->apmac, s)) {
        d->flags |= AP_BSSID;
        return 1;
    }
    return 0;
}


static int
parse_mymac(apdata *d, char *s)
{
    if (0 == strcmp("random", s)) {
        d->flags |= AP_RANDMAC|AP_MYMAC;
        return 1;
    }

    if (parse_mac(d->apmac, s)) {
        d->flags |= AP_MYMAC;
        return 1;
    }
    return 0;
}


static int
parse_wpakey(apdata *d, char *s)
{
    strlcpy(d->key, s, sizeof d->key);
    d->flags |= AP_WPAKEY;
    return 1;
}

static int
parse_wepkey(apdata *d, char *s)
{
    strlcpy(d->key, s, sizeof d->key);
    d->flags |= AP_WEPKEY;
    return 1;
}


static int
parse_nwid(apdata *d, char *s)
{
    strlcpy(d->apname, s, sizeof d->apname);
    d->flags |= AP_NWID;
    return 1;
}


static kwparser *
find_parser(const char *kw)
{
    const kwpair *a = Add_kw;

    for (; a->name; a++) {
        if (0 == strcmp(kw, a->name)) return a->fp;
    }
    return 0;
}



// add nwid AP [lladdr MAC] [wpakey|nwkey KEY] [bssid mac] [inet dhcp|IP/MASK] [gw IP] [inet6 IP6/MASK6] [GW6 IP]
static int
cmd_add(cmd_state *s, char **args, int argc)
{
    int i;
    apdata d;

    if (argc < 1)  return cmd_error(s, "insufficient arguments to 'add'");
    if (argc > 12) return cmd_error(s, "too many arguments to 'add'");
    if (0 != (argc % 2)) return cmd_error(s, "incomplete arguments to 'add'");

    memset(&d, 0, sizeof d);
    for (i = 0; i < argc; i += 2) {
        char *kw  = args[i];
        char *val = args[i+1];

        kwparser *fp = find_parser(kw);
        if (!fp) return cmd_error(s, "unknown keyword %s in 'add'", kw);

        if (!(*fp)(&d, val)) return cmd_error(s, "malformed value %s for %s in 'add'", val, kw);
    }

    uint32_t flags = d.flags;

    if (! (flags & AP_NWID)) return cmd_error(s, "missing AP name");

    if ( (flags & (AP_WPAKEY|AP_WEPKEY)) == (AP_WPAKEY|AP_WEPKEY))
        return cmd_error(s, "only one of WPA or WEP is needed");

    if ((flags & AP_GW4) && !(flags & AP_IN4))
        return cmd_error(s, "default-gateway needs an IPv4 address/mask");

    if ((flags & AP_GW6) && !(flags & AP_IN6))
        return cmd_error(s, "default-gateway needs IPv6 address/mask");

    db_set_apdata(s->db, &d);

    cmd_response_ok(s);
    return 1;
}


// del AP
static int
cmd_del(cmd_state *s, char **args, int argc)
{
    if (argc < 1) return cmd_error(s, "insufficient arguments to 'del'");

    char *ap = args[0];

    db_del_ap(s->db, ap);
    cmd_response_ok(s);
    return 1;
}


// list all configured AP
static int
cmd_list(cmd_state *s, char **args, int argc)
{
    int json = 0;

    if (argc > 0) {
        if (argc > 1) return cmd_error(s, "too many arguments to 'list'");

        if (0 != strcmp(args[0], "json")) return cmd_error(s, "unknown format %s for 'list'", args[0]);
        json = 1;
    }

    // XXX Lets not do json yet

    char line[1024];
    apvect av;

    VECT_INIT(&av, 8);
    db_get_all_ap(s->db, &av);
    if (VECT_SIZE(&av) == 0) {
        cmd_error(s, "No remembered access points");
        goto end;
    }

    apdata *a;
    VECT_FOR_EACH(&av, a) {
        size_t n = db_ap_sprintf(line, (sizeof line)-2,  a);

        line[n++] = '\n';
        line[n]   = 0;

        fast_buf_push(&s->out, line, n);
    }

end:
    VECT_FINI(&av);
    return 1;
}


// scan visible AP
static int
cmd_scan(cmd_state *s, char **args, int argc)
{
    assert(s->ifs);

    int json = 0;

    if (argc > 0) {
        if (argc > 1) return cmd_error(s, "too many arguments to 'scan'");

        if (0 != strcmp(args[0], "json")) return cmd_error(s, "unknown format %s for 'scan'", args[0]);
        json = 1;
    }

    // XXX Lets not do json yet

    ifstate_scan(s->ifs);

    nodevect *apv = &s->ifs->nv;
    struct ieee80211_nodereq *nr;
    
    if (VECT_SIZE(apv) == 0) return cmd_error(s, "no access points visible");

    char buf[1024];
    VECT_FOR_EACH(apv, nr) {
        ssize_t n = ifstate_sprintf_node(buf, (sizeof buf)-2, nr);

        buf[n++] = '\n';
        buf[n]   = 0;

        fast_buf_push(&s->out, buf, n);
    }
    return 1;
}


// set global randmac property
static int
cmd_set_randmac(cmd_state *s, char **args, int argc)
{
    char *z = args[0];
    int val = 0;

    argc--;
    args++;

    if (0 == strcasecmp("true", z) ||
        0 == strcasecmp("yes", z)  ||
        0 == strcmp("1", z)) {
        val = 1;
    } else if (0 != strcasecmp("false", z) &&
               0 != strcasecmp("no", z)    &&
               0 != strcmp("0", z)) {
        return cmd_error(s, "Unknown boolean value '%s'", z);
    }

    db_set_randmac(s->db, val);
    cmd_response_ok(s);
    return 1;
}


// configure relative AP order
static int
cmd_set_aporder(cmd_state *s, char **args, int argc)
{
    if (argc < 1) return cmd_error(s, "Insufficient arguments to 'ap-order'");

    db_set_ap_order(s->db, args, argc);

    cmd_response_ok(s);
    return 1;
}


static int
cmd_set(cmd_state *s, char **args, int argc)
{
    if (argc < 2)
        return cmd_error(s, "insufficient arguments to 'set'");

    char *sub = args[0];

    args++;
    argc--;

    if (0 == strcmp(sub, "randmac"))
        return cmd_set_randmac(s, args, argc);

    if (0 == strcmp(sub, "ap-order"))
        return cmd_set_aporder(s, args, argc);

    return cmd_error(s, "unknown 'set %s'", sub);
}



static void
append_randmac(apdb *db, fast_buf *out)
{
    char buf[64];
    int randmac = db_get_randmac(db);
    snprintf(buf, sizeof buf, "randmac %s\n", randmac ? "true" : "false");
    fast_buf_push(out, buf, strlen(buf));
}


static void
append_aporder(apdb *db, fast_buf *out)
{
    char buf[256];
    strvect sv  = db_get_ap_order(db);
    size_t i;

    if (VECT_SIZE(&sv) == 0) {
        fast_buf_append(out, '\n');
        goto end;
    }

    snprintf(buf, sizeof buf, "ap-order");
    fast_buf_push(out, buf, strlen(buf));

    for (i = 0; i < VECT_SIZE(&sv)-1; i++) {
        snprintf(buf, sizeof buf, " \"%s\"", VECT_ELEM(&sv, i));
        fast_buf_push(out, buf, strlen(buf));
    }
    snprintf(buf, sizeof buf, " \"%s\"\n", VECT_ELEM(&sv, i));
    fast_buf_push(out, buf, strlen(buf));

end:
    VECT_FINI(&sv);
    return;
}

static int
cmd_get(cmd_state *s, char **args, int argc)
{
    if (argc < 1) return cmd_error(s, "too few arguments to 'get'");
    char *key = args[0];

    if (0 == strcmp(key, "all")) {
        append_randmac(s->db, &s->out);
        append_aporder(s->db, &s->out);
    } else if (0 == strcmp(key, "randmac"))       append_randmac(s->db, &s->out);
      else if (0 == strcmp(key, "ap-order"))      append_aporder(s->db, &s->out);
      else return cmd_error(s, "unknown get subcommand '%s'", key);

    return 1;
}


static int
cmd_down(cmd_state *s, char **args, int argc)
{
    extern volatile uint32_t Quit;

    Quit = 1;   // ask the outer loop to quit gracefully
    cmd_response_ok(s);
    return 1;
}


static const cmdpair *
find_cmd(const char *name)
{
    const cmdpair * p = Commands;

    for (;p->name; p++) {
        if (0 == strcmp(p->name, name)) return p;
    }

    return 0;
}


// Process a command in the input buffer 's->in'. Push output to the
// output buffer 's->out'
//
// Returns:
//    0 on EOF
//    > 0 on success
//    -errno on failure
int
cmd_process(cmd_state* s)
{
    char * line = (char *)fast_buf_ptr(&s->in);
    int n;

    if (fast_buf_size(&s->in) == 0) return 0;

    strtrim(line);
    if (strlen(line) == 0 || line[0] == '#') return 0;

    char *args[128];

    //printf("Read: |%s| from %s\n", line, s->from.sun_path);
    n = strsplitargs(args, ARRAY_SIZE(args), line);

    if (n == 0)       return 1;
    if (n == -ENOSPC) return cmd_error(s, "too many arguments (max 128)");
    if (n == -EINVAL) return cmd_error(s, "missing closing quote in string");
    if (n < 0)        return cmd_error(s, "parse error");

    const cmdpair *c = find_cmd(args[0]);

    if (!c) return cmd_error(s, "unknown command %s", args[0]);

    return (*c->handler)(s, &args[1], n-1);
}

