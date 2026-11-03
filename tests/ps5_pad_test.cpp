// Tests for port/ps5/src/pad_ps5.cpp: two players' DualSense pads as SDL
// gamepads, over a stand-in libScePad and UserService (users 10, 20, 30, 40;
// pad handles 100-103). Player 2's gamepad comes and goes with their pad, each
// player's buttons and rumble stay their own, and sign-outs, failed reads and
// a failed sign-in list keep player numbers where they were. From the repo
// root, with SDL3 installed:
//   clang++ -std=c++20 -O2 -UNDEBUG -Iport/ps5/src tests/ps5_pad_test.cpp port/ps5/src/pad_ps5.cpp \
//     $(pkg-config --cflags --libs sdl3) -o build/ps5_pad_test && build/ps5_pad_test
#include "ps5_host.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ps5::pad
{
    void PollAt(uint64_t nowMs);
}

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The stand-in console.
static int g_users[4] = { 10, 20, -1, -1 }, g_initial = 10;
static bool g_listFails = false;
static bool g_live[4], g_borrowed[4], g_open[4], g_readFails[4];
static int g_closes[4];
static uint32_t g_buttons[4];
static uint8_t g_motors[4][2];

static int IndexOf(int user)
{
    assert(user >= 10 && user <= 40 && user % 10 == 0);
    return user / 10 - 1;
}
static int IndexOfHandle(int handle)
{
    assert(handle >= 100 && handle < 104);
    return handle - 100;
}

extern "C"
{
    struct SceUserServiceLoginUserIdList
    {
        int userId[4];
    };
    int sceUserServiceInitialize(const void*) { return 0; }
    int sceUserServiceGetInitialUser(int* user)
    {
        *user = g_initial;
        return g_initial < 0 ? -1 : 0;
    }
    int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list)
    {
        if (g_listFails)
            return -1;
        memcpy(list->userId, g_users, sizeof(g_users));
        return 0;
    }
    int scePadInit(void) { return 0; }
    int scePadOpen(int user, int type, int index, const void* param)
    {
        assert(!type && !index && !param);
        const int i = IndexOf(user);
        if (g_borrowed[i])
            return -1;  // the system has it open: scePadGetHandle
        assert(!g_open[i]);
        g_open[i] = true;
        return 100 + i;
    }
    int scePadGetHandle(int user, int, int)
    {
        const int i = IndexOf(user);
        return g_borrowed[i] ? 100 + i : -1;
    }
    int scePadClose(int handle)
    {
        const int i = IndexOfHandle(handle);
        assert(!g_borrowed[i] && g_open[i]);
        g_open[i] = false;
        g_closes[i]++;
        return 0;
    }
    int scePadReadState(int handle, void* data)
    {
        const int i = IndexOfHandle(handle);
        assert(g_open[i] || g_borrowed[i]);
        if (g_readFails[i])
            return -1;
        uint8_t* d = static_cast<uint8_t*>(data);
        memset(d, 0, 256);
        memcpy(d, &g_buttons[i], 4);
        memset(d + 4, 128, 4);  // sticks at rest
        d[76] = g_live[i];
        return 0;
    }
    int scePadSetVibration(int handle, const void* param)
    {
        memcpy(g_motors[IndexOfHandle(handle)], param, 2);
        return 0;
    }
    int scePadSetVibrationMode(int handle, int mode)
    {
        IndexOfHandle(handle);
        assert(mode == 2);
        return 0;
    }
}

static uint64_t g_now = 1000;

// A scan a second later, then the SDL events that brings (each player's
// gamepad attached or detached).
static void Tick()
{
    g_now += 1000;
    ps5::pad::PollAt(g_now);
    SDL_UpdateGamepads();
    ps5::pad::PollAt(g_now);  // a pad SDL's update found gone goes at once
    SDL_PumpEvents();
}

// The gamepad whose joystick has that name (0: none).
static SDL_JoystickID Find(const char* name)
{
    SDL_JoystickID found = 0;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; i++)
    {
        const char* n = SDL_GetJoystickNameForID(ids[i]);  // (the gamepad name is the mapping's, one for both)
        if (n && strcmp(n, name) == 0)
            found = ids[i];
    }
    SDL_free(ids);
    return found;
}

int main()
{
    // Only the virtual pads: nothing plugged into this machine.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // Player 1 alone: user 20 is signed in with no pad on.
    g_live[0] = true;
    g_buttons[0] = 0x4000;  // Cross
    g_buttons[1] = 0x2000;  // Circle
    ps5::pad::Attach();
    SDL_PumpEvents();
    const SDL_JoystickID one = Find("DualSense");
    CHECK(one != 0, "player 1's gamepad");
    CHECK(Find("DualSense 2") == 0, "no player 2 without a pad");
    CHECK(g_open[0] && !g_open[1] && g_closes[1] == 1, "user 20's pad looked at and closed");
    SDL_Gamepad* p1 = SDL_OpenGamepad(one);
    CHECK(p1 != nullptr, "%s", SDL_GetError());

    // User 20's pad on: player 2 arrives, its buttons its own.
    g_live[1] = true;
    Tick();
    SDL_JoystickID two = Find("DualSense 2");
    CHECK(two != 0 && two != one, "player 2's gamepad");
    SDL_Gamepad* p2 = SDL_OpenGamepad(two);
    CHECK(p2 != nullptr, "%s", SDL_GetError());
    SDL_UpdateGamepads();
    CHECK(SDL_GetGamepadButton(p1, SDL_GAMEPAD_BUTTON_SOUTH) && !SDL_GetGamepadButton(p1, SDL_GAMEPAD_BUTTON_EAST), "p1 Cross");
    CHECK(SDL_GetGamepadButton(p2, SDL_GAMEPAD_BUTTON_EAST) && !SDL_GetGamepadButton(p2, SDL_GAMEPAD_BUTTON_SOUTH), "p2 Circle");
    CHECK(SDL_GetGamepadAxis(p2, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == 0 && SDL_GetGamepadAxis(p2, SDL_GAMEPAD_AXIS_LEFTX) == 0,
        "trigger and stick at rest");

    // Rumble: each to its own pad.
    CHECK(SDL_RumbleGamepad(p1, 0xabcd, 0x1234, 1000), "%s", SDL_GetError());
    CHECK(SDL_RumbleGamepad(p2, 0x4567, 0x89ab, 1000), "%s", SDL_GetError());
    CHECK(g_motors[0][0] == 0xab && g_motors[0][1] == 0x12, "p1 motors %02x %02x", g_motors[0][0], g_motors[0][1]);
    CHECK(g_motors[1][0] == 0x45 && g_motors[1][1] == 0x89, "p2 motors %02x %02x", g_motors[1][0], g_motors[1][1]);

    // Scans a second apart: polling every frame doesn't open anything again.
    for (int i = 0; i < 20; i++)
        ps5::pad::PollAt(g_now);
    CHECK(g_closes[1] == 1, "no rescans within the second");

    // The sign-in list reordered: player numbers stay.
    g_users[0] = 20;
    g_users[1] = 10;
    Tick();
    CHECK(Find("DualSense 2") == two, "player 2 kept");

    // Player 2's pad off mid-race: its gamepad goes (SDL's removal; the game
    // pauses), and comes back as a new one when it's on again.
    g_live[1] = false;
    Tick();
    CHECK(Find("DualSense 2") == 0, "player 2's gamepad detached");
    CHECK(!SDL_GamepadConnected(p2), "p2 closed by SDL");
    SDL_CloseGamepad(p2);
    p2 = nullptr;
    CHECK(Find("DualSense") == one, "player 1 stays");
    g_live[1] = true;
    Tick();
    two = Find("DualSense 2");
    CHECK(two != 0, "player 2 back");

    // Player 1's pad off: its gamepad stays, idle, and player 2 isn't moved.
    g_live[0] = false;
    Tick();
    SDL_UpdateGamepads();
    CHECK(Find("DualSense") == one && !SDL_GetGamepadButton(p1, SDL_GAMEPAD_BUTTON_SOUTH), "p1 idle");
    g_motors[0][0] = g_motors[0][1] = 0;
    CHECK(!SDL_RumbleGamepad(p1, 0xffff, 0xffff, 100) && g_motors[0][0] == 0, "no rumble to a pad that's off");
    CHECK(Find("DualSense 2") == two, "player 2 kept while player 1's pad is off");
    g_live[0] = true;
    Tick();

    // A failed read closes the pad and drops player 2; the next scan reopens it.
    g_readFails[1] = true;
    Tick();
    CHECK(Find("DualSense 2") == 0 && !g_open[1] && g_motors[1][0] == 0, "read failure: closed, motors stopped");
    g_readFails[1] = false;
    Tick();
    CHECK(Find("DualSense 2") != 0 && g_open[1], "reopened");

    // A failed sign-in list keeps both players.
    g_listFails = true;
    Tick();
    CHECK(Find("DualSense") == one && Find("DualSense 2") != 0, "both kept");
    g_listFails = false;

    // User 20 signs out; user 30 has no pad on, user 40's pad is the
    // system's handle: player 2 is user 40, and user 30's handle is closed.
    g_users[0] = 10;
    g_users[1] = 30;
    g_users[2] = 40;
    g_live[3] = true;
    g_borrowed[3] = true;
    g_buttons[3] = 0x8000;  // Square
    Tick();
    Tick();  // (the sign-out detaches; the next scan finds user 40)
    two = Find("DualSense 2");
    CHECK(two != 0, "player 2 is user 40");
    CHECK(!g_open[1] && !g_open[2], "users 20 and 30 closed");
    p2 = SDL_OpenGamepad(two);
    SDL_UpdateGamepads();
    CHECK(p2 && SDL_GetGamepadButton(p2, SDL_GAMEPAD_BUTTON_WEST), "user 40's Square");
    CHECK(p2 && SDL_RumbleGamepad(p2, 0x4500, 0, 100) && g_motors[3][0] == 0x45, "user 40's rumble");

    // Shutdown: motors stopped, our handles closed, the system's left open.
    ps5::pad::Shutdown();
    CHECK(!g_open[0] && !g_open[1] && !g_open[2] && !g_open[3], "all closed");
    CHECK(g_closes[3] == 0 && g_motors[3][0] == 0, "borrowed handle stopped, not closed");

    if (p2)
        SDL_CloseGamepad(p2);
    SDL_CloseGamepad(p1);
    SDL_Quit();
    if (g_failures)
    {
        printf("ps5_pad_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("ps5_pad_test: two players, hotplug, rumble, sign-outs and failures: pass\n");
    return 0;
}
