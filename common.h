/* vim: expandtab:tw=68:ts=4:sw=4:
 *
 * common.h - Common header between ifscand and ifscanctl
 *
 * Copyright (c) 2016 Sudhi Herle <sw at herle.net>
 */

#ifndef ___COMMON_H_9509687_1482985191__
#define ___COMMON_H_9509687_1482985191__ 1

    /* Provide C linkage for symbols declared here .. */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>
#include <netinet/in.h>
#include <net80211/ieee80211.h>
#include <net/if.h>
#include <net80211/ieee80211_ioctl.h>
#include "vect.h"
#include "fastbuf.h"

/*
 * Global vars
 */
#define IFSCAND_PREFS       "/var/ifscand/prefs"
#define IFSCAND_SOCK        "/var/run/ifscand"



#define AP_NAMELEN  128
#define AP_KEYLEN   128


/*
 * BitFlags used in 'apdata' below.
 */
#define AP_MAC      (1 << 0)
#define AP_KEY      (1 << 1)

/*
 * MYMAC is set if the non-default MAC address is set for a given
 * AP. The non-default can be a fixed value or a random value.
 * RANDMAC signifies the latter.
 */
#define AP_MYMAC    (1 << 2)
#define AP_RANDMAC  (1 << 3)
#define AP_IN4      (1 << 4)
#define AP_GW4      (1 << 5)
#define AP_IN6      (1 << 6)
#define AP_GW6      (1 << 7)
#define AP_KEYTYPE  (1 << 8)


/*
 * Supported key types
 */
#define AP_KEYTYPE_WEP      1
#define AP_KEYTYPE_WPA      2

/*
 * information about a particular AP and the user's preference. This
 * is persisted.
 */
struct apdata
{
    // Flags determine validity of the rest of the contents below.
    uint32_t flags; // AP_xxx flags above.

    char apname[AP_NAMELEN];
    char key[AP_KEYLEN];

    uint8_t apmac[6];   // pinned MAC addr
    uint8_t mymac[6];   // station MAC addr
    uint16_t keytype;   // One of AP_KEY_XXX above

    struct in_addr in4;
    struct in_addr mask4;
    struct in_addr gw4;

    struct in6_addr in6;
    struct in6_addr mask6;
    struct in6_addr gw6;

    /*
     * We need these two elements for measuring RSSI
     */
    uint8_t nr_bssid[6];
    int8_t  nr_rssi;
    int8_t  nr_max_rssi;
};
typedef struct apdata apdata;


// Array of persisted AP's
VECT_TYPEDEF(apvect, apdata);

// List of All APs we have scanned
VECT_TYPEDEF(nodevect, struct ieee80211_nodereq);

// Vector of strings
VECT_TYPEDEF(strvect, char *);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! ___COMMON_H_9509687_1482985191__ */

/* EOF */
