======================================================
``ifscand`` - Automatic WiFi configuration for OpenBSD
======================================================

``ifscand`` is a daemon to automatically configure WiFi interfaces on
OpenBSD. A companion program ``ifscanctl`` provides corresponding
control - including addition, deletion of WiFi Access Points (AP).

Once setup, it is entirely automatic - you don't need to fiddle
with hostname.if(5) or dhclient(8) to join wireless networks.

**This is written specifically to work ONLY on OpenBSD.**

Usage
=====

``ifscand``::

    # ifscand [-d] [-f] IFACE

Where *IFACE* is the WiFi interface that ``ifscand`` should monitor
and auto-configure.

``ifscanctl``::

    # ifscanctl IFACE command

Where *command* can be one of::

    add nwid AP [bssid BSSID] [wpakey|nwkey KEY] [lladdr MACADDR] \
        [inet dhcp|IP4/MASK4] [gw GW4] [inet6 IP6/MASK6] [gw6 GW6]

    del AP

    list

    scan

    set ap-order AP1 [AP2...]




Design Details
==============
``ifscand`` has the following features:

- configure WiFi link layer (WEP/WPA).
- associate a static IP address for a specific AP
- enable DHCP IP address configuration (default) when joining an AP.
- detect rogue AP - by pinning BSSID to a configured value.
- randomize client MAC address when joining specific AP or all AP.
- configure relative order of selecting APs when more than one
  configured APs are visible.

``ifscand`` begins its operation by scanning for visible APs. It
does this "full scan" every 60 seconds. Once it finds one or more APs
that it is configured to join, it will sort them based on RSSI
(signal strength) and picks the one with the highest RSSI.

Once ``ifscand`` joins an AP, it will further monitor the RSSI of
the joined AP every 10 seconds. If the weighted average of the last
4 RSSI measurements falls below 8%, ``ifscand`` will do a full-scan
and pick a new AP.

``ifscand`` also configures the interface's IP address. It does this
by two means:

#. If the AP configuration doesn't have a static IP address
   configured, ``ifscand`` will start dhclient(8).

#. If the AP configuration has a static IP address configured,
   ``ifscand`` will run ifconfig(8) and route(8) to setup the
   interface address and default gateway respectively.

``ifscand`` configures the WiFi link-layer properties by calling
appropriate ioctl(2).

Rationale for Design Choices
----------------------------
* WiFi is unlike traditional link layers - users are not tethered to
  a single link-layer attachment point. And thus, whenever the link
  layer changes, the corresponding network configuration also
  changes.

  Thus, it is unwise to make ``ifscand`` only focus on link-layer
  autoconfig.  If it did only that, then the user has to manually
  decide whether to start dhclient(8) or ifconfig(8). And, most
  importantly, there is no easy place in the current schme of
  hostname.if(5) to express the binding between AP and IP address.

* ``ifscand`` manages lifetime of dhclient(8) because of the
  following scenario:

    - consider a user's setup where there are two AP's configured:
      AP1 with DHCP assigned IP address and AP2 with a static IP
      address.

    - when the user is connected to AP1, dhclient(8) will get a
      dynamic IP address. 

    - If the user "roams" out of AP1 and is connected to AP2, then
      dhclient must cease its operation -- since AP2 has a static IP
      address assigned.

  Thus, I made the decision to allow ``ifscand`` to control the
  lifetime of dhclient.


FAQ
===
How do I build it?
------------------
Assuming you are on an OpenBSD machine::

    $ make


How do I start it?
------------------
As 'root', type::

    # ./ifscand/ifscand IFNAME

Where 'IFNAME' is the wireless interface for which you want automatic
scanning and joining. Bear in mind, the above command merely starts
the daemon; it doesn't know anything about your access
points.

Use ``ifscanctl`` to configure the daemon. See examples below.

Show me some examples
---------------------
Let us assume your WiFi interface is **iwm0** and you have an AP
with SSID "myap" and WPA key "origami987".  As 'root' do::

 # ./ifscanctl/ifscanctl iwm0 add nwid myap wpakey origami987 inet dhcp
 # ./ifscanctl/ifscanctl iwm0 add nwid "Google Starbucks" inet dhcp
 # ./ifscanctl/ifscanctl iwm0 add nwid SomeAP wpakey foobar12345

The last example remembers ``SomeAP`` - but **without** any network
address (IP) configuration.

For more details, consult the man page for ``ifscanctl``.

How do I install it permanently?
--------------------------------
#. As a non-root user, build the software::

    make

#. As 'root', do the following::

    make install

   This will install the binaries and manpages to */usr/local/bin*
   and */usr/local/man* respectively.
   It will also append a fragment to */etc/rc.conf.local*. 

#. Edit */etc/rc.conf.local* and configure the ``ifscand`` line with
   the appropriate WiFi interface for your setup. e.g., if *iwm0* is
   your WiFi interface, ::

        ifscand_flags=iwm0

#. "Unconfigure" ``/etc/hostname.IFNAME`` if you have previously
   configured the interface manually.

#. Start it for the first time::

    # /etc/rc.d/ifscand start

How can I prevent leak of my MAC address to some APs
----------------------------------------------------
``ifscand`` can randomize MAC addresses prior to joining an AP.
In the example above, let us tell ``ifscand`` to use a random MAC
address with "myap"::

    # ./ifscanctl/ifscanctl iwm0 add nwid myap wpakey origami987 \
        lladdr random inet dhcp

Now, ``ifscand`` will pick a random MAC address whenever it joins
"myap".

How can I be sure that I have joined the AP I previously configured?
--------------------------------------------------------------------
When you first join an AP, manually verify (using whatever means you
can think of) that it is the right one. Once certain, you can teach
``ifscand`` to pin the AP name to its BSSID. e.g., let us connect to
"secureAP" with a pinned BSSID::

    # ifscanctl iwm0 add nwid secureAP bssid 60:00:0a:13:22:5a \
        wpakey histeriana7139 inet dhcp

Now, whenever ``ifscand`` sees the AP "secureAP", it will verify that
it's BSSID is *60:00:0a:13:22:5a*. If it isn't - it will write a
warning to syslog and eliminate the AP from the current scan
consideration.

How can I troubleshoot if ``ifscand`` isn't working for me?
-----------------------------------------------------------
Start ``ifscand`` in debug mode; it should print more diagnostics to
syslog::

    # ifscand -d IFNAME

Now, look at */var/log/daemon*; ``ifscand`` prints messages with the
prefix "ifscand.IFNAME" where "IFNAME" is the interface it is
monitoring.


Developer Notes
===============
* Files common to ``ifscand`` and ``ifscanctl`` are in the *lib*
  directory:

    - fastbuf.h: Manage growable buffer of ``uint8_t``
    - vect.h:  Typesafe vector for "C"
    - error.c: write error message to stderr along with strerror(3)
      info.
    - mkdirhier.c: re-entrant, portable mkdir -p implementation in "C"
    - splitargs.c: Separate a quoted string into an array of
      arguments.
    - str2hex.c: Convert a string containing hexadecimal characters
      into equivalent ``uint8_t`` array.
    - strtrim.c: Remove leading & trailing white space from a string

* common.h: header file common to ``ifscand`` and ``ifscanctl``.

* Guide to ``ifscand`` sources:

    - ifscand.h: Header file containing all the struct, #defines and
      function prototypes.
    - ifscand.c: main() for ifscand and some helper routines.
    - db.c: Persistent DB storage and retrieval.
    - ifcfg.c: Configure interface, scan interface etc. 
    - scan.c: Logic to scan for WiFi AP and maintenance post-joining.


BUGS, TODO
==========
* Sporadic disconnects on iwm(4) when ``ifscand`` runs. I haven't
  had time to chase this down.

* privilege separation, pledge(2) of ``ifscand``:

   #. one proc to fork/exec external programs
   #. one proc to ONLY do wifi scan and joins
   #. one proc to listen to commands from ``ifscanctl``

  Scanning and fork/exec both need root privs. 3) above doesn't in
  theory need root privs. What does this complexity buy us?

* ``ifscand`` doesn't know when the host wakes up from sleep (zzz, ZZZ).
  If it had a way to know of this from the kernel, it can scan
  immediately upon wakeup from sleep.

* ``ifscand`` has no way of asynchronously knowing when RSSI is
  declining and projected to fade. If the kernel provided this
  information, ``ifscand`` can avoid the once every 10 second scan.

* ``ifscand`` could remember the BSSID of joined AP and automatically
  verify if the BSSID changed; and print a warning if it detects a
  change..?

