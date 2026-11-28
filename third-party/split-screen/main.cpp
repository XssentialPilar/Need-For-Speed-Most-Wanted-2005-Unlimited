// NFSMW Split Screen - restores the console 2-player split screen mode on PC.
// Target: speed.exe v1.3 (English, 6,029,312 bytes)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <d3d9.h>
#include <xinput.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <cmath>
#include <initializer_list>
#include <algorithm>
#include <array>
#include "game.h"
#include "integration.hpp"
#include "../../nfs_settings.hpp"
#include "../../nfs_discx.hpp"

#define THISCALL __attribute__((thiscall))
#undef FASTCALL
#define FASTCALL __attribute__((fastcall))

namespace {

// ------------------------------------------------------------------ config / log
struct Config {
    int enabled = 1;
    int layout = 0;          // 0 = top/bottom, 1 = side by side
    int p2pad = 1;           // XInput index used by player 2
    int poolKB = 0;          // 2P car loader pool, 0 = same as 1P
    int divider = 2;         // divider thickness in pixels
    int debug = 0;
    float widen = 0.25f;
    int separateMinimaps = 1;
    int joinScreen = 1;
    int p2CareerCars = 1;
    int p2MenuInput = 1;
    float minimapScale = 0.8f;
    int stickDeadzone = 7849;
    int triggerThreshold = 30;
    int maxAI = 4;           // most AI racers selectable in split screen
    uint32_t aiIconColor = 0xFF3C3C;   // minimap tint for AI racers (RGB)
    int speedTrapScore = 1;  // speed trap score on each player's HUD
    int randomGrid = 1;      // shuffle the starting grid in split screen
    int resultsDelayMs = 4000; // split screen: time to read the race over message before the results
} cfg;

static FILE* g_log = nullptr;
static char g_dir[MAX_PATH];
static char g_ini[MAX_PATH];
static wchar_t g_ctlIni[MAX_PATH], g_ctlUserIni[MAX_PATH];

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

static void LoadConfig() {
    char* ini = g_ini; snprintf(g_ini, sizeof g_ini, "%s\\NFSMWSplitScreen.ini", g_dir);
    cfg.enabled  = GetPrivateProfileIntA("SplitScreen", "Enabled", 1, ini);
    cfg.layout   = GetPrivateProfileIntA("SplitScreen", "Layout", 0, ini);
    cfg.p2pad    = GetPrivateProfileIntA("SplitScreen", "Player2ControllerIndex", 1, ini);
    cfg.poolKB   = GetPrivateProfileIntA("SplitScreen", "TwoPlayerCarMemoryKB", 0, ini);
    cfg.divider  = GetPrivateProfileIntA("SplitScreen", "DividerPixels", 2, ini);
    cfg.debug    = GetPrivateProfileIntA("SplitScreen", "DebugLog", 0, ini);
    cfg.p2MenuInput = GetPrivateProfileIntA("SplitScreen", "Player2MenuControl", 1, ini);
    cfg.joinScreen = GetPrivateProfileIntA("SplitScreen", "JoinScreen", 1, ini);
    cfg.p2CareerCars = GetPrivateProfileIntA("SplitScreen", "Player2UsesPlayer1Cars", 1, ini);
    cfg.separateMinimaps = GetPrivateProfileIntA("SplitScreen", "SeparateMinimaps", 1, ini);
    cfg.maxAI = GetPrivateProfileIntA("SplitScreen", "MaxAIRacers", 4, ini);
    cfg.speedTrapScore = GetPrivateProfileIntA("SplitScreen", "ShowSpeedTrapScore", 1, ini);
    cfg.randomGrid = GetPrivateProfileIntA("SplitScreen", "RandomStartGrid", 1, ini);
    cfg.resultsDelayMs = GetPrivateProfileIntA("SplitScreen", "ResultsDelayMs", 4000, ini);
    { char b[32]; GetPrivateProfileStringA("SplitScreen", "AIMinimapColor", "FF3C3C", b, sizeof b, ini); cfg.aiIconColor = (uint32_t)strtoul(b, nullptr, 16) & 0xFFFFFF; }
    { char b[32]; GetPrivateProfileStringA("SplitScreen", "MinimapScale", "0.8", b, sizeof b, ini); cfg.minimapScale = (float)atof(b);
      if (cfg.minimapScale < 0.3f || cfg.minimapScale > 1.5f) cfg.minimapScale = 0.8f; }
    { char b[32]; GetPrivateProfileStringA("SplitScreen", "ViewWidening", "0.25", b, sizeof b, ini); cfg.widen = (float)atof(b);
      if (cfg.widen < 0) cfg.widen = 0; if (cfg.widen > 1) cfg.widen = 1; }
    cfg.stickDeadzone = GetPrivateProfileIntA("Player2", "StickDeadzone", 7849, ini);
    cfg.triggerThreshold = GetPrivateProfileIntA("Player2", "TriggerThreshold", 30, ini);
    GetModuleFileNameW(nullptr, g_ctlIni, MAX_PATH);
    if (auto* filename = wcsrchr(g_ctlIni, L'\\')) {
        const auto remaining = MAX_PATH - (filename + 1 - g_ctlIni);
        const auto suffix = std::swprintf(filename + 1, remaining, L"NFSMWSplitScreen_Controls.ini");
        if (suffix < 0 || suffix >= remaining) g_ctlIni[0] = 0;
    }
    unlimited_settings::UserFile(g_ctlUserIni, L"Controls");
    cfg.p2pad    = GetPrivateProfileIntA("Player2", "Device", cfg.p2pad, ini);   // -1 = keyboard

    if (cfg.p2pad < -1 || cfg.p2pad > 3) cfg.p2pad = 1;
}

// ------------------------------------------------------------------ patch helpers
static void WriteMem(uintptr_t addr, const void* data, size_t n) {
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy((void*)addr, data, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
}
static bool PatchCall(uintptr_t at, void* fn, uintptr_t expectTarget) {
    uint8_t* p = (uint8_t*)at;
    if (p[0] != 0xE8) { Log("PatchCall %08X: not a call (%02X)", at, p[0]); return false; }
    uintptr_t cur = at + 5 + *(int32_t*)(p + 1);
    if (expectTarget && cur != expectTarget) { Log("PatchCall %08X: unexpected target %08X", at, cur); return false; }
    uint8_t b[5]; b[0] = 0xE8; *(int32_t*)(b + 1) = (int32_t)((uintptr_t)fn - (at + 5));
    WriteMem(at, b, 5);
    return true;
}
// Replace a call and return whatever it pointed to before (so other mods' hooks keep working)
static uintptr_t HookCall(uintptr_t at, void* fn, uintptr_t expectOriginal) {
    uint8_t* p = (uint8_t*)at;
    if (p[0] != 0xE8) { Log("HookCall %08X: not a call (%02X) - skipped", at, p[0]); return 0; }
    uintptr_t cur = at + 5 + *(int32_t*)(p + 1);
    if (cur != expectOriginal) Log("HookCall %08X: already hooked by another mod (-> %08X), chaining", at, cur);
    uint8_t b[5]; b[0] = 0xE8; *(int32_t*)(b + 1) = (int32_t)((uintptr_t)fn - (at + 5));
    WriteMem(at, b, 5);
    return cur;
}
// ------------------------------------------------------------------ bigger frame memory
// The game's per-frame scratch heap (FrameMalloc) is 400 KB. Two views plus a full
// pursuit (a dozen cops, roadblocks, spikes, flares) run it dry, and some renderers
// (RenderTextureHeadlights) then write through a null pointer. Swap in larger buffers.
static const uint32_t kFrameMemSize = 0x400000;   // 4 MB per buffer
static uint8_t* g_fmBuf[2] = {}; static int g_fmIdx = 1; static uint32_t g_fmLogged = 0;
static void Hook_SwapFrameMalloc() {
    uint8_t*& bufCur = *(uint8_t**)0x915128;  uint8_t*& bufOther = *(uint8_t**)0x91512C;
    uint8_t*& pos = *(uint8_t**)0x915F84;
    uint32_t& failed = *(uint32_t*)0x915F94, &failAmt = *(uint32_t*)0x915F98, &maxFail = *(uint32_t*)0x915F9C;
    int& largest = *(int*)0x915FA0;
    if (failed && failAmt > maxFail) maxFail = failAmt;
    int used = (int)(pos - bufCur);
    failed = 0; failAmt = 0;
    if (used > largest) largest = used;
    *(int*)0x915FA4 = used;
    *(int*)0x915124 = *(int*)0x915120; *(int*)0x915120 = used;
    g_fmIdx ^= 1;
    uint8_t* next = g_fmBuf[g_fmIdx];
    bufOther = bufCur; bufCur = next;                       // last frame's buffer stays intact for one more frame
    *(uint8_t**)0x915F80 = next; pos = next; *(uint8_t**)0x915F88 = next + kFrameMemSize;
    *(uint32_t*)0x915F7C = kFrameMemSize; *(uint32_t*)0x915F90 = 0;
    if ((uint32_t)largest >= g_fmLogged + 0x80000) { g_fmLogged = (uint32_t)largest & ~0x7FFFFu; Log("Frame memory: peak use %u KB of %u KB", (unsigned)largest / 1024, kFrameMemSize / 1024); }
}
static void ExpandFrontendRenderStorage() {
    // cFEngRender shipped with 180 matrix slots, indexed by live FE objects.
    // Car selector transitions and four complete race HUDs can exceed that capacity.
    constexpr uint32_t slotBytes = 0xB0;
    constexpr uint32_t renderSlots = 4096;
    struct Immediate { uintptr_t address; uint32_t stock, replacement; };
    constexpr Immediate changes[] = {
        {0x54EB2E, 0x7BD0, 0x10 + renderSlots * slotBytes},
        {0x591B36, 0x1EF0, renderSlots * slotBytes / sizeof(uint32_t)},
        {0x591AFC, 0x180, 2048},
        {0x591B15, 0x348, 4096},
    };
    for (const auto& change : changes)
        if (memcmp(reinterpret_cast<const void*>(change.address), &change.stock, sizeof(change.stock))) return;
    for (const auto& change : changes)
        WriteMem(change.address, &change.replacement, sizeof(change.replacement));
    Log("Front-end render storage: %u matrix slots", renderSlots);
}

static void InstallFrameMemory() {
    for (int i = 0; i < 2; i++) g_fmBuf[i] = (uint8_t*)VirtualAlloc(nullptr, kFrameMemSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_fmBuf[0] || !g_fmBuf[1]) { Log("Frame memory: allocation failed, game default kept"); return; }
    if (!HookCall(0x6E7641, (void*)Hook_SwapFrameMalloc, 0x4FAD80)) return;
    // RenderTextureHeadlights: when frame memory runs out, skip the draw instead of writing to null
    uint8_t* p = (uint8_t*)0x7381A0;
    if (p[0] == 0xA1 && *(uint32_t*)(p + 1) == 0x915F98) {
        uint8_t b[5] = { 0xE9 }; *(int32_t*)(b + 1) = (int32_t)(0x7382F5 - (0x7381A0 + 5)); WriteMem(0x7381A0, b, 5);
    }
    Log("Frame memory: %u KB x2", kFrameMemSize / 1024);
}
static uintptr_t o_IOInit = 0x63CD00, o_FEInitInput = 0x516D70, o_AddOption = 0x573960,
                 o_DisplayInner = 0x6DE300, o_FERender = 0x516F70, o_OptionsAddOption = 0x573960;
template <class T> static inline T& At(uintptr_t a) { return *(T*)a; }
template <class T> static inline T& At(uint8_t* a, int off) { return *(T*)(a + off); }

static bool LocalQuickRace(const uint8_t* db) {
    if (!db) return false;
    constexpr uint32_t raceModes = 1 | 2 | 4 | 8 | 64; // career, challenge, quick race, online, LAN
    return (*reinterpret_cast<const uint32_t*>(db + G::FEDB_GAMEMODE) & raceModes) == 4;
}

static bool SplitRaceSetup() {
    const auto* database = *G::pFEDatabase;
    return LocalQuickRace(database) && database[G::FEDB_NUMPLAYERS] == 2;
}

// ------------------------------------------------------------------ player 2 input device
// Mimics the engine's InputDevice layout: vtable at +0, channel table at +0x18,
// previous values at +0x1c, current values at +0x20.
struct Channel { int type; uint32_t hash; float* prev; float* cur; };
struct P2Device {
    void** vtbl;
    uint8_t pad0[0x14];
    Channel* channels;   // +0x18
    float* prev;         // +0x1c
    float* cur;          // +0x20
    int port;            // +0x24
    int player;          // 0 = player 1, 1 = player 2
    uint8_t pad1[0x40];
};
static P2Device* g_p2dev = nullptr;
static int g_p2port = -1, g_p1port = -1;

typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE*);
#include "sdl_input.inc"
static const XInputGetStateFn pXInputGetState = PollGamepad;

static void* FASTCALL Dev_Dtor(P2Device* self, void*, int) { return self; }
static bool FASTCALL Dev_True(P2Device*, void*) { return true; }
static int FASTCALL Dev_Zero(P2Device*, void*) { return 0; }
static void FASTCALL Dev_Nop(P2Device*, void*) {}
static int FASTCALL Dev_NumChannels(P2Device*, void*) { return G::NUM_GAME_ACTIONS; }
static void FASTCALL Dev_Init(P2Device* self, void*) {
    for (int i = 0; i < G::NUM_GAME_ACTIONS; i++) {
        self->channels[i].type = 1;
        self->channels[i].hash = G::stringhash32(G::GameActionNames[i * 13]);
        self->channels[i].prev = &self->prev[i];
        self->channels[i].cur  = &self->cur[i];
    }
}

static float Axis(SHORT v, int dz, int sign) {
    int x = v * sign;
    if (x <= dz) return 0.f;
    float f = (float)(x - dz) / (32767.f - dz);
    return f > 1.f ? 1.f : f;
}

#include "p2input.inc"

static void* g_devVtbl[24];
static P2Device* AddPlayerDevice(int player) {
    int& count = At<int>(G::pIOModule, 4);
    if (count <= 0 || count >= 15) { Log("IOModule not ready (count=%d)", count); return nullptr; }
    P2Device* d = (P2Device*)calloc(1, sizeof(P2Device));
    d->channels = (Channel*)calloc(G::NUM_GAME_ACTIONS, sizeof(Channel));
    d->prev = (float*)calloc(256, sizeof(float));
    d->cur  = (float*)calloc(256, sizeof(float));
    if (!g_devVtbl[0]) {
        for (int i = 0; i < 24; i++) g_devVtbl[i] = (void*)Dev_Zero;
        g_devVtbl[0] = (void*)Dev_Dtor;
        g_devVtbl[1] = (void*)Dev_True;      // IsConnected
        g_devVtbl[3] = (void*)Dev_Init;      // Initialize
        g_devVtbl[4] = (void*)Dev_Poll;      // Poll
        g_devVtbl[5] = (void*)Dev_NumChannels;
        g_devVtbl[6] = (void*)Dev_Nop;
        g_devVtbl[7] = (void*)Dev_Nop;
        g_devVtbl[10] = (void*)G::InputDevice_HasChanged;
        g_devVtbl[11] = (void*)G::InputDevice_AnyNonZero;
        g_devVtbl[12] = (void*)G::InputDevice_Snapshot;
        g_devVtbl[13] = (void*)G::InputDevice_Restore;
    }
    d->vtbl = g_devVtbl;
    d->port = count;
    d->player = player;
    Dev_Init(d, nullptr);
    At<P2Device*>(G::pIOModule, 8 + count * 4) = d;
    At<uint32_t>(G::pIOModule, 0x48) |= 1u << count;
    count++;
    Log("Player %d input device added at port %d", player + 1, d->port);
    return d;
}
static int g_quadPort[2] = { -1, -1 };    // input ports of players 3 and 4
static void AddPlayer2Device() {
    if (g_p2dev) return;
    g_p2dev = AddPlayerDevice(1); if (g_p2dev) g_p2port = g_p2dev->port;   // port 1 (also feeds menus)
    P2Device* p1 = AddPlayerDevice(0); if (p1) g_p1port = p1->port;         // port 2
    for (int k = 0; k < 2; k++) { P2Device* d = AddPlayerDevice(2 + k); if (d) g_quadPort[k] = d->port; }   // ports 3, 4
}

// Split screen races: player 1 drives with the mod's device too, so each player's saved
// controls apply whatever controller (or keyboard) they joined with.
static uintptr_t o_CreatePlayers = 0x6F2700;
static void FASTCALL Hook_CreatePlayers(void* game, void*) {
    uint8_t* db = *G::pFEDatabase;
    bool split = *G::pSimUserMode == 1 && SplitRaceSetup() && g_p1port >= 0 && g_p2port >= 0;
    int8_t old0 = 0;
    if (split) {
        old0 = At<int8_t>(db, G::FEDB_JOYPORT0);
        At<int8_t>(db, G::FEDB_JOYPORT0) = (int8_t)g_p1port;
        At<int8_t>(db, G::FEDB_JOYPORT1) = (int8_t)g_p2port;
        Log("Creating players: P1 port %d (device %d), P2 port %d (device %d)", g_p1port, g_dev[0], g_p2port, g_dev[1]);
    }
    ((void(THISCALL*)(void*))o_CreatePlayers)(game);
    if (split) At<int8_t>(db, G::FEDB_JOYPORT0) = old0;
}

// In a race, each player's InputPlayer maps device channels to car controls with a layout picked from
// that player's PlayerSettings (GetControllerAttribs(1): the Options -> Controls config +0x08 and
// layout flag +0x07, 12 layouts at 0x89F6AC). The mod's devices already deliver every action on its
// own channel, so only the default layout (index 1, as PlayerSettings::Default) fits. With another one
// picked in the profile (e.g. a different controller config in single player), that player's
// accelerate went to the wrong channel: the car only reversed. Split screen players always get the default.
static unsigned FASTCALL Hook_GetControllerAttribs(void* settings, void*, int attrib, bool b) {
    unsigned r = ((unsigned(THISCALL*)(void*, int, bool))0x56D4B0)(settings, attrib, b);
    if (attrib != 1 || !cfg.enabled || *G::pSimUserMode != 1) return r;
    unsigned def = *(unsigned*)(0x89F6AC + 4);
    static void* logged[2] = {};
    if (r != def) {
        int k = 0; for (; k < 2 && logged[k] && logged[k] != settings; k++) {}
        if (k < 2 && !logged[k]) {
            logged[k] = settings;
            uint8_t* s = (uint8_t*)settings;
            Log("Player controls layout %08X (config %d, flag %d) from the profile -> default %08X", r, At<int>(s, 8), s[7], def);
        }
    }
    return def;
}
typedef void (THISCALL *IOInitFn)(void* io);
static void InstallLateHooks();
static void FASTCALL Hook_IOModuleInit(void* io, void*) {
    ((IOInitFn)o_IOInit)(io);
    InstallLateHooks();
    Log("IOModule initialized with %d devices", At<int>(G::pIOModule, 4));
    AddPlayer2Device();
}

// "Press Start" screens: on PC the Start action is bound to Space, so also accept Enter
typedef void (THISCALL *NotifyFn)(void* self, uint32_t msg, void* obj, uint32_t p1, uint32_t p2);
static void FASTCALL Hook_PressStartNotify(void* self, void*, uint32_t msg, void* obj, uint32_t p1, uint32_t p2) {
    if (SplitRaceSetup() && msg == 0xC12E9E27 /*__PAD_ACCEPT_RELEASED__*/) msg = 0xEBFCDA65 /*__PAD_START_RELEASED__*/;
    ((NotifyFn)0x7A3F20)(self, msg, obj, p1, p2);
}

// The PC front end never sends controller input to PressStart.fng, so skip that screen:
// redirect switches to it straight to the matching destination.
typedef void (THISCALL *QPSFn)(void* fe, const char* name, int arg, uint32_t mask, int b);
static QPSFn g_qpsTramp = nullptr;
static void StartJoinScreen();
static void StartCustomPlayerChoice();
static bool FourRace();
static void FourCarSelReset();
static void CaptureFourPlayerOne();
static int g_carSelectPlayer = -1;
static char g_lastPkg[64];

struct CustomTrackSwitch {
    void* frontend;
    const char* package;
    int arg;
    uint32_t mask;
    int flags;
};
static CustomTrackSwitch customTrackSwitch{};
// Player 2's car select creates a fresh multiplayer profile for player 2 (CreateMultiplayerProfile,
// called from the UIQRCarSelect constructor) with only the stock cars, so there are no career or
// My Cars entries. Right after it is created, give player 2 a copy of player 1's car stable
// (FEPlayerCarDB, profile + 0x414, 0x8CC8 bytes: car records, customizations, career records).
static void CopyCarStableToP2() {
    uint8_t* db = *G::pFEDatabase;
    if (!db) { Log("Player 2 car stable: no FE database"); return; }
    uint8_t* p0 = At<uint8_t*>(db, 0x10); uint8_t* p1 = At<uint8_t*>(db, 0x14);
    if (!p0 || !p1 || p0 == p1) { Log("Player 2 car stable: profiles %p %p, not copied", p0, p1); return; }
    int n0 = 0, n1 = 0;
    for (int i = 0; i < 200; i++) { if (At<uint32_t>(p0, 0x414 + i * 0x14) != 0xFFFFFFFF) n0++; if (At<uint32_t>(p1, 0x414 + i * 0x14) != 0xFFFFFFFF) n1++; }
    memcpy(p1 + 0x414, p0 + 0x414, 0x8CC8);
    Log("Player 2 car stable: copied from player 1 (profiles %p %p, cars %d -> was %d)", p0, p1, n0, n1);
}
static uintptr_t o_CreateMPProfile = 0x5ABA50;
static void FASTCALL Hook_CreateMPProfile(void* db, void*, int idx) {
    ((void(THISCALL*)(void*, int))o_CreateMPProfile)(db, idx);
    if (idx == 1 && cfg.p2CareerCars) CopyCarStableToP2();
}
static void FASTCALL Hook_QueuePackageSwitch(void* fe, void*, const char* name, int arg, uint32_t mask, int b) {
    if (name && cfg.enabled) {
        if (SplitRaceSetup() && !_stricmp(name, "PressStart.fng")) {
            const char* to = name; int a = arg; uint32_t m = mask;
            if (arg == 0 && mask == 0 && cfg.joinScreen) { StartJoinScreen(); to = name; }    // forward: our join screen
            else if (arg == 0 && mask == 0) { to = "Car_Select.fng"; a = 0; m = 7; }       // forward from Track_Options
            else if (arg == 0) {                                                             // back from P1 car select
                uint8_t* db = *G::pFEDatabase;
                bool trackSel = db && (At<uint32_t>(db, G::FEDB_GAMEMODE) & 4) && db[0] == 2 && At<int>(db, 0x18) == 2;
                to = trackSel ? "Track_Select.fng" : "Track_Options.fng"; a = 0; m = 0;
            }
            else if (g_carSelectPlayer == 1) { to = "Car_Select.fng"; a = 0; m = 7; }      // back from P2 car select
            else {
                CaptureFourPlayerOne();
                to = "Car_Select.fng"; a = 1; m = 7;
            }
            Log("PressStart.fng(%d,%X) -> %s(%d,%X)", arg, mask, to, a, m);
            name = to; arg = a; mask = m;
        }
        if (!_stricmp(name, "Car_Select.fng")) { g_carSelectPlayer = arg; if (arg == 0) FourCarSelReset(); }
        // split screen race: menus (pause, options, ...) accept every player's controller
        if (cfg.p2MenuInput && SplitRaceSetup() && *G::pSimUserMode == 1 && *G::pGameFlowState == 6) mask |= 7;
    }
    // The native jukebox may be entered through any of the carousel's input paths.
    if (name && !_stricmp(name, "EA_Trax_Jukebox.fng")) unlimited_discx::SelectStockMusic();
    if (name) snprintf(g_lastPkg, sizeof g_lastPkg, "%s", name);
    g_qpsTramp(fe, name, arg, mask, b);
}
// the pause menu is pushed for the controller of the player who paused; open it to both players
typedef void (THISCALL *QPushFn)(void* fe, const char* name, int arg, uint32_t mask, int b);
static uintptr_t o_PausePush = 0x5257F0;
static void FASTCALL Hook_PausePush(void* fe, void*, const char* name, int arg, uint32_t mask, int b) {
    if (cfg.p2MenuInput && SplitRaceSetup() && *G::pSimUserMode == 1) mask |= 7;
    ((QPushFn)o_PausePush)(fe, name, arg, mask, b);
}
static void InstallQPSDetour() {
    uint8_t* f = (uint8_t*)0x525940;
    const uint8_t expect[7] = { 0x83, 0x3D, 0x90, 0x5E, 0x92, 0x00, 0x06 };
    uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    if (!memcmp(f, expect, 7)) {
        memcpy(t, expect, 7);
        t[7] = 0xE9; *(int32_t*)(t + 8) = (int32_t)(0x525947 - ((uintptr_t)t + 12));
    } else if (f[0] == 0xE9) {   // another mod detoured it: chain to their jump
        uintptr_t other = 0x525940 + 5 + *(int32_t*)(f + 1);
        t[0] = 0xE9; *(int32_t*)(t + 1) = (int32_t)(other - ((uintptr_t)t + 5));
        Log("QueuePackageSwitch already detoured by another mod, chaining");
    } else {
        VirtualFree(t, 0, MEM_RELEASE);
        Log("QueuePackageSwitch: unexpected prologue");
        return;
    }
    g_qpsTramp = (QPSFn)t;
    uint8_t j[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    *(int32_t*)(j + 1) = (int32_t)((uintptr_t)Hook_QueuePackageSwitch - (0x525940 + 5));
    WriteMem(0x525940, j, 7);
}

static void FASTCALL SelectCustomTrack(void* fe, void*, const char* package, int arg, uint32_t mask, int flags) {
    const auto* database = *G::pFEDatabase;
    // The stock local Track Select screen queues its options panel here.
    // Tollbooth has no two-player race setup.
    if (cfg.enabled && package && !_stricmp(package, "Track_Options.fng") && arg &&
        g_p1port >= 0 && g_p2port >= 0 && *G::pGameFlowState == 3 && *G::pQRMode == 1 &&
        LocalQuickRace(database) && database[G::FEDB_NUMPLAYERS] == 1 &&
        *reinterpret_cast<const int*>(database + 0x18) != 4) {
        customTrackSwitch = {fe, package, arg, mask, flags};
        StartCustomPlayerChoice();
        return;
    }
    Hook_QueuePackageSwitch(fe, nullptr, package, arg, mask, flags);
}

static void InstallCustomTrackChoice() {
    if (g_qpsTramp) PatchCall(0x7C1759, reinterpret_cast<void*>(&SelectCustomTrack), 0x525940);
}

// PC's cFEngJoyInput only allocates one ActionQueue slot, but the split screen code indexes
// it with player 2's port. Grow it to 5 slots with real queues for ports 1..3.
typedef void* (THISCALL *AQCtorFn)(void* self, int port, uint32_t cfg, const char* name, int b);
typedef void (THISCALL *AQEnableFn)(void* self, int en);
static void Hook_FEInitInput() {
    ((G::VoidFn)o_FEInitInput)();
    void** inst = *(void***)0x91CAD0;
    if (!inst) return;
    void** n = (void**)calloc(8, sizeof(void*));
    n[0] = inst[0];
    for (int port = 1; port <= 3; port++) {
        void* q = G::GameNew(0x28C);
        if (!q) continue;
        memset(q, 0, 0x28C);
        ((AQCtorFn)0x648DB0)(q, port, 0x82D21520, "FEng", 0);
        ((AQEnableFn)0x644060)(q, 1);
        n[port] = q;
    }
    n[4] = inst[0];
    *(void***)0x91CAD0 = n;
    Log("FE joy input expanded to 4 ports");
}

// ------------------------------------------------------------------ quick race menu option
static void SetLocalPlayerCount(int count);
// Split screen starts with every player on the near chase camera (PlayerSettings +0xC = 2,
// POV_OUTSIDE_CLOSE; FEDatabase +0x10 -> profile, +0x80: the two players' settings, 0x14 bytes each).
// Player 1's own camera (saved in the profile) is put back when the Quick Race menu opens again.
static int g_p1CamSaved = -1;
static void QuadCamerasNear();
static void SplitCamerasNear() {
    uint8_t* db = *G::pFEDatabase;
    uint8_t* prof = db ? At<uint8_t*>(db, 0x10) : nullptr;
    if (!prof) return;
    if (g_p1CamSaved < 0) g_p1CamSaved = At<int>(prof, 0x80 + 0xC);
    At<int>(prof, 0x80 + 0xC) = 2; At<int>(prof, 0x80 + 0x14 + 0xC) = 2;
    QuadCamerasNear();
}
static void RestoreP1Camera() {
    uint8_t* db = *G::pFEDatabase;
    uint8_t* prof = db ? At<uint8_t*>(db, 0x10) : nullptr;
    if (!prof || g_p1CamSaved < 0) return;
    At<int>(prof, 0x80 + 0xC) = g_p1CamSaved;
    Log("Player 1's own camera (%d) back", g_p1CamSaved);
    g_p1CamSaved = -1;
}
template<int players>
static void FASTCALL SelectLocalSplit(void*, void*, const char*, uint32_t data, void*, uint32_t, uint32_t) {
    if (data == G::BUTTON_PRESSED) {
        SetLocalPlayerCount(players);
        *G::pQRMode = 2;
        uint8_t* db = *G::pFEDatabase;
        if (db && g_p2port >= 0) At<int8_t>(db, G::FEDB_JOYPORT1) = (int8_t)g_p2port;
        SplitCamerasNear();
        Log("Local split screen selected (%d players)", players);
    }
}

static const uint32_t STR_THREEPLAYER = 0x5C2B000B;
static const uint32_t STR_FOURPLAYER = 0x5C2B0006;
static const uint32_t TEX_FOURPLAYER = 0x5C2B0007;
static const uint32_t TEX_TWOPLAYER = 0x5C2B0012;
static const uint32_t TEX_THREEPLAYER = 0x5C2B0013;
static const uint32_t TEX_DISCX = 0x5C2B0014;
static const uint32_t STR_DISCX = 0x5C2B0015;

// ------------------------------------------------------------------ 4 player menu icon
// Custom FE textures use the same TextureInfo and platform metadata as
// the stock icon scroller. The language glyph is baked from its SVG source.
#include "../../artwork/menu_icons_alpha.inc"
#include "../../artwork/languages_alpha.inc"
static IDirect3DDevice9* Dev();
using GetTexInfoFn = void* (__cdecl*)(uint32_t, int, int);
static GetTexInfoFn g_gtiTramp = nullptr;

struct EmbeddedIcon {
    alignas(16) uint8_t info[0x80]{}, platform[0x20]{};
    const uint8_t* base = nullptr;
    IDirect3DTexture9* texture = nullptr;
};
static EmbeddedIcon g_splitIcons[3], g_languageIcon, g_discxIcon;
static std::shared_ptr<const unlimited_discx::Cover> g_discCover;
static constexpr uint32_t splitIconHashes[] = {TEX_TWOPLAYER, TEX_THREEPLAYER, TEX_FOURPLAYER};

template<class Pixel>
static bool MakeIconTexture(IDirect3DDevice9* device, EmbeddedIcon& icon, const Pixel* source, unsigned edge) {
    if (icon.texture) return true;
    IDirect3DTexture9* texture = nullptr;
    if (!device || FAILED(device->CreateTexture(edge, edge, 0, 0, D3DFMT_A8R8G8B8,
                                                D3DPOOL_MANAGED, &texture, nullptr))) return false;
    std::array<Pixel, 128 * 128> pixels;
    std::copy_n(source, edge * edge, pixels.begin());
    for (DWORD level = 0; level < texture->GetLevelCount(); ++level) {
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(level, &locked, nullptr, 0))) {
            texture->Release();
            return false;
        }
        for (unsigned y = 0; y < edge; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch);
            for (unsigned x = 0; x < edge; ++x) {
                const auto pixel = pixels[y * edge + x];
                if constexpr (sizeof(Pixel) == 1) row[x] = (static_cast<uint32_t>(pixel) << 24) | 0xffffffu;
                else row[x] = pixel;
            }
        }
        if (FAILED(texture->UnlockRect(level))) {
            texture->Release();
            return false;
        }
        const auto half = edge / 2;
        for (unsigned y = 0; y < half; ++y) for (unsigned x = 0; x < half; ++x) {
            const auto i = 2*y*edge + 2*x;
            const auto a = pixels[i], b = pixels[i + 1], c = pixels[i + edge], d = pixels[i + edge + 1];
            if constexpr (sizeof(Pixel) == 1) {
                pixels[y * half + x] = static_cast<uint8_t>((a + b + c + d + 2) / 4);
            } else {
                uint32_t pixel = 0;
                for (unsigned shift = 0; shift < 32; shift += 8) {
                    const auto sum = ((a >> shift) & 255) + ((b >> shift) & 255) + ((c >> shift) & 255) + ((d >> shift) & 255);
                    pixel |= ((sum + 2) / 4) << shift;
                }
                pixels[y * half + x] = pixel;
            }
        }
        edge = half;
    }
    icon.texture = texture;
    return true;
}

static void* RegisterEmbeddedTexture(EmbeddedIcon& icon, const uint8_t* base, uint32_t hash, const char* name) {
    if (base != icon.base) {
        memcpy(icon.info, base, 0x7c);
        memcpy(icon.platform, *reinterpret_cast<void* const*>(base), sizeof(icon.platform));
        auto* platform = icon.platform;
        auto* info = icon.info;
        At<void*>(platform, 0) = At<void*>(platform, 4) = platform;
        At<uint32_t>(platform, 0x14) = D3DFMT_A8R8G8B8;
        At<IDirect3DTexture9*>(platform, 0x18) = icon.texture;
        At<void*>(info, 0) = platform;
        std::fill_n(info + 0xc, 0x18, 0);
        snprintf(reinterpret_cast<char*>(info + 0xc), 0x18, "%s", name);
        At<uint32_t>(info, 0x24) = hash;
        At<uint32_t>(info, 0x2c) = 0;
        At<void*>(info, 0x6c) = info;
        At<void*>(info, 0x70) = At<void*>(info, 0x74) = nullptr;
        icon.base = base;
        D3DSURFACE_DESC desc{};
        if (SUCCEEDED(icon.texture->GetLevelDesc(0, &desc)))
            Log("Frontend texture ready: %s (%ux%u px)", name, desc.Width, desc.Height);
    }
    return icon.info;
}

template<class Pixel>
static void* GetEmbeddedIcon(EmbeddedIcon& icon, uint32_t baseHash, uint32_t hash, const char* name,
                             const Pixel* coverage, unsigned edge) {
    const auto* base = static_cast<const uint8_t*>(g_gtiTramp(baseHash, 0, 0));
    if (!base || !*reinterpret_cast<void* const*>(base) || !MakeIconTexture(Dev(), icon, coverage, edge))
        return nullptr;
    return RegisterEmbeddedTexture(icon, base, hash, name);
}

#include "discx_tab.inc"

static void* __cdecl Hook_GetTextureInfo(uint32_t hash, int useDefault, int b) {
    constexpr const uint8_t* splitCoverage[] = {splitTwoAlpha, splitThreeAlpha, splitFourAlpha};
    constexpr const char* splitNames[] = {"MODE_ICON_SPLIT_2", "MODE_ICON_SPLIT_3", "MODE_ICON_SPLIT_4"};
    for (size_t i = 0; i < std::size(splitIconHashes); ++i) {
        if (hash != splitIconHashes[i]) continue;
        if (auto* icon = GetEmbeddedIcon(g_splitIcons[i], G::TEX_SPLITSCREEN, hash, splitNames[i], splitCoverage[i], 128))
            return icon;
        return g_gtiTramp(G::TEX_SPLITSCREEN, useDefault, b);
    }
    if (hash == TEX_DISCX) {
        constexpr uint32_t stockIcon = 0xc52ccbf6;
        if (auto* icon = GetEmbeddedIcon(g_discxIcon, stockIcon, hash, "OPTIONS_ICON_DISCX", discxIconAlpha, 128))
            return icon;
        hash = stockIcon;
    } else if (hash == unlimited_split_screen::discxNoticeCoverHash) {
        constexpr auto original = unlimited_split_screen::discxCoverBase;
        static EmbeddedIcon cover;
        static std::shared_ptr<const unlimited_discx::Cover> loaded;
        if (g_discCover && loaded != g_discCover) {
            EmbeddedIcon replacement;
            if (MakeIconTexture(Dev(), replacement, g_discCover->data(), unlimited_discx::coverEdge)) {
                if (cover.texture) cover.texture->Release();
                cover.texture = replacement.texture;
                cover.base = nullptr;
                loaded = g_discCover;
            }
        }
        if (g_discCover && loaded == g_discCover) {
            if (auto* icon = GetEmbeddedIcon(cover, original, hash, "DISCX_HUD_COVER",
                                            loaded->data(), unlimited_discx::coverEdge))
                return icon;
        }
        hash = original;
    } else if (hash == unlimited_split_screen::discxNoticeEmptyHash) {
        constexpr uint32_t stockBadge = 0x000004e5;
        static EmbeddedIcon empty;
        constexpr uint8_t transparent[8 * 8]{};
        if (auto* icon = GetEmbeddedIcon(empty, stockBadge, hash, "DISCX_HUD_EMPTY", transparent, 8))
            return icon;
        hash = stockBadge;
    } else if (hash == unlimited_split_screen::discxNoticeTabHash) {
        constexpr uint32_t stockTab = 0x575221d4;
        struct DiscTab {
            EmbeddedIcon icon;
            IDirect3DTexture9* source = nullptr;
        };
        static DiscTab menuTab, raceTab;
        auto& tab = *G::pGameFlowState == 3 ? menuTab : raceTab;
        auto* base = static_cast<uint8_t*>(g_gtiTramp(stockTab, 0, 0));
        auto* source = FindNativeTexture(base);
        try {
            if (source) {
                if (tab.source != source || tab.icon.base != base) {
                    if (!BuildDiscTab(tab.icon, source)) return base;
                    tab.source = source;
                    tab.icon.base = nullptr;
                }
                return RegisterEmbeddedTexture(tab.icon, base, hash, "DISCX_HUD_TAB");
            }
        } catch (const std::bad_alloc&) {}
        hash = stockTab;
    } else if (hash == unlimited_split_screen::languageIconHash) {
        constexpr uint32_t stockIcon = 0x4df98fb2;
        if (auto* icon = GetEmbeddedIcon(g_languageIcon, stockIcon, hash, "OPTIONS_ICON_LANGUAGES", languageIconAlpha, 128))
            return icon;
        hash = stockIcon;
    }
    return g_gtiTramp(hash, useDefault, b);
}
static void InstallGetTextureInfoDetour() {
    uint8_t* f = (uint8_t*)0x503400;
    const uint8_t expect[8] = { 0x53, 0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14 };
    uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    if (!memcmp(f, expect, 8)) {
        memcpy(t, expect, 8);
        t[8] = 0xE9; *(int32_t*)(t + 9) = (int32_t)(0x503408 - ((uintptr_t)t + 13));
    } else if (f[0] == 0xE9) {
        uintptr_t other = 0x503400 + 5 + *(int32_t*)(f + 1);
        t[0] = 0xE9; *(int32_t*)(t + 1) = (int32_t)(other - ((uintptr_t)t + 5));
        Log("GetTextureInfo already detoured by another mod, chaining");
    } else { Log("GetTextureInfo: unexpected prologue, 4P icon off"); return; }
    g_gtiTramp = (GetTexInfoFn)t;
    uint8_t j[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
    *(int32_t*)(j + 1) = (int32_t)((uintptr_t)Hook_GetTextureInfo - (0x503400 + 5));
    WriteMem(0x503400, j, 8);
}
typedef void (THISCALL *AddOptionFn)(void* menu, void* opt);
typedef void* (THISCALL *IconOptionCtorFn)(void* self, uint32_t tex, uint32_t name, uint32_t desc);

static void FASTCALL Hook_QRAddOption(void* menu, void*, void* opt) {
    ((AddOptionFn)o_AddOption)(menu, opt);  // CustomRace
    RestoreP1Camera();
    if (!cfg.enabled || g_p1port < 0 || g_p2port < 0 || !g_qpsTramp) return;
    static void* callbacks[][2] = {
        {((void**)G::CustomRace_vtable)[0], (void*)SelectLocalSplit<2>},
        {((void**)G::CustomRace_vtable)[0], (void*)SelectLocalSplit<3>},
        {((void**)G::CustomRace_vtable)[0], (void*)SelectLocalSplit<4>}
    };
    const uint32_t labels[] = {G::STR_SPLITSCREEN, STR_THREEPLAYER, STR_FOURPLAYER};
    const int slots = g_quadPort[0] < 0 ? 1 : g_quadPort[1] < 0 ? 2 : 3;
    for (int i = 0; i < slots; ++i) {
        auto* option = G::GameNew(0x4C);
        if (!option) break;
        const auto icon = g_gtiTramp ? splitIconHashes[i] : G::TEX_SPLITSCREEN;
        ((IconOptionCtorFn)G::IconOption_ctor)(option, icon, labels[i], 0);
        *(void***)option = callbacks[i];
        ((AddOptionFn)G::IconScrollerMenu_AddOption)(menu, option);
    }
}

// ------------------------------------------------------------------ rendering
static IDirect3DDevice9* Dev() { return (IDirect3DDevice9*)*G::ppD3DDevice; }
static IDirect3DTexture9* g_tex[4] = {};
static UINT g_texW = 0, g_texH = 0; static D3DFORMAT g_texFmt = D3DFMT_UNKNOWN;
static IDirect3DStateBlock9* g_sb = nullptr;
struct SplitWorldTarget {
    IDirect3DTexture9* image = nullptr;
    IDirect3DSurface9* resolved = nullptr;
    IDirect3DSurface9* color = nullptr;
    IDirect3DSurface9* depth = nullptr;
    IDirect3DSurface9* destination = nullptr;
    UINT width = 0, height = 0;

    void Release() {
        if (color && color != resolved) color->Release();
        if (resolved) resolved->Release();
        if (image) image->Release();
        if (depth) depth->Release();
        *this = {};
    }
};
// Three players use two distinct viewport sizes.
static SplitWorldTarget g_worldTargets[2];
static SplitWorldTarget g_soloWorldTarget;
static float g_renderScale = 1.f;
static int g_filtersPreference = -1;
static int g_blurPreference = -1;
static int g_frame = 0;
static int g_pass = 0;          // 0 = normal, 1 = an intermediate player pass, 2 = the last pass
static int g_passSlot = 0;      // capture slot of the pass being drawn
static bool g_quadRender = false;
static int g_splitViews = 2;
static int g_localPlayerCount = 2;

struct SplitPane { float x, y, w, h; };
static SplitPane PlayerPane(int slot) {
    if (g_splitViews == 3 && slot == 2) return {0, .5f, 1, .5f};
    if (g_splitViews >= 3) return {(slot & 1) * .5f, (slot >> 1) * .5f, .5f, .5f};
    if (cfg.layout == 1) return {slot * .5f, 0, .5f, 1};
    return {0, slot * .5f, 1, .5f};
}
static void QuadTick();
static void QuadFindHudPackages();
static void RestoreSplitCamera();
static bool g_captured[4];
static bool g_wasSplit = false;

struct SplitPostTargets {
    IDirect3DTexture9* images[2]{};
    IDirect3DSurface9* surfaces[2]{};
    IDirect3DTexture9* source[2]{};
    bool ready = false;

    void Release() {
        for (auto* surface : surfaces) if (surface) surface->Release();
        for (auto* image : images) if (image) image->Release();
        *this = {};
    }
};

static SplitPostTargets g_postTargets[3];
static bool g_splitHistoryActive = false;

static void SwapPostTargets(SplitPostTargets& post) {
    for (int i = 0; i < 2; ++i) {
        std::swap(At<IDirect3DTexture9*>(0x93DEF8 + 4*i), post.images[i]);
        std::swap(At<IDirect3DSurface9*>(0x93DE60 + 4*i), post.surfaces[i]);
    }
}

static void BeginPlayerPostTargets(int slot) {
    if (!slot) return;
    auto* device = Dev();
    if (!device) return;
    auto& post = g_postTargets[slot - 1];
    auto* a = At<IDirect3DTexture9*>(0x93DEF8);
    auto* b = At<IDirect3DTexture9*>(0x93DEFC);
    if (!a || !b) return; // The selected graphics settings may not request any post targets.
    if (post.source[0] != a || post.source[1] != b) {
        post.Release();
        post.source[0] = a;
        post.source[1] = b;
        for (int i = 0; i < 2; ++i) {
            D3DSURFACE_DESC desc{};
            if (FAILED(post.source[i]->GetLevelDesc(0,&desc)) ||
                FAILED(device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,
                                              D3DPOOL_DEFAULT,&post.images[i],nullptr)) ||
                FAILED(post.images[i]->GetSurfaceLevel(0,&post.surfaces[i]))) {
                Log("Player %d post targets unavailable", slot + 1);
                post.Release();
                post.source[0] = a;
                post.source[1] = b;
                return;
            }
            device->ColorFill(post.surfaces[i],nullptr,0xff000000);
        }
        Log("Player %d post targets initialized", slot + 1);
        post.ready = true;
    }
    if (post.ready) {
        SwapPostTargets(post);
        g_splitHistoryActive = true;
    }
}

static void EndPlayerPostTargets() {
    if (!g_splitHistoryActive) return;
    SwapPostTargets(g_postTargets[g_passSlot - 1]);
    g_splitHistoryActive = false;
}

static void ReleaseWorldTargets() {
    EndPlayerPostTargets();
    for (auto& target : g_worldTargets) target.Release();
    for (auto& target : g_postTargets) target.Release();
}

static void ReleaseResources() {
    ReleaseWorldTargets();
    g_soloWorldTarget.Release();
    for (auto& t : g_tex) if (t) { t->Release(); t = nullptr; }
    if (g_sb) { g_sb->Release(); g_sb = nullptr; }
    g_texW = g_texH = 0;
}

typedef HRESULT (STDMETHODCALLTYPE *ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static ResetFn g_origReset = nullptr;
static void ReleaseUIResources();
static HRESULT STDMETHODCALLTYPE Hook_Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp) {
    ReleaseResources();
    ReleaseUIResources();
    return g_origReset(d, pp);
}
static void HookDevice() {
    static IDirect3DDevice9* hooked = nullptr;
    IDirect3DDevice9* d = Dev();
    if (!d || d == hooked) return;
    void** vt = *(void***)d;
    if (vt[16] != (void*)Hook_Reset) {
        g_origReset = (ResetFn)vt[16];
        void* f = (void*)Hook_Reset;
        WriteMem((uintptr_t)&vt[16], &f, sizeof f);
    }
    hooked = d;
    Log("D3D9 device hooked (%p)", d);
}

static bool Capture(int idx) {
    g_captured[idx] = false;
    IDirect3DDevice9* d = Dev();
    IDirect3DSurface9* rt = nullptr;
    if (!d || FAILED(d->GetRenderTarget(0, &rt)) || !rt) return false;
    D3DSURFACE_DESC desc{};
    if (FAILED(rt->GetDesc(&desc))) {
        rt->Release();
        return false;
    }
    if (desc.Width != g_texW || desc.Height != g_texH || desc.Format != g_texFmt) {
        for (auto& t : g_tex) if (t) { t->Release(); t = nullptr; }
        g_texW = desc.Width; g_texH = desc.Height; g_texFmt = desc.Format;
        Log("Render target %ux%u fmt %d msaa %d", desc.Width, desc.Height, desc.Format, desc.MultiSampleType);
    }
    if (!g_tex[idx]) {
        HRESULT hr = d->CreateTexture(g_texW, g_texH, 1, D3DUSAGE_RENDERTARGET, g_texFmt, D3DPOOL_DEFAULT, &g_tex[idx], nullptr);
        if (FAILED(hr)) { Log("CreateTexture failed %08X", hr); rt->Release(); return false; }
    }
    IDirect3DSurface9* s = nullptr;
    if (FAILED(g_tex[idx]->GetSurfaceLevel(0, &s)) || !s) {
        rt->Release();
        return false;
    }
    HRESULT hr = d->StretchRect(rt, nullptr, s, nullptr, D3DTEXF_NONE);
    s->Release(); rt->Release();
    if (FAILED(hr)) { static int n; if (n++ < 5) Log("StretchRect capture failed %08X", hr); return false; }
    g_captured[idx] = true;
    return true;
}

struct QuadV { float x, y, z, rhw, u, v; };
struct ColV { float x, y, z, rhw; DWORD c; };

static void PrepareSceneQuad(IDirect3DDevice9* d) {
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
}

static void Compose() {
    IDirect3DDevice9* d = Dev();
    if (!d) return;
    const int views = g_splitViews;
    for (int i = 0; i < views; ++i) if (!g_captured[i]) return;
    if (!g_sb) d->CreateStateBlock(D3DSBT_ALL, &g_sb);
    if (!g_sb || FAILED(g_sb->Capture())) return;
    float W = (float)g_texW, H = (float)g_texH;
    D3DVIEWPORT9 vp = { 0, 0, g_texW, g_texH, 0.f, 1.f };
    d->SetViewport(&vp);
    PrepareSceneQuad(d);
    for (int i = 0; i < views; i++) {
        const auto pane = PlayerPane(i);
        const float x0 = pane.x * W - .5f, x1 = (pane.x + pane.w) * W - .5f;
        const float y0 = pane.y * H - .5f, y1 = (pane.y + pane.h) * H - .5f;
        QuadV q[4] = { {x0,y0,0,1,0,0}, {x1,y0,0,1,1,0}, {x0,y1,0,1,0,1}, {x1,y1,0,1,1,1} };
        d->SetTexture(0, g_tex[i]);
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QuadV));
    }
    if (cfg.divider > 0) {
        float t = (float)cfg.divider;
        d->SetTexture(0, nullptr);
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        DWORD c = 0xFF000000;
        for (int line = 0; line < (g_quadRender ? 2 : 1); line++) {
            bool vert = g_quadRender ? line == 0 : cfg.layout == 1;
            float x0, y0, x1, y1;
            if (vert) { x0 = W / 2 - t / 2; x1 = W / 2 + t / 2; y0 = 0; y1 = g_splitViews == 3 ? H / 2 : H; }
            else { y0 = H / 2 - t / 2; y1 = H / 2 + t / 2; x0 = 0; x1 = W; }
            ColV q[4] = { {x0,y0,0,1,c}, {x1,y0,0,1,c}, {x0,y1,0,1,c}, {x1,y1,0,1,c} };
            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(ColV));
        }
    }
    g_sb->Apply();
}

typedef void (THISCALL *FERenderFn)(void* fe);

// The 3D passes are composed first. Native FEng HUD packages then draw at
// output resolution, with only their coordinates mapped into each player's view.
static void** LocalPlayers(int* n) { *n = *(int*)0x92D884; return *(void***)0x92D87C; }

struct SplitHudPass { int player; };
static SplitHudPass g_hudQ;
static bool g_hudQOn = false;
static bool g_hudMapTile = false;
enum class SplitHudPiece { General, RacePanel, Speedbreaker, Pursuit };
static SplitHudPiece g_hudPiece = SplitHudPiece::General;
static bool g_splitHudPursuits = false;
static SplitHudPiece IdentifySplitHudPiece(void* object);
static int g_hudAx = -1, g_hudAy = -1;
static void XformHudPoly(float* p, float* save);
static void RestoreHudPoly(float* p, const float* saved) {
    for (int i = 0; i < 4; ++i) {
        p[4*i] = saved[2*i];
        p[4*i+1] = saved[2*i+1];
    }
}
static void Hook_PolyRenderMask(float* poly, void* texture, void* mask, int flags) {
    float saved[8];
    const bool transform = g_hudQOn && poly;
    if (transform) XformHudPoly(poly, saved);
    ((void(*)(float*,void*,void*,int))0x6D1A40)(poly, texture, mask, flags);
    if (transform) RestoreHudPoly(poly, saved);
}
static uintptr_t o_PolyRender3 = 0x6D1260, o_PolyRender4 = 0x6D1930;
static void Hook_PolyRender3(float* poly, void* texture, int flags) {
    float saved[8];
    const bool transform = g_hudQOn && poly;
    if (transform) XformHudPoly(poly, saved);
    ((void(*)(float*, void*, int))o_PolyRender3)(poly, texture, flags);
    if (transform) RestoreHudPoly(poly, saved);
}
static void Hook_PolyRender4(float* poly, void* texture, void* mask, int flags) {
    float saved[8];
    const bool transform = g_hudQOn && poly;
    if (transform) XformHudPoly(poly, saved);
    ((void(*)(float*, void*, void*, int))o_PolyRender4)(poly, texture, mask, flags);
    if (transform) RestoreHudPoly(poly, saved);
}

using GIRenderObjFn = void (THISCALL*)(void*, void*);
static GIRenderObjFn o_GIRenderObject = (GIRenderObjFn)0x516630;
static void FASTCALL Hook_GIRenderObject(void* gi, void*, void* object) {
    const auto piece = g_hudQOn && object ? IdentifySplitHudPiece(object) : SplitHudPiece::General;
    if (piece == SplitHudPiece::Speedbreaker || (piece == SplitHudPiece::Pursuit && !g_splitHudPursuits))
        return;
    g_hudPiece = piece;
    g_hudAx = g_hudAy = -1;
    const auto hash = object && g_hudQOn ? At<uint32_t>(static_cast<uint8_t*>(object), 0x10) : 0;
    g_hudMapTile = hash - 0xF60166C2u < 4; // TRACK_MAP1 through TRACK_MAP4.
    o_GIRenderObject(gi, object);
    g_hudMapTile = false;
    g_hudPiece = SplitHudPiece::General;
}

using UncompressFn = void (THISCALL*)(void*, short*, int);
#include "minimap_cache.inc"

static int mapSlot = -1;
static short mapSectors[MAX_PLAYERS][4]{};
static void* mapSession = nullptr;
static void* activeMaps[MAX_PLAYERS]{};
using MMUpdateFn = void (THISCALL*)(void*, void*);
static MMUpdateFn o_MMUpdateRaw;
static void FASTCALL Hook_MinimapUpdate(void* map, void*, void* player) {
    const auto saved = mapSlot;
    int count = 0;
    auto** players = LocalPlayers(&count);
    if (SplitRaceSetup() && *G::pSimUserMode == 1 && players) {
        for (int i = 0; i < count && i < MAX_PLAYERS; ++i)
            if (players[i] == player) { mapSlot = i; activeMaps[i] = map; break; }
    }
    o_MMUpdateRaw(map, player);
    mapSlot = saved;
}

static void FASTCALL Hook_UncompressMaps(void* manager, void*, short* list, int count) {
    if (!minimapCacheInstalled || !SplitRaceSetup() || *G::pSimUserMode != 1 || count != 4) {
        ((UncompressFn)0x5672F0)(manager, list, count);
        return;
    }
    if (manager != mapSession) {
        mapSession = manager;
        for (auto& sectors : mapSectors) std::fill_n(sectors, 4, -1);
    }
    if (mapSlot >= 0) std::copy_n(list, 4, mapSectors[mapSlot]);
    short keep[MAX_PLAYERS * 4]{};
    int unique = 0;
    const auto append = [&](const short* ids) {
        for (int i = 0; i < 4; ++i) {
            if (unique < MAX_PLAYERS * 4 && ids[i] >= 0 && std::find(keep, keep + unique, ids[i]) == keep + unique)
                keep[unique++] = ids[i];
        }
    };
    append(list);
    for (const auto& sectors : mapSectors) append(sectors);
    LoadSplitMapTiles(static_cast<uint8_t*>(manager), keep, unique);
}

static void* FASTCALL Hook_MinimapDelete(void* map, void*, unsigned flags) {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (activeMaps[i] != map) continue;
        activeMaps[i] = nullptr;
        std::fill_n(mapSectors[i], 4, -1);
    }
    return ((void*(THISCALL*)(void*, unsigned))0x5A6520)(map, flags);
}

#include "ui.inc"
#include "raceopts.inc"

// the game is a 32-bit program (2 GB of address space without the large address aware flag):
// how much is left, and the biggest free block (D3D / streaming allocations need one piece)
static void LogMemory(const char* tag) {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) return;
    SIZE_T biggest = 0; MEMORY_BASIC_INFORMATION mbi; uintptr_t a = 0x10000;
    while (a < (uintptr_t)ms.ullTotalVirtual && VirtualQuery((void*)a, &mbi, sizeof mbi)) {
        if (mbi.State == MEM_FREE && mbi.RegionSize > biggest) biggest = mbi.RegionSize;
        uintptr_t n = (uintptr_t)mbi.BaseAddress + mbi.RegionSize; if (n <= a) break; a = n;
    }
    Log("%s: address space %llu of %llu MB free, biggest free block %u MB", tag,
        (unsigned long long)(ms.ullAvailVirtual >> 20), (unsigned long long)(ms.ullTotalVirtual >> 20), (unsigned)(biggest >> 20));
}
static void FASTCALL Hook_FERender(void* fe, void*) {
    if (g_pass == 1) {                              // hide FE in P1 pass, keep the 3D image
        Capture(g_passSlot);
        // the game normally clears the back buffer after Present; do it here so player 2's
        // frame doesn't get blended with player 1's image
        Dev()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
        return;
    }
    bool split = g_pass == 2;
    if (split) {
        if (Capture(g_passSlot)) Compose();
        RestoreSplitCamera();
    }
    g_pass = 0;
    QuadTick();
    if (cfg.debug && *G::pSimUserMode == 1) { static DWORD memAt = 0; DWORD now = GetTickCount(); if (*G::pGameFlowState == 6 && now - memAt > 30000) { memAt = now; LogMemory("Memory"); } }
    if (split) QuadFindHudPackages();
    ((FERenderFn)o_FERender)(fe);
    StartPositionDiagnostics();
    PostRaceTick();
    UpdateAndDrawMenus();
}

// ------------------------------------------------------------------ pause menu: "P2 CONTROLS" option
static const uint32_t STR_P2CONTROLS = 0x5C2B0001;   // our own language hash
static const char g_strP2Controls[] = "Local controls";
static const int OC_P2CONTROLS = 50;
static void* g_p2ctlVtbl[2];
static void FASTCALL P2Controls_React(void*, void*, const char*, uint32_t data, void*, uint32_t, uint32_t) {
    if (data == G::BUTTON_PRESSED) {
        uint8_t* db = *G::pFEDatabase;
        if (db) At<int>(At<uint8_t*>(db, 0x10), 0x24) = OC_P2CONTROLS;
    }
}
static void FASTCALL Hook_OptionsAddOption(void* menu, void*, void* opt) {
    ((AddOptionFn)o_OptionsAddOption)(menu, opt);
    Log("Options menu setup: userMode=%d fromPause=%d", *G::pSimUserMode, At<uint8_t>((uint8_t*)menu, 0x148));
    if (!cfg.enabled || *G::pSimUserMode != 1 || !At<uint8_t>((uint8_t*)menu, 0x148)) return;
    if (!g_p2ctlVtbl[0]) { g_p2ctlVtbl[0] = ((void**)0x89B8F0)[0]; g_p2ctlVtbl[1] = (void*)P2Controls_React; }
    void* o = G::GameNew(0x4C);
    if (!o) return;
    ((IconOptionCtorFn)G::IconOption_ctor)(o, 0xA04A7B26, STR_P2CONTROLS, 0);
    *(void***)o = g_p2ctlVtbl;
    ((AddOptionFn)G::IconScrollerMenu_AddOption)(menu, o);
    Log("P2 CONTROLS option added to pause options (%s)", At<const char*>((uint8_t*)menu, 0x10));
}
static void FASTCALL Hook_TraxAddOption(void* menu, void*, void* stock) {
    const auto add = (AddOptionFn)G::IconScrollerMenu_AddOption;
    add(menu, stock);
    if (!stock || !g_discxRouteReady || *G::pGameFlowState != 3 ||
        At<uint8_t>(static_cast<uint8_t*>(menu), 0x148)) return;
    auto* option = G::GameNew(0x4c);
    if (!option) return;
    ((IconOptionCtorFn)G::IconOption_ctor)(option, g_gtiTramp ? TEX_DISCX : 0xc52ccbf6, STR_DISCX, 0);
    add(menu, option);
}
typedef void (THISCALL *MenuNotifyFn)(void* self, uint32_t msg, void* obj, uint32_t p1, uint32_t p2);
static MenuNotifyFn o_OptionsNotify = (MenuNotifyFn)0x545310;
static void FASTCALL Hook_OptionsNotify(void* self, void*, uint32_t msg, void* obj, uint32_t p1, uint32_t p2) {
    if (RouteDiscMenuMessage(self, msg)) return;
    auto* menu = static_cast<uint8_t*>(self);
    if (msg == G::BUTTON_PRESSED && *G::pGameFlowState == 3 && !At<uint8_t>(menu, 0x148) &&
        At<uint8_t>(menu, carouselReactsToInput)) {
        auto* selected = At<uint8_t*>(menu, 0x38);
        if (selected && obj == At<void*>(selected, 0x10)) {
            const auto title = At<uint32_t>(selected, 0x2c);
            if (title == STR_DISCX && OpenDiscScreen(self)) return;
        }
    }
    o_OptionsNotify(self, msg, obj, p1, p2);
    uint8_t* db = *G::pFEDatabase;
    if (msg != 0xE1FDE1D1 || !db || At<uint32_t>((uint8_t*)self, 0x138) != G::BUTTON_PRESSED) return;
    auto* options = At<uint8_t*>(db, 0x10);
    if (!options) return;
    auto& category = At<int>(options, 0x24);
    if (category == OC_P2CONTROLS) {
        category = 0;
        OpenControlsScreen(At<const char*>((uint8_t*)self, 0x10), At<uint8_t>((uint8_t*)self, 0x148));
    }
}
// language lookup (hash arrives in EDX): return our own strings for our hashes
static void InstallStringHook() {
    uint8_t* f = (uint8_t*)0x56BB80;
    const uint8_t expect[7] = { 0x53, 0x8B, 0x1D, 0x80, 0xCF, 0x91, 0x00 };
    const bool stock = !memcmp(f, expect, sizeof(expect));
    if (!stock && f[0] != 0xe9) { Log("SearchForString: unexpected prologue"); return; }
    uintptr_t resume = 0x56BB87;
    if (!stock) {
        int32_t displacement;
        memcpy(&displacement, f + 1, sizeof(displacement));
        resume = 0x56BB85 + displacement;
    }
    const struct { uint32_t hash; const char* english; const char* spanish; } labels[] = {
        { G::STR_SPLITSCREEN, "Split screen", "Pantalla dividida" },
        { STR_P2CONTROLS, g_strP2Controls, "Controles locales" },
        { STR_AIRACERS, g_strAIRacers, "Rivales IA" },
        { STR_AIDIFF, g_strAIDiff, "Dificultad de IA" },
        { STR_THREEPLAYER, "3-player split screen", "Pantalla dividida para 3" },
        { STR_FOURPLAYER, "4-player split screen", "Pantalla dividida para 4" },
        { STR_DISCX, "DISCX", "DISCX" },
    };
    uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    int n = 0;
    for (const auto& label : labels) {
        // CMP EDX, hash. On a match, resolve the label using the live language.
        constexpr uint8_t block[] = {
            0x81, 0xfa, 0, 0, 0, 0, 0x75, 0x14,
            0x83, 0x3d, 0xc0, 0x41, 0x8f, 0x00, 0x04, // CMP [0x8f41c0], 4
            0xb8, 0, 0, 0, 0, 0x75, 0x05,             // MOV EAX, english; JNE return
            0xb8, 0, 0, 0, 0, 0xc3                    // MOV EAX, spanish; RET
        };
        memcpy(t + n, block, sizeof block);
        memcpy(t + n + 2, &label.hash, sizeof label.hash);
        memcpy(t + n + 16, &label.english, sizeof label.english);
        memcpy(t + n + 23, &label.spanish, sizeof label.spanish);
        n += sizeof block;
    }
    if (stock) {
        memcpy(t + n, expect, sizeof(expect));
        n += sizeof(expect);
    }
    t[n++] = 0xe9;
    *(int32_t*)(t + n) = (int32_t)(resume - ((uintptr_t)t + n + 4));
    uint8_t j[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    *(int32_t*)(j + 1) = (int32_t)((uintptr_t)t - (0x56BB80 + 5));
    WriteMem(0x56BB80, j, 7);
}
// block the game's menu input while one of our screens is open
typedef void (THISCALL *JoyFn)(void* self);
static void FASTCALL Hook_HandleJoy(void* self, void*) {
    if (g_inputBlocked) { ((JoyFn)0x5247A0)(self); return; }   // FlushActions
    ((JoyFn)0x559400)(self);
}

// bTList head swap that keeps node back-links valid
struct bNode { bNode* next; bNode* prev; };
static void SwapLists(bNode* a, bNode* b) {
    bNode an = *a, bn = *b;
    bool ae = (an.next == a), be = (bn.next == b);
    if (be) { a->next = a->prev = a; } else { a->next = bn.next; a->prev = bn.prev; bn.next->prev = a; bn.prev->next = a; }
    if (ae) { b->next = b->prev = b; } else { b->next = an.next; b->prev = an.prev; an.next->prev = b; an.prev->next = b; }
}
static bool HasMover(uint8_t* v) { bNode* h = (bNode*)(v + G::EV_MOVERLIST); return h->next && h->next != h; }

typedef void (THISCALL *CalcMatFn)(void* plat, void* view, uint32_t a, uint32_t farz, uint32_t b, uint32_t c);
typedef void (THISCALL *ViewFn)(void* view);

static void MatMul(float* o, const float* a, const float* b) {
    float r[16];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        r[i*4+j] = a[i*4]*b[j] + a[i*4+1]*b[4+j] + a[i*4+2]*b[8+j] + a[i*4+3]*b[12+j];
    memcpy(o, r, sizeof r);
}

// Recompute player view matrices, then narrow the projection to the half-screen viewport.
// Done on the final matrices so it works with or without the widescreen fix.
// eViewPlatInfo layout (floats): [0] view, [16] proj, [32] view*proj, [48] proj(z-biased),
// [64] view*proj(z-biased), [80] six frustum planes
static void RecalcView1(int slot) {
    uint8_t* v1 = G::View(1);
    float* m = *(float**)(v1 + G::EV_PLAT);
    if (!m || !*(void**)(v1 + G::EV_RT) || !*(void**)(v1 + G::EV_CAMERA)) return;
    ((CalcMatFn)G::eView_CalcMatrices)(m, v1, 0, 0x466A6000 /*15000.0f*/, 0, 1);
    const auto pane = PlayerPane(slot);
    const float stretch = pane.w / pane.h;
    const float wide = powf(std::min(stretch, 1.f / stretch), cfg.widen);
    const float kx = wide * std::max(1.f, 1.f / stretch);
    const float ky = wide * std::max(1.f, stretch);
    for (int base : {16, 48}) {
        for (int r = 0; r < 4; r++) { m[base + r*4 + 0] *= kx; m[base + r*4 + 1] *= ky; }
    }
    MatMul(m + 32, m, m + 16);
    MatMul(m + 64, m, m + 48);
    const float* M = m + 32;
    for (int i = 0; i < 6; i++) {
        float* pl = m + 80 + i * 4;
        for (int r = 0; r < 4; r++) {
            float c3 = M[r*4 + 3];
            switch (i) {
                case 0: pl[r] = c3 + M[r*4 + 0]; break;
                case 1: pl[r] = c3 - M[r*4 + 0]; break;
                case 2: pl[r] = c3 + M[r*4 + 1]; break;
                case 3: pl[r] = c3 - M[r*4 + 1]; break;
                case 4: pl[r] = M[r*4 + 2]; break;
                default: pl[r] = c3 - M[r*4 + 2]; break;
            }
        }
        float len = sqrtf(pl[0]*pl[0] + pl[1]*pl[1] + pl[2]*pl[2]);
        if (len > 0) { float inv = 1.f / len; for (int r = 0; r < 4; r++) pl[r] *= inv; }
    }
}

static void SwapPlayerViews(int other = 2) {
    uint8_t* v1 = G::View(1); uint8_t* v2 = G::View(other);
    void* c = At<void*>(v1, G::EV_CAMERA);
    At<void*>(v1, G::EV_CAMERA) = At<void*>(v2, G::EV_CAMERA);
    At<void*>(v2, G::EV_CAMERA) = c;
    SwapLists((bNode*)(v1 + G::EV_MOVERLIST), (bNode*)(v2 + G::EV_MOVERLIST));
}

static bool SplitActive() {
    if (!cfg.enabled || !SplitRaceSetup()) return false;
    if (*G::pSimUserMode != 1) return false;
    if (*G::pGameFlowState != 6) return false;
    uint8_t* v1 = G::View(1); uint8_t* v2 = G::View(2);
    if (!At<void*>(v1, G::EV_CAMERA) || !At<void*>(v2, G::EV_CAMERA)) return false;
    if (!HasMover(v1) || !HasMover(v2)) return false;
    if (!At<int8_t>(v1, G::EV_ACTIVE)) return false;
    return true;
}

// Update reflection cameras for the current driver with the user's native
// reflection setting. The engine retains its own update-rate decision.
static void RefreshEnvMapForView1() {
    if (At<int>(0x9017B8) > 0) ((G::VoidFn)0x756060)();
}
#include "quad.inc"

static bool ResolveSplitScene(IDirect3DDevice9* device, const SplitWorldTarget& source, IDirect3DSurface9* destination,
                              UINT width, UINT height) {
    if (source.color != source.resolved &&
        FAILED(device->StretchRect(source.color, nullptr, source.resolved, nullptr, D3DTEXF_NONE))) return false;
    if (SUCCEEDED(device->StretchRect(source.resolved, nullptr, destination, nullptr, D3DTEXF_LINEAR))) return true;
    // A textured draw is also valid when the destination uses multisampling.
    if (!g_sb) device->CreateStateBlock(D3DSBT_ALL, &g_sb);
    if (!g_sb || FAILED(g_sb->Capture())) return false;
    IDirect3DSurface9* color = nullptr;
    IDirect3DSurface9* depth = nullptr;
    device->GetRenderTarget(0, &color);
    device->GetDepthStencilSurface(&depth);
    D3DVIEWPORT9 oldViewport{};
    device->GetViewport(&oldViewport);
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
    HRESULT result = device->SetRenderTarget(0, destination);
    if (SUCCEEDED(result)) {
        device->SetDepthStencilSurface(nullptr);
        device->SetViewport(&viewport);
        PrepareSceneQuad(device);
        device->SetTexture(0, source.image);
        const float x = float(width) - .5f, y = float(height) - .5f;
        const QuadV quad[] = {{-.5f,-.5f,0,1,0,0}, {x,-.5f,0,1,1,0}, {-.5f,y,0,1,0,1}, {x,y,0,1,1,1}};
        result = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(QuadV));
    }
    if (color) { device->SetRenderTarget(0, color); color->Release(); }
    device->SetDepthStencilSurface(depth);
    if (depth) depth->Release();
    g_sb->Apply();
    device->SetViewport(&oldViewport);
    return SUCCEEDED(result);
}

struct ScaledWorldPass {
    uint8_t* target;
    IDirect3DDevice9* device;
    SplitWorldTarget& reduced;
    IDirect3DSurface9* color;
    IDirect3DSurface9* depth;
    UINT width, height;
    bool resolved = false;
};

static ScaledWorldPass* g_scaledWorldPass = nullptr;
static bool g_resolveBeforePost = false;

extern "C" __attribute__((used, force_align_arg_pointer)) void SplitResolveWorldBeforePost() {
    auto* pass = g_scaledWorldPass;
    if (!pass) return;
    g_scaledWorldPass = nullptr;
    auto* target = pass->target;
    At<IDirect3DSurface9*>(target, 8) = pass->color;
    At<IDirect3DSurface9*>(target, 12) = pass->depth;
    At<UINT>(target, 0x14) = pass->width;
    At<UINT>(target, 0x18) = pass->height;
    if (At<uint8_t*>(0x982A20) == target)
        ((void(*)(uint8_t*, int))0x6BD3B0)(target, 0);
    pass->resolved = ResolveSplitScene(pass->device, pass->reduced, pass->color, pass->width, pass->height);
}

// The renderer copies the scene into the stock effect inputs immediately after
// this boundary. Both the inputs and the viewport must already be restored.
__attribute__((naked)) static void Hook_WorldPostBoundary() {
    asm volatile(
        "pushfl\n"
        "pushal\n"
        "call _SplitResolveWorldBeforePost\n"
        "popal\n"
        "popfl\n"
        "movl 0x9017fc, %edx\n"
        "ret\n");
}

// The console renderer continues with its own post-processing chain here.
__attribute__((naked)) static void ResolveBeforeConsolePost() {
    asm volatile(
        "pushfl\n"
        "pushal\n"
        "call _SplitResolveWorldBeforePost\n"
        "popal\n"
        "popfl\n"
        "pushl $0x6df8ab\n"
        "ret\n");
}

static bool InstallWorldPostBoundary() {
    constexpr uintptr_t address = 0x6DF5E7;
    constexpr uint8_t original[] = {0x8b, 0x15, 0xfc, 0x17, 0x90, 0x00};
    constexpr uint8_t consoleContinue[] = {0xe9, 0xbf, 0x02, 0, 0};
    if (GetModuleHandleW(L"World360.dll") &&
        !memcmp(reinterpret_cast<const void*>(address), consoleContinue, sizeof consoleContinue)) {
        uint8_t jump[5] = {0xe9};
        const auto displacement = reinterpret_cast<uintptr_t>(&ResolveBeforeConsolePost) - (address + sizeof(jump));
        memcpy(jump + 1, &displacement, sizeof displacement);
        WriteMem(address, jump, sizeof jump);
        return !memcmp(reinterpret_cast<const void*>(address), jump, sizeof jump);
    }
    if (memcmp(reinterpret_cast<const void*>(address), original, sizeof original)) {
        Log("World post-processing boundary unavailable");
        return false;
    }
    uint8_t patch[] = {0xe8, 0, 0, 0, 0, 0x90};
    auto displacement = reinterpret_cast<uintptr_t>(&Hook_WorldPostBoundary) - (address + 5);
    memcpy(patch + 1, &displacement, sizeof displacement);
    WriteMem(address, patch, sizeof patch);
    return !memcmp(reinterpret_cast<const void*>(address), patch, sizeof patch);
}

// Render the world with the pixel budget of this player's part of the output.
static bool g_drawingPlayerPane = false;
static void RenderScaledWorld(bool split) {
    struct PaneScope {
        bool previous;
        ~PaneScope() { g_drawingPlayerPane = previous; }
    } scope{g_drawingPlayerPane};
    g_drawingPlayerPane = split;
    const auto render = (G::VoidFn)o_DisplayInner;
    auto* target = At<uint8_t*>(G::View(1), G::EV_RT);
    auto* d = Dev();
    if (!g_resolveBeforePost || !target || !d || !At<int>(target, 0x10)) { render(); return; }
    auto*& color = At<IDirect3DSurface9*>(target, 8);
    auto*& depth = At<IDirect3DSurface9*>(target, 12);
    auto& width = At<UINT>(target, 0x14);
    auto& height = At<UINT>(target, 0x18);
    const UINT originalW = width, originalH = height;
    const auto pane = split ? PlayerPane(g_passSlot) : SplitPane{0, 0, 1, 1};
    const UINT paneW = static_cast<UINT>(ceilf(width * pane.w));
    const UINT paneH = static_cast<UINT>(ceilf(height * pane.h));
    const float adaptive = split ? std::clamp(0.9f - (std::min(paneW, paneH) - 480.0f) / 3600.0f, .72f, .94f) : 1.f;
    const float scale = adaptive * g_renderScale;
    const UINT w = std::max(1u, static_cast<UINT>(lroundf(paneW * scale)));
    const UINT h = std::max(1u, static_cast<UINT>(lroundf(paneH * scale)));
    if (w == width && h == height) { render(); return; }
    auto& reduced = split ? g_worldTargets[g_splitViews == 3 && g_passSlot == 2 ? 1 : 0] : g_soloWorldTarget;
    if (color != reduced.destination || w != reduced.width || h != reduced.height) {
        reduced.Release();
        D3DSURFACE_DESC rt{}, ds{};
        if (color && depth && SUCCEEDED(color->GetDesc(&rt)) && SUCCEEDED(depth->GetDesc(&ds)) &&
            SUCCEEDED(d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, rt.Format, D3DPOOL_DEFAULT, &reduced.image, nullptr)) &&
            SUCCEEDED(reduced.image->GetSurfaceLevel(0, &reduced.resolved)) &&
            SUCCEEDED(d->CreateDepthStencilSurface(w,h,ds.Format,rt.MultiSampleType,rt.MultiSampleQuality,TRUE,&reduced.depth,nullptr)) &&
            (rt.MultiSampleType == D3DMULTISAMPLE_NONE ||
                SUCCEEDED(d->CreateRenderTarget(w,h,rt.Format,rt.MultiSampleType,rt.MultiSampleQuality,FALSE,&reduced.color,nullptr)))) {
            if (!reduced.color) reduced.color = reduced.resolved;
            Log("%s world target: %ux%u -> %ux%u (target %d, MSAA %u)", split ? "Split" : "Solo", w, h, width, height, At<int>(target, 0), (unsigned)rt.MultiSampleType);
        } else {
            reduced.Release();
            Log("Scaled world target unavailable, using %ux%u", width, height);
        }
        reduced.destination = color;
        reduced.width = w;
        reduced.height = h;
    }
    if (!reduced.color || !reduced.depth) { render(); return; }
    ScaledWorldPass pass{target, d, reduced, color, depth, originalW, originalH};
    color = reduced.color;
    depth = reduced.depth;
    width = w;
    height = h;
    using BindTarget = void(*)(uint8_t*, int);
    const auto bindTarget = (BindTarget)0x6BD3B0;
    bindTarget(target, 1);
    g_scaledWorldPass = &pass;
    render();
    // Restore the state on native paths that bypass the effects entirely.
    if (g_scaledWorldPass) SplitResolveWorldBeforePost();
    if (!pass.resolved) {
        Log("Scene resolve failed, reverting to the stock target");
        reduced.Release();
        reduced.destination = pass.color;
        reduced.width = w;
        reduced.height = h;
        render();
    }
}

static G::IntFn g_frameComposite = G::DisplayComposite;
static int g_lastSplitView = 0;

static void RestoreSplitCamera() {
    if (!g_lastSplitView) return;
    SwapPlayerViews(g_lastSplitView);
    g_lastSplitView = 0;
    RecalcView1(0);
}

static void Hook_FrameComposite(int beforeScene) {
    g_frameComposite(beforeScene);
    RestoreSplitCamera();
    EndPlayerPostTargets();
}

static void Hook_DisplayFrameInner() {
    g_frame++;
    HookDevice();
    if (g_filtersPreference >= 0) At<int>(0x901828) = g_filtersPreference;
    if (g_blurPreference >= 0) At<int>(0x9017dc) = g_blurPreference;
    bool split = SplitActive();
    if (!SplitRaceSetup() || *G::pGameFlowState != 6) {
        mapSession = nullptr;
        std::fill_n(activeMaps, MAX_PLAYERS, nullptr);
    }
    if (split != g_wasSplit) {
        Log("Split rendering %s (userMode=%d flow=%d viewMode=%d)", split ? "ON" : "OFF", *G::pSimUserMode, *G::pGameFlowState, *G::pCurrentViewMode);
        g_wasSplit = split;
        if (!split) ReleaseWorldTargets();
    }
    if (!split) {
        g_pass = 0;
        g_quadRender = false;
        g_splitViews = 2;
        if (*G::pGameFlowState == 6 && g_renderScale != 1.f) RenderScaledWorld(false);
        else {
            g_soloWorldTarget.Release();
            ((G::VoidFn)o_DisplayInner)();
        }
        return;
    }
    g_soloWorldTarget.Release();

    bool quad = QuadViewsReady();
    const int views = quad ? g_localPlayerCount : 2;
    if (views != g_splitViews) Log("Split rendering uses %d views", views);
    g_quadRender = quad;
    g_splitViews = views;
    for (auto& c : g_captured) c = false;
    // ---- player 1
    RecalcView1(0);
    RefreshEnvMapForView1();
    g_pass = 1; g_passSlot = 0;
    RenderScaledWorld(true);
    G::PostDisplayFX();
    G::DisplayComposite(0);
    if (!g_captured[0]) { Capture(0); Dev()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0); }

    // ---- players 2 and 3 of four: complete passes like player 1's
    if (quad) {
        for (int k = 1; k < g_splitViews - 1; k++) {
            SwapPlayerViews(kQuadView[k]);
            RecalcView1(k);
            g_pass = 1; g_passSlot = k;
            BeginPlayerPostTargets(k);
            G::DisplayComposite(1);
            RefreshEnvMapForView1();
            ((ViewFn)G::eView_SetupWorldLight)(G::View(1));
            G::ResetRenderStates();
            RenderScaledWorld(true);
            G::PostDisplayFX();
            G::DisplayComposite(0);
            if (!g_captured[k]) { Capture(k); Dev()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0); }
            EndPlayerPostTargets();
            SwapPlayerViews(kQuadView[k]);
        }
    }
    // ---- last player (2 of 2, or 4 of 4)
    const int lastSlot = g_splitViews - 1;
    const int last = kQuadView[lastSlot];
    SwapPlayerViews(last);
    RecalcView1(lastSlot);
    g_pass = 2; g_passSlot = lastSlot;
    BeginPlayerPostTargets(lastSlot);
    G::DisplayComposite(1);
    RefreshEnvMapForView1();
    ((ViewFn)G::eView_SetupWorldLight)(G::View(1));
    G::ResetRenderStates();
    RenderScaledWorld(true);
    g_lastSplitView = last;
    // eDisplayFrame runs the stock post-processing and composition, then restores the original camera and targets.
}

// Track streaming: UpdateCameraMovers only feeds the streamer from *active* views, and the PC
// never marks player 2's view active, so the world around player 2 was never loaded.
static uintptr_t o_UpdateCameraMovers = 0x47C880;
static void Hook_UpdateCameraMovers(uint32_t arg) {
    uint8_t* v2 = G::View(2);
    bool force = cfg.enabled && *G::pSimUserMode == 1 && HasMover(v2) && !At<int8_t>(v2, G::EV_ACTIVE);
    if (force) At<int8_t>(v2, G::EV_ACTIVE) = 1;
    ((void(*)(uint32_t))o_UpdateCameraMovers)(arg);
    if (force) At<int8_t>(v2, G::EV_ACTIVE) = 0;
    QuadStreaming();
}

// ------------------------------------------------------------------ install
// installed once the game starts initialising, i.e. after every other .asi has applied its patches
static void InstallLateHooks() {
    static bool done = false; if (done) return; done = true;
    uintptr_t o;
    if ((o = HookCall(G::CALL_QRMenu_AddOption2, (void*)Hook_QRAddOption, G::IconScrollerMenu_AddOption))) o_AddOption = o;
    if ((o = HookCall(0x6E75A7, (void*)Hook_FrameComposite, 0x6E6E40))) {
        g_resolveBeforePost = InstallWorldPostBoundary();
        g_frameComposite = (G::IntFn)o;
        if ((o = HookCall(G::CALL_eDisplayFrameInner, (void*)Hook_DisplayFrameInner, 0x6DE300))) o_DisplayInner = o;
    }
    if ((o = HookCall(G::CALL_FEManagerRender, (void*)Hook_FERender, G::FEManager_Render))) o_FERender = o;
    InstallFrameMemory();
    InstallQuadHooks();
    {
        if ((o = HookCall(0x6BE70F, (void*)Hook_PolyRender3, 0x6D1260))) o_PolyRender3 = o;
        if ((o = HookCall(0x6BE734, (void*)Hook_PolyRender4, 0x6D1930))) o_PolyRender4 = o;
        PatchCall(0x6BE6F4, (void*)Hook_PolyRenderMask, 0x6D1A40);
        PatchCall(0x6BE764, (void*)Hook_PolyRenderMask, 0x6D1A40);
        PatchCall(0x6BE77F, (void*)Hook_PolyRender3, 0x6D1260);
        PatchCall(0x6BE7A4, (void*)Hook_PolyRender4, 0x6D1930);
        InstallMinimapCache();
        HookCall(0x579A8F, (void*)Hook_UncompressMaps, 0x5672F0);
        void* f;
        o_MMUpdateRaw = *(MMUpdateFn*)0x8A2044; f = (void*)Hook_MinimapUpdate; WriteMem(0x8A2044, &f, 4);
        if (*(uintptr_t*)0x8A2040 == 0x5A6520) {
            f = (void*)Hook_MinimapDelete;
            WriteMem(0x8A2040, &f, sizeof f);
        }
    }
    {   // FE object render filter (minimap passes, speed trap score row)
        void* f;
        if (*(uintptr_t*)0x89B46C == 0x516630) { f = (void*)Hook_GIRenderObject; WriteMem(0x89B46C, &f, 4); }
        else { o_GIRenderObject = *(GIRenderObjFn*)0x89B46C; f = (void*)Hook_GIRenderObject; WriteMem(0x89B46C, &f, 4); Log("GI RenderObject already hooked, chaining"); }
    }
    if ((o = HookCall(0x52909C, (void*)Hook_OptionsAddOption, G::IconScrollerMenu_AddOption))) o_OptionsAddOption = o;
    g_discxRouteReady = InstallDiscMusicRouting();
    if (g_discxRouteReady) PatchCall(0x52915A, (void*)Hook_TraxAddOption, G::IconScrollerMenu_AddOption);
    HookCall(0x56431B, (void*)Hook_HandleJoy, 0x559400);
    { void* f = (void*)Hook_OptionsNotify; o_OptionsNotify = *(MenuNotifyFn*)0x89E530; WriteMem(0x89E530, &f, 4); }
    InstallStringHook();
    InstallRaceOptions();
    if (cfg.p2MenuInput) { uint8_t* c = (uint8_t*)0x559627; if (c[0] == 0x83 && c[1] == 0xFD && c[2] == 0x01) { uint8_t three = 3; WriteMem(0x559629, &three, 1); } }
    if ((o = HookCall(0x62F33E, (void*)Hook_PausePush, 0x5257F0))) o_PausePush = o;
    PatchCall(0x6A8959, (void*)Hook_GetControllerAttribs, 0x56D4B0);   // InputPlayer::FetchInput
    {   // FEngHud::DetermineHudFeatures turns the whole HUD off while its player looks back
        // (CameraMover::GetLookbackAngle != 0: test ax,ax / ja 0x57CD96). In split screen both minimaps
        // are drawn from player 1's HUD, so player 1 looking back took the map away from both players.
        // Split screen: the HUD stays while looking back.
        uint8_t* p = (uint8_t*)0x57CAB9;
        const uint8_t e[9] = { 0x66, 0x85, 0xC0, 0x0F, 0x87, 0xD4, 0x02, 0x00, 0x00 };
        if (!memcmp(p, e, 9)) {
            uint8_t* t = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            int k = 0;
            t[k++] = 0x66; t[k++] = 0x85; t[k++] = 0xC0;                            // test ax, ax
            t[k++] = 0x74; t[k++] = 0x0E;                                            // jz cont (+14)
            t[k++] = 0x83; t[k++] = 0x3D; *(uint32_t*)(t + k) = (uint32_t)(uintptr_t)G::pSimUserMode; k += 4; t[k++] = 0x01;   // cmp [userMode], 1
            t[k++] = 0x74; t[k++] = 0x05;                                            // je cont (+5)
            t[k++] = 0xE9; *(int32_t*)(t + k) = (int32_t)(0x57CD96 - ((uintptr_t)t + k + 4)); k += 4;   // jmp hide
            t[k++] = 0xE9; *(int32_t*)(t + k) = (int32_t)(0x57CAC2 - ((uintptr_t)t + k + 4)); k += 4;   // cont: jmp on
            uint8_t j[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
            *(int32_t*)(j + 1) = (int32_t)((uintptr_t)t - (0x57CAB9 + 5));
            WriteMem(0x57CAB9, j, 9);
        } else Log("HUD look back patch: unexpected bytes");
    }
    if ((o = HookCall(0x6F4EDA, (void*)Hook_CreatePlayers, 0x6F2700))) o_CreatePlayers = o;
    if ((o = HookCall(0x480C16, (void*)Hook_UpdateCameraMovers, 0x47C880))) o_UpdateCameraMovers = o;
    if ((o = HookCall(0x7C2DC5, (void*)Hook_CreateMPProfile, 0x5ABA50))) o_CreateMPProfile = o;   // player 2 car stable
    Log("Late patches installed");
}

static void Install() {
    // car memory for 2 player races (PC ships with 1 KB, which can't load any car)
    int pool = cfg.poolKB > 0 ? cfg.poolKB : *G::pCarLoaderPool1P;
    Log("CarLoaderPoolSizes = {%d, %d} -> 2P %d", *G::pCarLoaderPool1P, *G::pCarLoaderPool2P, pool);
    WriteMem((uintptr_t)G::pCarLoaderPool2P, &pool, 4);

    // view mode switch: treat EVIEWMODE_TWOH/TWOV like EVIEWMODE_ONE (keeps player view active)
    uint8_t* j = (uint8_t*)G::JNE_ViewModeSwitch;
    if (j[0] == 0x75 && j[1] == 0x7D) { uint8_t b[2] = { 0x75, 0x37 }; WriteMem(G::JNE_ViewModeSwitch, b, 2); }
    else Log("view mode patch: unexpected bytes %02X %02X", j[0], j[1]);

    // front end: track 2 joypads (PC sets 1) so player 2's controller can drive menus
    if (cfg.p2MenuInput) { uint8_t* q = (uint8_t*)0x516925; if (q[0] == 0x6A && q[1] == 0x01) { uint8_t three = 3; WriteMem(0x516926, &three, 1); } else Log("SetNumJoyPads patch: unexpected bytes"); }

    // "player 2 press start": accept the same controller as player 1
    uint8_t* je = (uint8_t*)G::JE_PressStartSamePort;
    if (je[0] == 0x74) { uint8_t b[2] = { 0x90, 0x90 }; WriteMem(G::JE_PressStartSamePort, b, 2); }
    else Log("press start patch: unexpected byte %02X", je[0]);

    ExpandFrontendRenderStorage();
    InstallQPSDetour();
    InstallCustomTrackChoice();
    { uintptr_t o = HookCall(0x6660E2, (void*)Hook_FEInitInput, 0x516D70); if (o) o_FEInitInput = o; }
    { uintptr_t o = HookCall(0x6660DD, (void*)Hook_IOModuleInit, 0x63CD00); if (o) o_IOInit = o; }
    if (*(uint32_t*)0x8B72D0 == 0x7A3F20) { void* f = (void*)Hook_PressStartNotify; WriteMem(0x8B72D0, &f, 4); }
    else Log("press start vtable: unexpected");
    Log("Early patches installed");
}

static bool CheckVersion() {
    // speed.exe v1.3: UIQRMainMenu::Setup prologue and the PC CarLoaderPoolSizes value
    const uint8_t sig[] = { 0x6A, 0xFF, 0x68, 0x3C, 0x2B, 0x88, 0x00 };
    return !memcmp((void*)0x7AA730, sig, sizeof sig) && *(int*)0x8F7EF0 == 50000;
}

static void LogCrashMarker() {}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    static int count = 0; static bool inside = false;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                 code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_PRIV_INSTRUCTION;
    if (fatal && !inside && count < 3 && g_log) {
        inside = true; count++;
        CONTEXT* c = ep->ContextRecord;
        Log("CRASH: code %08lX at %08lX (pass=%d frame=%d split=%d)", ep->ExceptionRecord->ExceptionCode,
            (unsigned long)(uintptr_t)ep->ExceptionRecord->ExceptionAddress, g_pass, g_frame, (int)g_wasSplit);
        {   // which module crashed
            HMODULE m = nullptr; char path[MAX_PATH] = "?";
            void* at = ep->ExceptionRecord->ExceptionAddress;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) && m) {
                GetModuleFileNameA(m, path, sizeof path);
                const char* b = strrchr(path, '\\'); Log("  module %s +%08lX", b ? b + 1 : path, (unsigned long)((uintptr_t)at - (uintptr_t)m));
            } else Log("  module: none");
        }
        if (ep->ExceptionRecord->NumberParameters >= 2)
            Log("  access %s %08lX", ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
        LogMemory("  memory at the crash");
        Log("  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
            c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        uint32_t* sp = (uint32_t*)c->Esp;
        char buf[1024]; int n = 0;
        for (int i = 0; i < 64 && n < 1000; i++) {
            uint32_t v = 0;
            if (IsBadReadPtr(sp + i, 4)) break;
            v = sp[i];
            if (v >= 0x401000 && v < 0x890000) n += snprintf(buf + n, sizeof buf - n, "%08X ", v);
            else { HMODULE me = nullptr; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void*)&LogCrashMarker, &me);
                   MEMORY_BASIC_INFORMATION mi; if (me && VirtualQuery((void*)(uintptr_t)v, &mi, sizeof mi) && mi.AllocationBase == (void*)me) n += snprintf(buf + n, sizeof buf - n, "asi+%X ", (unsigned)(v - (uintptr_t)me)); }
        }
        buf[n] = 0;
        Log("  stack code refs: %s", buf);
        inside = false;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool installed = false;
} // namespace

bool unlimited_split_screen::Install(HMODULE executable) {
    if (installed) return true;
    if (reinterpret_cast<uintptr_t>(executable) != 0x400000 || !CheckVersion()) return false;
    const auto pathLength = GetModuleFileNameA(executable, g_dir, MAX_PATH);
    if (!pathLength || pathLength >= MAX_PATH) return false;
    char* separator = strrchr(g_dir, '\\');
    if (!separator) return false;
    *separator = '\0';
    LoadConfig();
    InstallGetTextureInfoDetour();
    if (!cfg.enabled) return false;
    char logPath[MAX_PATH];
    if (cfg.debug && snprintf(logPath, sizeof(logPath), "%s\\NFSMWSplitScreen.log", g_dir) < MAX_PATH)
        g_log = fopen(logPath, "w");
    LoadBindings();
    ::Install();
    if (g_log) AddVectoredExceptionHandler(0, CrashFilter);
    installed = true;
    return true;
}

void unlimited_split_screen::SetGameWindow(HWND window) {
    SetDiscGameWindow(window);
}

void unlimited_split_screen::SetDiscCover(std::shared_ptr<const unlimited_discx::Cover> cover) {
    g_discCover = std::move(cover);
}

void unlimited_split_screen::SetRenderScale(unsigned tenths) {
    g_renderScale = std::clamp(tenths, 4u, 20u) / 10.f;
}

void unlimited_split_screen::SetPostPreferences(int filters, int motionBlur) {
    g_filtersPreference = filters;
    g_blurPreference = motionBlur;
}

bool unlimited_split_screen::DrawingPlayerPane() { return g_drawingPlayerPane; }

bool unlimited_split_screen::RaceActive() {
    return installed && SplitRaceSetup() && *G::pSimUserMode == 1 && *G::pGameFlowState == 6;
}

int unlimited_split_screen::FeedbackPad(int controllerPort) {
    if (!RaceActive() || controllerPort < 0) return -1;
    if (controllerPort == g_p1port) return g_dev[0];
    if (controllerPort == g_p2port) return g_dev[1];
    if (g_quad && controllerPort == g_quadPort[0] && !g_quadIsAI[0]) return g_dev[2];
    if (g_quad && controllerPort == g_quadPort[1] && !g_quadIsAI[1]) return g_dev[3];
    return -1;
}
