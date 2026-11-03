// SpeedBreaker PS5: the DualSense as an SDL gamepad. GPL-3.0-or-later.
//
// The console's pads come through libScePad (public exports, no SDK headers:
// the data layout is checked on a console). Each is attached as an SDL
// virtual gamepad, so the runtime's input (hid/input.cpp) sees ordinary
// controllers: buttons and axes are set in the virtual joystick's Update,
// which SDL calls while it updates joysticks, and rumble goes to
// scePadSetVibration. Options is Start and the touchpad click is Back, so
// Back + Start opens SpeedBreaker's settings as on any controller.
//
// Two players, as split screen has (hid/player_slots.h makes the second a
// player). A PS5 pad belongs to a signed-in user, so each player is a user:
//
//   - Player 1 is the initial user's pad: its gamepad is always attached,
//     idle while the pad is off. Its pad is reopened a second after it fails
//     (a pad switched off or reconnected).
//   - Player 2 is another signed-in user (a guest will do) whose pad is
//     connected; one without a pad doesn't hide a later user's. Its gamepad
//     is attached while the pad is connected and detached when it goes, so
//     the runtime sees it plugged in and removed (in a race the game pauses
//     until it's back). The user keeps the slot until they sign out.
//   - Users are checked once a second (Poll, from hid::Update); a failed
//     sign-in list keeps both players as they are.
//
// tests/ps5_pad_test.cpp drives this file with a stand-in libScePad.
#include "ps5_host.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

extern "C"
{
    // The public UserService ABI: four user ids, unused entries -1.
    struct SceUserServiceLoginUserIdList
    {
        int userId[4];
    };
    int sceUserServiceInitialize(const void* params);
    int sceUserServiceGetInitialUser(int* user);
    int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list);
    int scePadInit(void);
    int scePadOpen(int user, int type, int index, const void* param);
    int scePadGetHandle(int user, int type, int index);
    int scePadClose(int handle);
    int scePadReadState(int handle, void* data);
    int scePadSetVibration(int handle, const void* param);
    int scePadSetVibrationMode(int handle, int mode);
}

namespace ps5::pad
{
    namespace
    {
        // ScePadData's public prefix.
        struct PadData
        {
            uint32_t buttons;
            uint8_t lx, ly, rx, ry, l2, r2;
            uint8_t reserved[66];
            uint8_t connected;
            uint8_t tail[179];
        };
        static_assert(offsetof(PadData, connected) == 76, "libScePad connected ABI");
        static_assert(sizeof(PadData) == 256, "libScePad data size");

        enum : uint32_t
        {
            kL3 = 0x02, kR3 = 0x04, kOptions = 0x08,
            kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80,
            kL2 = 0x100, kR2 = 0x200, kL1 = 0x400, kR1 = 0x800,
            kTriangle = 0x1000, kCircle = 0x2000, kCross = 0x4000, kSquare = 0x8000,
            kTouchPad = 0x100000,
        };

        constexpr int kPlayers = 2;
        constexpr int kNoUser = -1;
        constexpr uint64_t kScanMs = 1000;

        struct Slot
        {
            int user = kNoUser;
            int handle = -1;
            bool owned = false;      // opened here (else the system's handle, not ours to close)
            bool connected = false;  // its pad, as last read
        };

        std::mutex s_mutex;  // the slots; never held across an SDL call (SDL's joystick lock comes first)
        Slot s_slots[kPlayers];
        uint64_t s_nextScan = 0;
        std::atomic<SDL_JoystickID> s_ids[kPlayers];  // each player's SDL gamepad (0: not attached); main thread writes
        bool s_attached = false;

        bool ValidUser(int user) { return user >= 0 && user != 255; }

        void NoteConnected(Slot& slot, bool connected)
        {
            if (slot.connected == connected)
                return;
            slot.connected = connected;
            fprintf(stderr, "[ps5] player %d's pad (user %d) %s\n", int(&slot - s_slots) + 1, slot.user,
                connected ? "connected" : "disconnected");
        }

        void ReleaseLocked(Slot& slot)
        {
            if (slot.handle >= 0)
            {
                const uint8_t stop[2] = { 0, 0 };
                scePadSetVibration(slot.handle, stop);
                if (slot.owned)
                    scePadClose(slot.handle);
            }
            NoteConnected(slot, false);
            slot.handle = -1;
            slot.owned = false;
        }

        // Opens the user's pad (or takes the handle the system has).
        void OpenLocked(Slot& slot)
        {
            if (slot.handle >= 0 || !ValidUser(slot.user))
                return;
            slot.handle = scePadOpen(slot.user, 0, 0, nullptr);
            slot.owned = slot.handle >= 0;
            if (slot.handle < 0)
                slot.handle = scePadGetHandle(slot.user, 0, 0);
            if (slot.handle < 0)
                return;
            scePadSetVibrationMode(slot.handle, 2);
            fprintf(stderr, "[ps5] player %d's pad opened (user %d, handle %d)\n", int(&slot - s_slots) + 1, slot.user,
                slot.handle);
        }

        // Reads the slot's pad; a failed read closes it (reopened by the next scan).
        bool ReadLocked(Slot& slot, PadData& data)
        {
            data = PadData{};
            if (slot.handle < 0)
                return false;
            if (scePadReadState(slot.handle, &data) < 0)
            {
                ReleaseLocked(slot);
                return false;
            }
            NoteConnected(slot, data.connected != 0);
            return slot.connected;
        }

        bool Contains(const SceUserServiceLoginUserIdList& users, int user)
        {
            for (int u : users.userId)
                if (u == user)
                    return true;
            return false;
        }

        void ScanLocked(uint64_t nowMs)
        {
            if (nowMs < s_nextScan)
                return;
            s_nextScan = nowMs + kScanMs;
            SceUserServiceLoginUserIdList users = { { -1, -1, -1, -1 } };
            const bool listed = sceUserServiceGetLoginUserIdList(&users) >= 0;
            int initial = kNoUser;
            if (sceUserServiceGetInitialUser(&initial) < 0)
                initial = kNoUser;
            // A user who signed out leaves their slot.
            for (Slot& slot : s_slots)
                if (listed && ValidUser(slot.user) && !Contains(users, slot.user))
                {
                    ReleaseLocked(slot);
                    slot.user = kNoUser;
                }
            // Player 1 stays the initial user's, its pad on or off, and never
            // shares a user with player 2.
            if (!ValidUser(s_slots[0].user) && ValidUser(initial) && (!listed || Contains(users, initial)))
            {
                if (s_slots[1].user == initial)
                {
                    ReleaseLocked(s_slots[1]);
                    s_slots[1].user = kNoUser;
                }
                s_slots[0].user = initial;
            }
            for (Slot& slot : s_slots)
                OpenLocked(slot);
            if (!listed)
                return;
            PadData probe;
            if (ReadLocked(s_slots[1], probe))
                return;
            // Player 2's user has no pad on: the first other signed-in user
            // whose pad is takes the slot.
            for (int user : users.userId)
            {
                if (!ValidUser(user) || user == s_slots[0].user || user == s_slots[1].user)
                    continue;
                Slot candidate;
                candidate.user = user;
                candidate.handle = scePadOpen(user, 0, 0, nullptr);
                candidate.owned = candidate.handle >= 0;
                if (candidate.handle < 0)
                    candidate.handle = scePadGetHandle(user, 0, 0);
                if (candidate.handle < 0)
                    continue;
                if (scePadReadState(candidate.handle, &probe) >= 0 && probe.connected)
                {
                    ReleaseLocked(s_slots[1]);
                    scePadSetVibrationMode(candidate.handle, 2);
                    s_slots[1] = candidate;
                    fprintf(stderr, "[ps5] player 2's pad opened (user %d, handle %d)\n", user, candidate.handle);
                    NoteConnected(s_slots[1], true);
                    return;
                }
                if (candidate.owned)
                    scePadClose(candidate.handle);
            }
        }

        Sint16 Stick(uint8_t value)
        {
            int v = (int(value) - 128) * 257;
            return Sint16(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }

        // A virtual gamepad's trigger is the joystick axis' whole range, released
        // at its minimum (SDL_virtualjoystick.c).
        Sint16 Trigger(uint8_t value)
        {
            return Sint16(int(value) * 257 - 32768);
        }

        void SDLCALL Update(void* userdata)
        {
            const int player = int(reinterpret_cast<intptr_t>(userdata));
            SDL_Joystick* joystick = SDL_GetJoystickFromID(s_ids[player].load(std::memory_order_relaxed));
            if (!joystick)
                return;
            PadData data;
            bool ok;
            {
                std::lock_guard lock(s_mutex);
                ok = ReadLocked(s_slots[player], data);
            }
            if (!ok)
                data = PadData{ 0, 128, 128, 128, 128, 0, 0, {}, 0, {} };
            const uint32_t b = data.buttons;
            static const struct { uint32_t bit; SDL_GamepadButton button; } kButtons[] = {
                { kCross, SDL_GAMEPAD_BUTTON_SOUTH }, { kCircle, SDL_GAMEPAD_BUTTON_EAST },
                { kSquare, SDL_GAMEPAD_BUTTON_WEST }, { kTriangle, SDL_GAMEPAD_BUTTON_NORTH },
                { kTouchPad, SDL_GAMEPAD_BUTTON_BACK }, { kOptions, SDL_GAMEPAD_BUTTON_START },
                { kL3, SDL_GAMEPAD_BUTTON_LEFT_STICK }, { kR3, SDL_GAMEPAD_BUTTON_RIGHT_STICK },
                { kL1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { kR1, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
                { kUp, SDL_GAMEPAD_BUTTON_DPAD_UP }, { kDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN },
                { kLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { kRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
            };
            for (const auto& m : kButtons)
                SDL_SetJoystickVirtualButton(joystick, m.button, (b & m.bit) != 0);
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, Stick(data.lx));
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, Stick(data.ly));
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, Stick(data.rx));
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, Stick(data.ry));
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, Trigger(data.l2));
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, Trigger(data.r2));
        }

        bool SDLCALL Rumble(void* userdata, Uint16 low, Uint16 high)
        {
            const int player = int(reinterpret_cast<intptr_t>(userdata));
            const uint8_t motors[2] = { uint8_t(low >> 8), uint8_t(high >> 8) };  // large, small
            std::lock_guard lock(s_mutex);
            const Slot& slot = s_slots[player];
            return slot.handle >= 0 && slot.connected && scePadSetVibration(slot.handle, motors) >= 0;
        }

        void AttachPlayer(int player)
        {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.vendor_id = 0x054C;   // Sony
            desc.product_id = 0x0CE6;  // DualSense
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = player == 0 ? "DualSense" : "DualSense 2";
            desc.userdata = reinterpret_cast<void*>(intptr_t(player));
            desc.Update = Update;
            desc.Rumble = Rumble;
            const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
            s_ids[player].store(id, std::memory_order_relaxed);
            if (!id)
                fprintf(stderr, "[ps5] SDL_AttachVirtualJoystick (player %d): %s\n", player + 1, SDL_GetError());
            else
                fprintf(stderr, "[ps5] player %d's DualSense attached as SDL gamepad %u\n", player + 1, unsigned(id));
        }

        void DetachPlayer(int player)
        {
            const SDL_JoystickID id = s_ids[player].exchange(0, std::memory_order_relaxed);
            if (!id)
                return;
            SDL_DetachVirtualJoystick(id);
            fprintf(stderr, "[ps5] player %d's DualSense detached (SDL gamepad %u)\n", player + 1, unsigned(id));
        }
    }

    // Poll with the clock given (tests/ps5_pad_test.cpp steps it).
    void PollAt(uint64_t nowMs)
    {
        if (!s_attached)
            return;
        bool second;
        {
            std::lock_guard lock(s_mutex);
            ScanLocked(nowMs);
            second = s_slots[1].connected;
        }
        const bool attached = s_ids[1].load(std::memory_order_relaxed) != 0;
        if (second && !attached)
            AttachPlayer(1);
        else if (!second && attached)
            DetachPlayer(1);
    }

    void Poll()
    {
        PollAt(SDL_GetTicks());
    }

    void Attach()
    {
        if (s_attached)
            return;
        s_attached = true;
        sceUserServiceInitialize(nullptr);
        scePadInit();
        AttachPlayer(0);
        Poll();  // player 1's pad now, and player 2's if one is on already
    }

    void Shutdown()
    {
        std::lock_guard lock(s_mutex);
        for (Slot& slot : s_slots)
        {
            ReleaseLocked(slot);
            slot.user = kNoUser;
        }
        s_nextScan = 0;
    }
}
