/* Split/Second gamepad fix: xinput1_3.dll proxy.
 *
 * The PC version reads the gamepad (DirectInput, "Joy 0") only for driving;
 * menus accept keyboard only, and in-race hints name keyboard keys or raw
 * joystick axes. This proxy forwards all XInput calls to the real XInput
 * and adds two things:
 *
 * 1. Menu navigation: a thread that, while the game is in a menu (front-end,
 *    pause, results), turns gamepad buttons into key presses. During driving
 *    it does nothing (except Start -> Esc for pause), so sticks and triggers
 *    stay analog. Menu detection uses two globals in SplitSecond.exe .data:
 *      [0xd4456c] == 0  -> not in a race (front-end)
 *      [0xd8c104] != 0  -> race paused / results
 * 2. HUD hints: a hook on the function that expands *ACTION_...* tokens in
 *    messages, so they show the gamepad button icon (see below).
 *
 * Addresses are for SplitSecond.exe with TimeDateStamp 0x4be3ea74 and
 * SizeOfImage 0x1a64000; with any other exe the proxy only forwards XInput.
 * Build with -DDIAG for the research build (memory snapshots, image dump).
 *
 * Log: %USERPROFILE%\Documents\ss_xinput_fix.log */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define EXPECTED_TIMESTAMP  0x4be3ea74
#define EXPECTED_IMAGESIZE  0x1a64000
#define ADDR_IN_RACE        0xd4456c
#define ADDR_RACE_PAUSED    0xd8c104

#define REPEAT_DELAY_MS     400
#define REPEAT_INTERVAL_MS  90
#define STICK_PRESS         20000
#define STICK_RELEASE       12000

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XGAMEPAD;
typedef struct { DWORD dwPacketNumber; XGAMEPAD Gamepad; } XSTATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XVIB;

#define XB_DPAD_UP    0x0001
#define XB_DPAD_DOWN  0x0002
#define XB_DPAD_LEFT  0x0004
#define XB_DPAD_RIGHT 0x0008
#define XB_START      0x0010
#define XB_A          0x1000
#define XB_B          0x2000
#define XB_X          0x4000
#define XB_Y          0x8000

/* Virtual inputs: buttons plus left stick directions. */
enum { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_A, IN_B, IN_X, IN_Y, IN_START, IN_COUNT };

static const struct { WORD vk; BOOL extended; BOOL repeat; BOOL always; } keymap[IN_COUNT] = {
    [IN_UP]    = { VK_UP,     TRUE,  TRUE },
    [IN_DOWN]  = { VK_DOWN,   TRUE,  TRUE },
    [IN_LEFT]  = { VK_LEFT,   TRUE,  TRUE },
    [IN_RIGHT] = { VK_RIGHT,  TRUE,  TRUE },
    [IN_A]     = { VK_RETURN, FALSE, FALSE },
    [IN_B]     = { VK_ESCAPE, FALSE, FALSE },
    [IN_X]     = { VK_SPACE,  FALSE, FALSE }, /* prompt slot X (Extra1) */
    [IN_Y]     = { VK_TAB,    FALSE, FALSE }, /* prompt slot Y (Extra2) */
    [IN_START] = { VK_ESCAPE, FALSE, FALSE, TRUE }, /* pause works only via Esc */
};

static HMODULE real;
static FARPROC p_GetState, p_SetState, p_GetCaps, p_Enable, p_GetDSound, p_GetBattery, p_GetKeystroke, p_GetStateEx;
static FILE *logf;
static CRITICAL_SECTION cs;

static void logmsg(const char *fmt, ...)
{
    SYSTEMTIME st;
    va_list ap;
    if (!logf) return;
    EnterCriticalSection(&cs);
    GetLocalTime(&st);
    fprintf(logf, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
    LeaveCriticalSection(&cs);
}

static void load_real(void)
{
    /* xinput1_3 comes with the DirectX June 2010 redist; newer Windows only
     * ship xinput1_4 / xinput9_1_0, which have the same entry points. */
    static const char *const names[] = { "\\xinput1_3.dll", "\\xinput1_4.dll", "\\xinput9_1_0.dll" };
    char path[MAX_PATH];
    unsigned i;
    if (real) return;
    for (i = 0; !real && i < sizeof(names) / sizeof(*names); i++)
    {
        GetSystemDirectoryA(path, sizeof(path));
        lstrcatA(path, names[i]);
        real = LoadLibraryA(path);
    }
    if (!real) { logmsg("failed to load the system XInput, error %lu", GetLastError()); return; }
    logmsg("using %s", path);
    p_GetState     = GetProcAddress(real, "XInputGetState");
    p_SetState     = GetProcAddress(real, "XInputSetState");
    p_GetCaps      = GetProcAddress(real, "XInputGetCapabilities");
    p_Enable       = GetProcAddress(real, "XInputEnable");
    p_GetDSound    = GetProcAddress(real, "XInputGetDSoundAudioDeviceGuids");
    p_GetBattery   = GetProcAddress(real, "XInputGetBatteryInformation");
    p_GetKeystroke = GetProcAddress(real, "XInputGetKeystroke");
    p_GetStateEx   = GetProcAddress(real, (LPCSTR)100);
}

static BOOL exe_matches(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    logmsg("exe base %p timestamp %08lx image size %08lx", base, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    return base == (BYTE *)0x400000
        && nt->FileHeader.TimeDateStamp == EXPECTED_TIMESTAMP
        && nt->OptionalHeader.SizeOfImage == EXPECTED_IMAGESIZE;
}

#ifdef DIAG
#include <stdlib.h>
/* Delta snapshots of the exe's .data and Sdata, every 0.5 s, to ss_mem.bin. */
static FILE *memf;
static struct { DWORD start, size; BYTE *prev; } regions[2] = {
    { 0xcf6000, 0xa8000 }, { 0x1a56000, 0x2d4000 },
};

static void diag_snapshot(DWORD index, double t)
{
    int i;
    LONG calls = 0;
    WORD buttons = 0;
    BYTE trig = 0;
    if (!memf) return;
    fwrite("SNAP", 4, 1, memf);
    fwrite(&index, 4, 1, memf);
    fwrite(&t, 8, 1, memf);
    fwrite(&calls, 4, 1, memf);
    fwrite(&buttons, 2, 1, memf);
    fwrite(&trig, 1, 1, memf);
    fwrite(&trig, 1, 1, memf);
    for (i = 0; i < 2; i++)
    {
        DWORD *c = (DWORD *)regions[i].start, *p, n, count = 0;
        long count_pos;
        if (!regions[i].prev) regions[i].prev = calloc(1, regions[i].size);
        p = (DWORD *)regions[i].prev;
        fwrite(&regions[i].start, 4, 1, memf);
        count_pos = ftell(memf);
        fwrite(&count, 4, 1, memf);
        for (n = 0; n < regions[i].size / 4; n++)
        {
            DWORD v = c[n];
            if (index == 0 || v != p[n])
            {
                DWORD off = n * 4;
                fwrite(&off, 4, 1, memf);
                fwrite(&v, 4, 1, memf);
                p[n] = v;
                count++;
            }
        }
        fseek(memf, count_pos, SEEK_SET);
        fwrite(&count, 4, 1, memf);
        fseek(memf, 0, SEEK_END);
    }
    fflush(memf);
}

/* Write the whole in-memory exe image (decrypted by SecuROM) to ss_dump.bin, once. */
static void diag_dump_image(void)
{
    static BOOL done;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL), *zero;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage, off, readable = 0;
    char path[MAX_PATH];
    FILE *f;

    if (done) return;
    done = TRUE;
    if (!GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH)) return;
    lstrcatA(path, "\\Documents\\ss_dump.bin");
    if (!(f = fopen(path, "wb"))) return;
    zero = calloc(1, 0x1000);
    for (off = 0; off < size; off += 0x1000)
    {
        MEMORY_BASIC_INFORMATION mbi;
        BOOL ok = VirtualQuery(base + off, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT
                  && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        fwrite(ok ? base + off : zero, 1, 0x1000, f);
        readable += ok;
    }
    fclose(f);
    free(zero);
    logmsg("image dumped: %lu/%lu pages readable", readable, size / 0x1000);
}

static void diag_keys(void)
{
    static const int vks[] = { VK_ESCAPE, VK_RETURN, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
    static BOOL down[6];
    int k;
    for (k = 0; k < 6; k++)
    {
        BOOL d = (GetAsyncKeyState(vks[k]) & 0x8000) != 0;
        if (d != down[k]) logmsg("KEY vk=%02x %s", vks[k], d ? "down" : "up");
        down[k] = d;
    }
}
#endif

/* ---- Gamepad icons in HUD messages ----
 * Messages such as "PRESS *ACTION_THROTTLE* TO ACCELERATE" are expanded by
 * ExpandActionToken (cdecl, (const char *action, String *out)), which writes
 * the name of the key or axis bound for the device the game thinks is active.
 * The hook looks the action up in the joystick profile instead and writes the
 * HUD image token for that button or axis (*BTN_RT* etc., drawn by Hud.gfx),
 * or a short Xbox label when the HUD has no image for it. */
#define ADDR_EXPAND_ACTION   0x562350
#define ADDR_STRING_ASSIGN   0xa16250   /* String::operator=(const char *), thiscall */
#define ADDR_INPUT_PROFILES  0xd656c8   /* -> object: +0xcf6c count, +0xcf70 profiles */
#define PROFILE_SIZE         0x584
#define PROFILE_TYPE         0x564      /* 1 = keyboard */
#define BINDING_SIZE         20         /* +8 joystick code, +0xc kind */
#define JOY_KIND_BUTTON      0
#define JOY_KIND_AXIS        1

static const BYTE expand_prologue[7] = { 0x6a, 0xff, 0x68, 0x48, 0x6b, 0xb3, 0x00 };
static const BYTE expand_marker[5] = { 0xbf, 0xa0, 0x0c, 0xbd, 0x00 };  /* mov edi, "ACTION_THROTTLE" at +0x8c */

typedef void *(__cdecl *expand_fn)(const char *, void *);
typedef void *(__attribute__((thiscall)) *assign_fn)(void *, const char *);
static expand_fn expand_original;

/* Binding slots, as ExpandActionToken maps the names. */
static const struct { const char *action; int slot; } action_slots[] = {
    { "ACTION_THROTTLE", 0x1c }, { "ACTION_BREAK", 0x1d },
    { "ACTION_TURN_L", 0x1e }, { "ACTION_TURN_R", 0x1f },
    { "ACTION_CAM_SW", 0x24 },
    { "ACTION_LOOK_F", 0x2a }, { "ACTION_LOOK_B", 0x2b },
    { "ACTION_LOOK_L", 0x2c }, { "ACTION_LOOK_R", 0x2d },
    { "ACTION_PP_1", 0x33 }, { "ACTION_PP_2", 0x34 },
};

/* Xbox 360 pad under DirectInput: buttons A B X Y LB RB Back Start LS RS;
 * axes X Y (left stick), Z (LT +, RT -), RX RY (right stick). */
static const char *const button_tokens[] = {
    "*BTN_A*", "*BTN_B*", "X", "Y", "LB", "RB", "Back", "Start", "*BTN_L3*", "RS",
};
static const char *const axis_tokens[] = {
    "*BTN_LS*", "*BTN_LS*", "*BTN_LS*", "*BTN_LS*", "*BTN_LT*", "*BTN_RT*", "RS", "RS", "RS", "RS",
};

static const char *pad_token(int slot)
{
    BYTE *mgr = *(BYTE **)ADDR_INPUT_PROFILES, *profiles;
    int count, p;
    if (!mgr) return NULL;
    count = *(int *)(mgr + 0xcf6c);
    profiles = *(BYTE **)(mgr + 0xcf70);
    for (p = 0; p < count && profiles; p++)
    {
        BYTE *e = profiles + p * PROFILE_SIZE;
        int *binding = (int *)(e + slot * BINDING_SIZE);
        int code = binding[2], kind = binding[3];
        if (*(int *)(e + PROFILE_TYPE) == 1) continue;  /* keyboard */
        if (kind == JOY_KIND_BUTTON && code >= 0 && code < (int)(sizeof(button_tokens) / sizeof(*button_tokens)))
            return button_tokens[code];
        if (kind == JOY_KIND_AXIS && code >= 0 && code < (int)(sizeof(axis_tokens) / sizeof(*axis_tokens)))
            return axis_tokens[code];
        return NULL;
    }
    return NULL;
}

static void *__cdecl expand_action_hook(const char *action, void *out)
{
    unsigned i;
    for (i = 0; action && i < sizeof(action_slots) / sizeof(*action_slots); i++)
        if (!lstrcmpA(action, action_slots[i].action))
        {
            const char *token = pad_token(action_slots[i].slot);
            if (token) return ((assign_fn)ADDR_STRING_ASSIGN)(out, token);
            break;
        }
    return expand_original(action, out);
}

/* Patch once SecuROM has decrypted the code; returns TRUE when done (or impossible). */
static BOOL install_expand_hook(void)
{
    BYTE *target = (BYTE *)ADDR_EXPAND_ACTION, *tramp;
    DWORD old;
    if (memcmp(target, expand_prologue, sizeof(expand_prologue)) || memcmp(target + 0x8c, expand_marker, sizeof(expand_marker)))
        return FALSE;
    tramp = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return TRUE;
    memcpy(tramp, target, 7);
    tramp[7] = 0xe9;
    *(DWORD *)(tramp + 8) = (DWORD)(target + 7) - (DWORD)(tramp + 12);
    expand_original = (expand_fn)tramp;
    if (!VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &old)) return TRUE;
    target[0] = 0xe9;
    *(DWORD *)(target + 1) = (DWORD)expand_action_hook - (DWORD)(target + 5);
    target[5] = target[6] = 0x90;
    VirtualProtect(target, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 7);
    logmsg("action icon hook installed");
    return TRUE;
}

#ifdef DIAG
static void diag_log_bindings(void)
{
    BYTE *mgr = *(BYTE **)ADDR_INPUT_PROFILES, *profiles;
    int count, p, a;
    if (!mgr) return;
    count = *(int *)(mgr + 0xcf6c);
    profiles = *(BYTE **)(mgr + 0xcf70);
    logmsg("input profiles: %d", count);
    for (p = 0; p < count && profiles; p++)
    {
        BYTE *e = profiles + p * PROFILE_SIZE;
        logmsg(" profile %d: type=%d id=%d", p, *(int *)(e + PROFILE_TYPE), *(int *)(e + PROFILE_TYPE + 4));
        for (a = 0x1c; a <= 0x36; a++)
        {
            int *b = (int *)(e + a * BINDING_SIZE);
            logmsg("  action %#x: %d %d %d %d %d", a, b[0], b[1], b[2], b[3], b[4]);
        }
    }
}
#endif

static BOOL in_menu(void)
{
    return *(volatile DWORD *)ADDR_IN_RACE == 0 || *(volatile DWORD *)ADDR_RACE_PAUSED != 0;
}

static void send_key(int in, BOOL down)
{
    INPUT input = {0};
#ifdef DIAG
    logmsg("send vk=%02x %s", keymap[in].vk, down ? "down" : "up");
#endif
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = keymap[in].vk;
    input.ki.wScan = MapVirtualKeyA(keymap[in].vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = (keymap[in].extended ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(input));
}

static void read_inputs(const XGAMEPAD *pad, const BOOL *prev, BOOL *cur)
{
    static const WORD bits[IN_COUNT] = {
        [IN_UP] = XB_DPAD_UP, [IN_DOWN] = XB_DPAD_DOWN, [IN_LEFT] = XB_DPAD_LEFT, [IN_RIGHT] = XB_DPAD_RIGHT,
        [IN_A] = XB_A, [IN_B] = XB_B, [IN_X] = XB_X, [IN_Y] = XB_Y, [IN_START] = XB_START,
    };
    /* Left stick acts as a d-pad, with hysteresis so it does not chatter. */
    int lx = pad->sThumbLX, ly = pad->sThumbLY, i;
    BOOL stick[4];
    stick[IN_UP]    = ly > (prev[IN_UP]    ? STICK_RELEASE : STICK_PRESS);
    stick[IN_DOWN]  = ly < -(prev[IN_DOWN]  ? STICK_RELEASE : STICK_PRESS);
    stick[IN_RIGHT] = lx > (prev[IN_RIGHT] ? STICK_RELEASE : STICK_PRESS);
    stick[IN_LEFT]  = lx < -(prev[IN_LEFT]  ? STICK_RELEASE : STICK_PRESS);
    for (i = 0; i < IN_COUNT; i++)
        cur[i] = (pad->wButtons & bits[i]) != 0 || (i <= IN_RIGHT && stick[i]);
}

static DWORD WINAPI pad_thread(void *arg)
{
    BOOL prev[IN_COUNT] = {0}, armed[IN_COUNT] = {0}, menu_prev = FALSE, hooked = FALSE;
    DWORD next_repeat[IN_COUNT] = {0};
    int i;
    (void)arg;

    if (!exe_matches())
    {
        logmsg("unexpected SplitSecond.exe version, gamepad fix disabled");
        return 0;
    }
    load_real();
    if (!p_GetState) return 0;
    logmsg("gamepad fix active");
#ifdef DIAG
    {
        char mpath[MAX_PATH];
        if (GetEnvironmentVariableA("USERPROFILE", mpath, MAX_PATH))
        {
            lstrcatA(mpath, "\\Documents\\ss_mem.bin");
            memf = fopen(mpath, "wb");
        }
        logmsg("diagnostics enabled");
    }
#endif

    for (;;)
    {
        XSTATE st = {0};
        BOOL cur[IN_COUNT], menu;
        DWORD now = GetTickCount();

        Sleep(10);
        if (!hooked) hooked = install_expand_hook();
        menu = in_menu();
#ifdef DIAG
        {
            static DWORD tick, snap, start;
            if (!start) start = now;
            diag_keys();
            if (tick++ % 50 == 0)
            {
                logmsg("SNAP %lu FLAGS race=%lu x104=%lu", snap, *(volatile DWORD *)ADDR_IN_RACE, *(volatile DWORD *)ADDR_RACE_PAUSED);
                diag_snapshot(snap++, (now - start) / 1000.0);
            }
        }
#endif
        if (((DWORD (WINAPI *)(DWORD, XSTATE *))p_GetState)(0, &st) != ERROR_SUCCESS)
            ZeroMemory(&st, sizeof(st));
        read_inputs(&st.Gamepad, prev, cur);
#ifdef DIAG
        {
            /* Log gamepad activity edges: buttons, triggers, sticks. */
            static WORD last_buttons;
            static BOOL last_lt, last_rt, last_stick;
            const XGAMEPAD *g = &st.Gamepad;
            BOOL lt = g->bLeftTrigger > 40, rt = g->bRightTrigger > 40;
            BOOL stick = abs(g->sThumbLX) > 12000 || abs(g->sThumbLY) > 12000
                      || abs(g->sThumbRX) > 12000 || abs(g->sThumbRY) > 12000;
            if (g->wButtons != last_buttons || lt != last_lt || rt != last_rt || stick != last_stick)
                logmsg("PAD buttons=%04x LT=%d RT=%d stick=%d", g->wButtons, lt, rt, stick);
            last_buttons = g->wButtons; last_lt = lt; last_rt = rt; last_stick = stick;
        }
#endif

        if (menu != menu_prev)
        {
            logmsg("%s", menu ? "menu" : "race");
#ifdef DIAG
            if (!menu)
            {
                diag_log_bindings();
                diag_dump_image();
            }
#endif
            for (i = 0; i < IN_COUNT; i++)
            {
                /* Release anything we hold; inputs already held when the
                 * mode changes must be released before they act again. */
                if (keymap[i].always) continue;
                if (armed[i] && prev[i]) send_key(i, FALSE);
                armed[i] = FALSE;
            }
            menu_prev = menu;
        }

        for (i = 0; i < IN_COUNT; i++)
        {
            BOOL active = menu || keymap[i].always;
            if (!cur[i])
            {
                if (armed[i] && prev[i]) send_key(i, FALSE);
                armed[i] = active;
            }
            else if (armed[i] && active)
            {
                if (!prev[i])
                {
                    send_key(i, TRUE);
                    next_repeat[i] = now + REPEAT_DELAY_MS;
                }
                else if (keymap[i].repeat && (LONG)(now - next_repeat[i]) >= 0)
                {
                    send_key(i, TRUE);
                    next_repeat[i] = now + REPEAT_INTERVAL_MS;
                }
            }
            prev[i] = cur[i];
        }
    }
    return 0;
}

DWORD WINAPI XInputGetState(DWORD i, XSTATE *s)
{
    load_real();
    return p_GetState ? ((DWORD (WINAPI *)(DWORD, XSTATE *))p_GetState)(i, s) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputSetState(DWORD i, XVIB *v)
{
    load_real();
    return p_SetState ? ((DWORD (WINAPI *)(DWORD, XVIB *))p_SetState)(i, v) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD f, void *c)
{
    load_real();
    return p_GetCaps ? ((DWORD (WINAPI *)(DWORD, DWORD, void *))p_GetCaps)(i, f, c) : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI XInputEnable(BOOL e)
{
    load_real();
    if (p_Enable) ((void (WINAPI *)(BOOL))p_Enable)(e);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD i, GUID *a, GUID *b)
{
    load_real();
    return p_GetDSound ? ((DWORD (WINAPI *)(DWORD, GUID *, GUID *))p_GetDSound)(i, a, b) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE t, void *b)
{
    load_real();
    return p_GetBattery ? ((DWORD (WINAPI *)(DWORD, BYTE, void *))p_GetBattery)(i, t, b) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD r, void *k)
{
    load_real();
    return p_GetKeystroke ? ((DWORD (WINAPI *)(DWORD, DWORD, void *))p_GetKeystroke)(i, r, k) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetStateEx(DWORD i, XSTATE *s)
{
    load_real();
    return p_GetStateEx ? ((DWORD (WINAPI *)(DWORD, XSTATE *))p_GetStateEx)(i, s) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        char path[MAX_PATH];
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&cs);
        if (GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH))
        {
            lstrcatA(path, "\\Documents\\ss_xinput_fix.log");
            logf = fopen(path, "w");
        }
        CloseHandle(CreateThread(NULL, 0, pad_thread, NULL, 0, NULL));
    }
    return TRUE;
}
