/*
 * lag_module.c  -  Example clumsy plugin
 *
 * Holds every captured packet in a queue for a configurable number of
 * milliseconds, then releases it. This creates realistic network lag.
 *
 * BUILD (MSVC, from the directory containing this file and clumsy_plugin.h)
 * -------------------------------------------------------------------------
 *   cl /LD /O2 lag_module.c ^
 *      /I"path\to\WinDivert\include" ^
 *      /link "path\to\WinDivert\x64\WinDivert.lib" ^
 *      /OUT:lag.dll
 *
 * BUILD (MinGW / GCC)
 * -------------------------------------------------------------------------
 *   gcc -shared -O2 -o lag.dll lag_module.c \
 *       -I"path/to/WinDivert/include" \
 *       -L"path/to/WinDivert/x64" -lWinDivert
 * 
 * Or just BUILD using zig
 * -------------------------------------------------------------------------
 *      zig build --build-file build_plugins.zig     <- then build plugins, x64 Debug
 *      zig build --build-file build_plugins.zig -Darch=x86     <- 32-bit
 *      zig build --build-file build_plugins.zig -Dconf=Release
 *      zig build --build-file build_plugins.zig -Dclumsy_dir=PATH  <- if clumsy.lib isn't at the default zig-out path
 *
 * Then drop lag.dll onto the clumsy window or select it when you
 * pressed the button while it is running.
 */

#include <stdlib.h>
#include <Windows.h>
#include "iup.h"
#include "clumsy_plugin.h"

// ------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------
#define LAG_NAME "lag"
#define LAG_DISPLAY_NAME "Lag"
#define LAG_MIN_MS 0
#define LAG_MAX_MS 5000
#define LAG_DEFAULT_MS 100

// ------------------------------------------------------------------
// Module state
// ------------------------------------------------------------------
static volatile short lagEnabled = 0;
static volatile short lagInbound = 1;
static volatile short lagOutbound = 1;
static volatile LONG lagDelayMs = LAG_DEFAULT_MS;

typedef struct _LagNode {
    ClumsyPacketNode *packetNode;
    DWORD releaseTime;
    struct _LagNode *next;
} LagNode;

static LagNode *lagQueueHead = NULL;
static LagNode *lagQueueTail = NULL;

// ------------------------------------------------------------------
// Queue helpers
// ------------------------------------------------------------------
static void lagEnqueue(ClumsyPacketNode *pn, DWORD releaseTime) {
    LagNode *ln = (LagNode*)malloc(sizeof(LagNode));
    if (!ln) return;
    ln->packetNode = pn;
    ln->releaseTime = releaseTime;
    ln->next = NULL;
    if (lagQueueTail) {
        lagQueueTail->next = ln;
    } else {
        lagQueueHead = ln;
    }
    lagQueueTail = ln;
}

static ClumsyPacketNode* lagDequeueIfDue(DWORD now) {
    if (!lagQueueHead) return NULL;
    if ((int)(now - lagQueueHead->releaseTime) < 0) return NULL;
    LagNode *ln = lagQueueHead;
    lagQueueHead = ln->next;
    if (!lagQueueHead) lagQueueTail = NULL;
    ClumsyPacketNode *pn = ln->packetNode;
    free(ln);
    return pn;
}

static void lagFlushQueue() {
    while (lagQueueHead) {
        LagNode *ln = lagQueueHead;
        lagQueueHead = ln->next;
        freeNode(ln->packetNode);
        free(ln);
    }
    lagQueueTail = NULL;
}

// ------------------------------------------------------------------
// UI
// ------------------------------------------------------------------
static Ihandle *inboundCheck, *outboundCheck, *delayInput;

static void* lagSetupUI(void) {
    Ihandle *box = IupHbox(
        inboundCheck = IupToggle("Inbound", NULL),
        outboundCheck = IupToggle("Outbound", NULL),
        IupLabel("Delay(ms):"),
        delayInput = IupText(NULL),
        NULL
    );

    // Textbox
    IupSetAttribute(delayInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(delayInput, "VALUE", "100");
    IupSetCallback(delayInput, "VALUECHANGED_CB", (Icallback)uiSyncInt32);
    IupSetAttribute(delayInput, PLUGIN_SYNCED_VALUE, (char*)&lagDelayMs);
    IupSetAttribute(delayInput, PLUGIN_INTEGER_MAX, "5000");
    IupSetAttribute(delayInput, PLUGIN_INTEGER_MIN, "0");

    // checkboxes
    // inbound checkbox
    IupSetCallback(inboundCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheck, PLUGIN_SYNCED_VALUE, (char*)&lagInbound);
    IupSetAttribute(inboundCheck, "VALUE", "ON");

    // outbound checkbox
    IupSetCallback(outboundCheck, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheck, PLUGIN_SYNCED_VALUE, (char*)&lagOutbound);
    IupSetAttribute(outboundCheck, "VALUE", "ON");

    return (void*)box;
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------
static void lagStartUp(void) {
    lagFlushQueue();
}

static void lagCloseDown(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    (void)head; (void)tail;
    lagFlushQueue();
}

// ------------------------------------------------------------------
// Packet processing
// ------------------------------------------------------------------
static short lagProcess(ClumsyPacketNode *head, ClumsyPacketNode *tail) {
    short triggered = 0;
    DWORD now = timeGetTime();
    DWORD release = now + (DWORD)lagDelayMs;

    while (head->next != tail) {
        ClumsyPacketNode *pac = head->next;
        if (clumsy_check_direction(pac->addr.Outbound, lagInbound, lagOutbound)) {
            pac->timestamp = release;
            popNode(pac);
            lagEnqueue(pac, release);
            triggered = 1;
        } else {
            head = head->next;
        }
    }

    ClumsyPacketNode *due;
    while ((due = lagDequeueIfDue(now)) != NULL) {
        appendNode(due);
        triggered = 1;
    }

    return triggered;
}

// ------------------------------------------------------------------
// Module descriptor
// ------------------------------------------------------------------
static ClumsyModule lagModule = {
    LAG_DISPLAY_NAME,
    LAG_NAME,
    &lagEnabled,
    lagSetupUI,
    lagStartUp,
    lagCloseDown,
    lagProcess,
    0, 0, NULL
};

// ------------------------------------------------------------------
// Exported entry point
// ------------------------------------------------------------------

// Required by Windows for DLL loading
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)hInst; (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        lagFlushQueue();
    }
    return TRUE;
}