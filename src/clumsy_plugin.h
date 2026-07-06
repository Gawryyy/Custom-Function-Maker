#pragma once
/*
 * clumsy_plugin.h - Public API for clumsy custom modules
 *
 * HOW TO USE
 * ----------
 * 1. Include this header in your module .c file.
 * 2. Implement all function pointers in ClumsyModule.
 * 3. Export a function named "clumsy_get_module" that returns a pointer
 *    to your filled ClumsyModule struct.
 * 4. Compile as a DLL (see the example lag_module.c for reference).
 * 5. Drop the .dll into clumsy while it is running to load it.
 *
 * NOTES
 * ----------
 * - Your DLL runs inside the clumsy process. A crash in your module
 *   will crash clumsy. Keep your process() function fast and safe.
 * - All packet manipulation must happen through the PacketNode list.
 *   Never hold raw packet pointers after process() returns.
 * - Calling WinDivert functions directly from a plugin is fine as long
 *   as you use the same WinDivert handle (you don't get one — just
 *   manipulate the node list and let clumsy do the sending).
 * - displayName and shortName must point to static storage (string
 *   literals are fine). Do not point them at stack buffers.
 */

#ifndef CLUMSY_PLUGIN_H
#define CLUMSY_PLUGIN_H

#include <Windows.h>
#include "windivert.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ClumsyPacketNode {
    char *packet;
    UINT packetLen;
    WINDIVERT_ADDRESS addr;
    DWORD timestamp;
    struct _ClumsyPacketNode *prev;
    struct _ClumsyPacketNode *next;
} ClumsyPacketNode;

// helper
static __inline BOOL clumsy_check_direction(
    BOOL outboundPacket,
    short handleInbound,
    short handleOutbound)
{
    return (handleInbound && !outboundPacket) || (handleOutbound && outboundPacket);
}

typedef struct {
    const char *displayName;
    const char *shortName;
    short *enabledFlag;

    // modulke setup UI stuff
    void* (*setupUI)(void);

    // startup closedown stuff
    void (*startUp)(void);
    void (*closeDown)(ClumsyPacketNode *head, ClumsyPacketNode *tail);
    short (*process)(ClumsyPacketNode *head, ClumsyPacketNode *tail);

    short lastEnabled;
    short processTriggered;
    void *iconHandle;
} ClumsyModule;


#ifndef CLUMSY_BUILDING_HOST
extern ClumsyPacketNode* createNode(char *buf, UINT len, WINDIVERT_ADDRESS *addr);
extern void freeNode(ClumsyPacketNode *node);
extern ClumsyPacketNode* popNode(ClumsyPacketNode *node);
extern ClumsyPacketNode* insertBefore(ClumsyPacketNode *node, ClumsyPacketNode *target);
extern ClumsyPacketNode* insertAfter(ClumsyPacketNode *node, ClumsyPacketNode *target);
extern ClumsyPacketNode* appendNode(ClumsyPacketNode *node);
extern short isListEmpty(void);

// iup sync helperss
extern int uiSyncChance(void *ih);
extern int uiSyncToggle(void *ih, int state);
extern int uiSyncInteger(void *ih);
extern int uiSyncFixed(void *ih);
extern int uiSyncInt32(void *ih);
#endif

// Attribute key constants
#define PLUGIN_SYNCED_VALUE "__SYNCED_VALUE"
#define PLUGIN_INTEGER_MAX "__INTEGER_MAX"
#define PLUGIN_INTEGER_MIN "__INTEGER_MIN"
#define PLUGIN_FIXED_MAX "__FIXED_MAX"
#define PLUGIN_FIXED_MIN "__FIXED_MIN"

#define CLUMSY_PLUGIN_EXPORT __declspec(dllexport)
typedef ClumsyModule* (*ClumsyGetModuleFn)(void);
CLUMSY_PLUGIN_EXPORT ClumsyModule* clumsy_get_module(void);
#define CLUMSY_PLUGIN_API_VERSION 1
typedef int (*ClumsyApiVersionFn)(void);

#ifdef __cplusplus
}
#endif
#endif