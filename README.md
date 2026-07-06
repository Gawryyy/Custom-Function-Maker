# Custom-Function-Maker
In this project you can make your own modules and compile them to DLL with zig to add them to my new program (Syntrix)

# clumsy Plugin SDK

This document explains how to write your own custom modules for clumsy as DLL plugins.
You do **not** need clumsy's source code. You only need two files that ship with the SDK:

- `clumsy_plugin.h` the only header you include
- `windivert.h` the WinDivert header (also included in the SDK)

Check out `lag_module.c` as a complete working reference - it implements artificial packet delay and covers everything a plugin needs.

---

## What is a plugin?

A plugin is a regular Windows DLL that clumsy loads at runtime. When loaded, it adds a new row to the Functions panel with a toggle, an activity indicator, and whatever controls you define. When the toggle is on and clumsy is capturing, your `process()` function gets called on every batch of packets so you can inspect, drop, modify, delay, or duplicate them however you like.

---

## Files you need to distribute with your plugin

Along with your `.dll` you must ship:

```
WinDivert.dll (from WinDivert x64 folder)
WinDivert64.sys (from WinDivert x64 folder)
iup.dll (from the IUP dll folder)
```

These need to be in the same folder as `clumsy.exe` when the plugin is loaded.
Which should already be there.

---

## Minimal plugin skeleton

```c
#include <Windows.h>
#include "iup.h"
#include "clumsy_plugin.h"

static volatile short myEnabled = 0;

static void* mySetupUI(void) {
    // return an IupHbox with your controls
    // if you have no controls, return IupHbox(NULL)
    return (void*)IupHbox(NULL);
}

static void myStartUp(void) {
    // called each time the user clicks Start
    // reset any per-run state here
}

static void myCloseDown(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    // called each time the user clicks Stop
    // free or flush any queued packets here
    (void)head; (void)tail;
}

static short myProcess(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    // called on every packet batch while the toggle is ON
    // return 1 if you touched at least one packet (lights the green LED)
    // return 0 if you did nothing
    (void)head; (void)tail;
    return 0;
}

static ClumsyModule myModule = {
    "My Module", // displayName - shown in the UI 
    "mymodule", // shortName - single word, no spaces 
    &myEnabled,
    mySetupUI,
    myStartUp,
    myCloseDown,
    myProcess,
    0, 0, NULL // runtime fields - always zero init these
};

CLUMSY_PLUGIN_EXPORT ClumsyModule* clumsy_get_module(void) {
    return &myModule;
}

CLUMSY_PLUGIN_EXPORT int clumsy_api_version(void) {
    return CLUMSY_PLUGIN_API_VERSION;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;
    return TRUE;
}
```

---

## How to compile

### MinGW / GCC (recommended, free)

```bash
gcc -shared -O2 -o my_module.dll my_module.c ^
    -I"sdk\include" ^
    -L"sdk\WinDivert-2.2.0\x64" -lWinDivert ^
    -L"sdk\iup" -liup
```

### MSVC

```
cl /LD /O2 my_module.c ^
   /I"sdk\include" ^
   /link "sdk\WinDivert-2.2.0\x64\WinDivert.lib" "sdk\iup\iup.lib" ^
   /OUT:my_module.dll
```

### ZIG 0.9.1

You can install zig here: https://ziglang.org/download/
Scroll down and find the correct version install it in your windows C:\
disk for example. Then go to the folder and copy the path for example:
C:\zig-windows-x86_64-0.9.1\ . Then add it inside your paths in Environment Variables. Press 'OK' and you're done.

Right click in an empty spot inside of the directory and press 'open in terminal'.
Then enter one of these following commands zig commands:
```
    zig build <- build clumsy.exe
    zig build --build-file build_plugins.zig <- then build plugins, x64 Debug
    zig build --build-file build_plugins.zig -Darch=x86 <- 32-bit
    zig build --build-file build_plugins.zig -Dconf=Release
    zig build --build-file build_plugins.zig -Dclumsy_dir=PATH <- if clumsy.lib isn't at the default zig-out path.
```

After compiling, open clumsy, click **Load Plugin (.dll)...** and select your DLL.

---

## Understanding the packet list

clumsy maintains a doubly-linked list of captured packets between two sentinel nodes called `head` and `tail`. Your `process()` function receives both. The real packets live between them:

```
head <-> [packet A] <-> [packet B] <-> [packet C] <-> tail
```

The sentinels are never real packets - never touch them directly. Always iterate from `head->next` up to (but not including) `tail`:

```c
ClumsyPacketNode *pac = head->next;
while (pac != tail) {
    ClumsyPacketNode *next = pac->next; // save next BEFORE you potentially remove pac
    
    // ... do something with pac ...
    
    pac = next;
}
```

Always save `pac->next` before you call `popNode(pac)`, otherwise you lose your position in the list.

Each node looks like this:

```c
typedef struct _ClumsyPacketNode {
    char *packet; // raw packet bytes
    UINT packetLen; // length of packet in bytes
    WINDIVERT_ADDRESS addr; // direction, interface info, etc.
    DWORD timestamp; // milliseconds — only used by lag-style modules
    struct _ClumsyPacketNode *prev;
    struct _ClumsyPacketNode *next;
} ClumsyPacketNode;
```

---

## Node manipulation functions

These functions are exported by `clumsy.exe` and declared in `clumsy_plugin.h`:

```c
// Remove a node from the list and return it (does NOT free memory)
ClumsyPacketNode* popNode(ClumsyPacketNode *node);

// Free a node's memory (call after popNode if you want to drop the packet)
void freeNode(ClumsyPacketNode *node);

// Insert node before target in the list
ClumsyPacketNode* insertBefore(ClumsyPacketNode *node, ClumsyPacketNode *target);

// Insert node after target in the list
ClumsyPacketNode* insertAfter(ClumsyPacketNode *node, ClumsyPacketNode *target);

// Append node just before tail (puts it back on the main list)
ClumsyPacketNode* appendNode(ClumsyPacketNode *node);

// Returns non-zero if the list is empty (only sentinels)
short isListEmpty(void);

// Create a brand new node from a raw buffer (for duplication)
ClumsyPacketNode* createNode(char *buf, UINT len, WINDIVERT_ADDRESS *addr);
```

---

## Checking packet direction

`addr.Outbound` is 1 for outgoing packets and 0 for incoming. Use the helper:

```c
if (clumsy_check_direction(pac->addr.Outbound, handleInbound, handleOutbound)) {
    // packet matches the direction settings
}
```

---

## Reading packet headers with WinDivert

WinDivert provides helper functions to parse raw packet bytes into typed structs. You don't need to manually offset into bytes. The key function is:

```c
BOOL WinDivertHelperParsePacket(
    const void *pPacket,
    UINT packetLen,
    PWINDIVERT_IPHDR *ppIpHdr, // IPv4 header, or NULL if not IPv4
    PWINDIVERT_IPV6HDR *ppIpv6Hdr, // IPv6 header, or NULL if not IPv6
    PUINT8 *ppIcmpHdr, // pass NULL if you don't care
    PWINDIVERT_ICMPHDR *ppIcmpHdr2, // pass NULL if you don't care
    PWINDIVERT_ICMPV6HDR *ppIcmpV6Hdr, // pass NULL if you don't care
    PWINDIVERT_TCPHDR *ppTcpHdr, // TCP header, or NULL if not TCP
    PWINDIVERT_UDPHDR *ppUdpHdr, // UDP header, or NULL if not UDP
    PVOID *ppData, // payload pointer
    UINT *pDataLen, // payload length
    PVOID *ppNext, // pass NULL
    UINT *pNextLen // pass NULL
);
```

Typical usage — parse and check what protocol we have:

```c
PWINDIVERT_IPHDR ipHdr = NULL;
PWINDIVERT_IPV6HDR ipv6Hdr = NULL;
PWINDIVERT_TCPHDR tcpHdr = NULL;
PWINDIVERT_UDPHDR udpHdr = NULL;
PVOID payload = NULL;
UINT payloadLen = 0;

WinDivertHelperParsePacket(
    pac->packet, pac->packetLen,
    &ipHdr, &ipv6Hdr,
    NULL, NULL, NULL,
    &tcpHdr, &udpHdr,
    &payload, &payloadLen,
    NULL, NULL
);

if (tcpHdr != NULL) {
    // it's TCP
    UINT16 srcPort = ntohs(tcpHdr->SrcPort);
    UINT16 dstPort = ntohs(tcpHdr->DstPort);
}

if (udpHdr != NULL) {
    // it's UDP
    UINT16 srcPort = ntohs(udpHdr->SrcPort);
}

if (ipHdr != NULL) {
    // IPv4 — source and destination as 32-bit integers (network byte order)
    UINT8 *src = (UINT8*)&ipHdr->SrcAddr;
    // src[0].src[1].src[2].src[3]
}
```

**Important:** Port numbers and addresses in headers are in **network byte order** (big-endian). Always use `ntohs()` for 16-bit ports and `ntohl()` for 32-bit addresses when reading them, and `htons()`/`htonl()` when writing back.

After modifying any header fields you must recalculate checksums:

```c
WinDivertHelperCalcChecksums(pac->packet, pac->packetLen, &pac->addr, 0);
```

---

## Example: Drop module

Drops a configurable percentage of matching packets. Good starting point for any probability-based module.

```c
#include <stdlib.h>
#include <Windows.h>
#include "iup.h"
#include "clumsy_plugin.h"

#define DROP_NAME "drop"
#define DROP_DISPLAY_NAME "Drop"

static volatile short dropEnabled = 0;
static volatile short dropInbound = 1;
static volatile short dropOutbound = 1;
static volatile short dropChance = 10; // % 0-100

static Ihandle *inCheck, *outCheck, *chanceText;

static void* dropSetupUI(void) {
    Ihandle *box = IupHbox(
        inCheck = IupToggle("Inbound",  NULL),
        outCheck = IupToggle("Outbound", NULL),
        IupLabel("Drop %:"),
        chanceText = IupText(NULL),
        NULL
    );

    IupSetAttribute(chanceText, "VISIBLECOLUMNS", "3");
    IupSetAttribute(chanceText, "VALUE", "10");
    IupSetCallback(chanceText, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    IupSetAttribute(chanceText, PLUGIN_SYNCED_VALUE, (char*)&dropChance);
    IupSetAttribute(chanceText, PLUGIN_INTEGER_MAX,  "100");
    IupSetAttribute(chanceText, PLUGIN_INTEGER_MIN,  "0");

    IupSetCallback(inCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inCheck, PLUGIN_SYNCED_VALUE, (char*)&dropInbound);
    IupSetAttribute(inCheck, "VALUE", "ON");

    IupSetCallback(outCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outCheck, PLUGIN_SYNCED_VALUE, (char*)&dropOutbound);
    IupSetAttribute(outCheck, "VALUE", "ON");

    return (void*)box;
}

static void dropStartUp(void)  { /* nothing to reset */ }
static void dropCloseDown(ClumsyPacketNode *h, ClumsyPacketNode *t) {
    (void)h; (void)t;
}

static short dropProcess(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    short triggered = 0;
    ClumsyPacketNode *pac = head->next;

    while (pac != tail) {
        ClumsyPacketNode *next = pac->next;

        if (clumsy_check_direction(pac->addr.Outbound, dropInbound, dropOutbound)) {
            // rand() % 100 gives 0..99, so dropChance=10 means ~10% drop
            if ((rand() % 100) < dropChance) {
                freeNode(popNode(pac)); // remove and free = packet is gone
                triggered = 1;
            }
        }

        pac = next;
    }

    return triggered;
}

static ClumsyModule dropModule = {
    DROP_DISPLAY_NAME, DROP_NAME,
    &dropEnabled,
    dropSetupUI, dropStartUp, dropCloseDown, dropProcess,
    0, 0, NULL
};

CLUMSY_PLUGIN_EXPORT ClumsyModule* clumsy_get_module(void) { return &dropModule; }
CLUMSY_PLUGIN_EXPORT int clumsy_api_version(void) { return CLUMSY_PLUGIN_API_VERSION; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    return TRUE;
}
```

---

## Example: Tamper module

Randomly corrupts bytes inside the packet payload. This simulates bitflips and bad network conditions. Note it rebuilds checksums after modifying bytes, otherwise the OS will silently discard the corrupted packet.

```c
#include <stdlib.h>
#include <Windows.h>
#include "iup.h"
#include "clumsy_plugin.h"

#define TAMPER_NAME "tamper"
#define TAMPER_DISPLAY_NAME "Tamper"

static volatile short tamperEnabled = 0;
static volatile short tamperInbound = 1;
static volatile short tamperOutbound = 1;
static volatile short tamperChance = 10;  // % of packets to corrupt

static Ihandle *inCheck, *outCheck, *chanceText;

static void* tamperSetupUI(void) {
    Ihandle *box = IupHbox(
        inCheck = IupToggle("Inbound",  NULL),
        outCheck = IupToggle("Outbound", NULL),
        IupLabel("Tamper %:"),
        chanceText = IupText(NULL),
        NULL
    );

    IupSetAttribute(chanceText, "VISIBLECOLUMNS", "3");
    IupSetAttribute(chanceText, "VALUE", "10");
    IupSetCallback(chanceText, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
    IupSetAttribute(chanceText, PLUGIN_SYNCED_VALUE, (char*)&tamperChance);
    IupSetAttribute(chanceText, PLUGIN_INTEGER_MAX, "100");
    IupSetAttribute(chanceText, PLUGIN_INTEGER_MIN, "0");

    IupSetCallback(inCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inCheck, PLUGIN_SYNCED_VALUE, (char*)&tamperInbound);
    IupSetAttribute(inCheck, "VALUE", "ON");

    IupSetCallback(outCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outCheck, PLUGIN_SYNCED_VALUE, (char*)&tamperOutbound);
    IupSetAttribute(outCheck, "VALUE", "ON");

    return (void*)box;
}

static void tamperStartUp(void)  {}
static void tamperCloseDown(ClumsyPacketNode *h, ClumsyPacketNode *t) {
    (void)h; (void)t;
}

static short tamperProcess(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    short triggered = 0;
    ClumsyPacketNode *pac = head->next;

    while (pac != tail) {
        ClumsyPacketNode *next = pac->next;

        if (clumsy_check_direction(pac->addr.Outbound, tamperInbound, tamperOutbound)) {
            if ((rand() % 100) < tamperChance) {
                // Parse the packet to find where the payload starts
                PWINDIVERT_IPHDR ipHdr = NULL;
                PWINDIVERT_IPV6HDR ipv6Hdr = NULL;
                PWINDIVERT_TCPHDR tcpHdr = NULL;
                PWINDIVERT_UDPHDR udpHdr = NULL;
                PVOID payload = NULL;
                UINT payloadLen = 0;

                WinDivertHelperParsePacket(
                    pac->packet, pac->packetLen,
                    &ipHdr, &ipv6Hdr,
                    NULL, NULL, NULL,
                    &tcpHdr, &udpHdr,
                    &payload, &payloadLen,
                    NULL, NULL
                );

                // Only tamper if there's actual payload data to corrupt
                if (payload != NULL && payloadLen > 0) {
                    // Flip a random byte in the payload
                    UINT offset = rand() % payloadLen;
                    ((unsigned char*)payload)[offset] ^= (unsigned char)(rand() % 256);

                    // Checksums MUST be recalculated after modifying packet bytes.
                    // Without this the OS/protocol stack will silently drop the packet
                    // because the checksum won't match.
                    WinDivertHelperCalcChecksums(
                        pac->packet, pac->packetLen, &pac->addr, 0
                    );

                    triggered = 1;
                }
            }
        }

        pac = next;
    }

    return triggered;
}

static ClumsyModule tamperModule = {
    TAMPER_DISPLAY_NAME, TAMPER_NAME,
    &tamperEnabled,
    tamperSetupUI, tamperStartUp, tamperCloseDown, tamperProcess,
    0, 0, NULL
};

CLUMSY_PLUGIN_EXPORT ClumsyModule* clumsy_get_module(void) { return &tamperModule; }
CLUMSY_PLUGIN_EXPORT int clumsy_api_version(void) { return CLUMSY_PLUGIN_API_VERSION; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    return TRUE;
}
```

---

## UI controls reference

All UI is built with IUP. Your `setupUI()` must return an `IupHbox(...)` containing your controls cast to `void*`. The sync helpers wire a control's value directly to one of your `volatile` variables:

### Text input (integer)

```c
Ihandle *input = IupText(NULL);
IupSetAttribute(input, "VISIBLECOLUMNS", "4"); // width in characters
IupSetAttribute(input, "VALUE", "100"); // default value as string
IupSetCallback (input, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
IupSetAttribute(input, PLUGIN_SYNCED_VALUE, (char*)&myShortVar);
IupSetAttribute(input, PLUGIN_INTEGER_MAX, "1000");
IupSetAttribute(input, PLUGIN_INTEGER_MIN, "0");
```

Use `uiSyncInt32` instead of `uiSyncInteger` if your variable is a `LONG` (32-bit).

### Checkbox / toggle

```c
Ihandle *check = IupToggle("Label", NULL);
IupSetCallback (check, "ACTION", (Icallback)uiSyncToggle);
IupSetAttribute(check, PLUGIN_SYNCED_VALUE, (char*)&myShortVar);
IupSetAttribute(check, "VALUE", "ON"); // start checked
```

### Label (no interaction)

```c
IupLabel("some text")
```

### Spacer

```c
IupFill() // expands to push controls to the sides
```

---

## Common mistakes

**Forgetting to save `pac->next` before removing a node.**
Once you call `popNode(pac)`, `pac->next` and `pac->prev` are garbage. Always do:
```c
ClumsyPacketNode *next = pac->next; // save first
freeNode(popNode(pac)); // then remove
pac = next; // then advance
```

**Modifying packet bytes without recalculating checksums.**
If you change any header field or payload byte, call `WinDivertHelperCalcChecksums()` before returning from `process()`. The OS drops packets with bad checksums silently, which looks like your module isn't working.

**Holding packet pointers after `process()` returns.**
You may keep `ClumsyPacketNode*` pointers in your own queue (like the lag module does) but only while they've been removed from clumsy's list via `popNode()`. Never keep a pointer to a node that's still on the main list.

**Not flushing your queue in `closeDown()`.**
If you hold packets in a private queue (for delay etc.), you must free them all in `closeDown()`. Otherwise clumsy leaks memory and the packets are never sent.

**Port numbers are in network byte order.**
Always convert with `ntohs()` when reading and `htons()` when writing port numbers. Same for 32-bit addresses with `ntohl()`/`htonl()`.

---

## Quick reference card

```
To DROP a packet:
    freeNode(popNode(pac));

To PASS a packet through unchanged:
    just don't touch it — clumsy sends it automatically

To DELAY a packet:
    popNode(pac) to remove it from the main list
    store it in your own queue with a release timestamp
    in each process() call, appendNode() any packets whose time has come

To DUPLICATE a packet:
    ClumsyPacketNode *copy = createNode(pac->packet, pac->packetLen, &pac->addr);
    appendNode(copy); // original stays on list, copy is also sent

To MODIFY a packet:
    edit pac->packet bytes directly (use WinDivertHelperParsePacket to find offsets)
    WinDivertHelperCalcChecksums(pac->packet, pac->packetLen, &pac->addr, 0);

To CHECK direction:
    clumsy_check_direction(pac->addr.Outbound, handleInbound, handleOutbound)

To PARSE headers:
    WinDivertHelperParsePacket(pac->packet, pac->packetLen,
        &ipHdr, &ipv6Hdr, NULL, NULL, NULL, &tcpHdr, &udpHdr,
        &payload, &payloadLen, NULL, NULL);
```
