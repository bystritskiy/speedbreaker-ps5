// SpeedBreaker PS5: the DualSense as an SDL gamepad. GPL-3.0-or-later.
//
// The console's pad comes through libScePad (public exports, no SDK headers:
// the data layout is checked on a console). It is attached as
// one SDL virtual gamepad, so the runtime's input (hid/input.cpp) sees an
// ordinary controller: buttons and axes are set in the virtual joystick's
// Update, which SDL calls while it updates joysticks, and rumble goes to
// scePadSetVibration. Options is Start and the touchpad click is Back, so
// Back + Start opens SpeedBreaker's settings as on any controller.
//
// One player, the initial user's pad, reopened a second after it fails (a
// pad switched off or reconnected).
#include "ps5_host.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

extern "C"
{
    int sceUserServiceInitialize(const void* params);
    int sceUserServiceGetInitialUser(int* user);
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

        std::mutex s_mutex;
        int s_handle = -1;
        bool s_ownHandle = false;
        time_t s_nextOpen = 0;
        SDL_JoystickID s_id = 0;

        // Opens the initial user's pad (or takes the handle the system has).
        bool OpenLocked()
        {
            if (s_handle >= 0)
                return true;
            time_t now = time(nullptr);
            if (now < s_nextOpen)
                return false;
            s_nextOpen = now + 1;
            int user = -1;
            if (sceUserServiceGetInitialUser(&user) < 0)
                return false;
            s_handle = scePadOpen(user, 0, 0, nullptr);
            s_ownHandle = s_handle >= 0;
            if (s_handle < 0)
                s_handle = scePadGetHandle(user, 0, 0);
            if (s_handle < 0)
                return false;
            scePadSetVibrationMode(s_handle, 2);
            fprintf(stderr, "[ps5] pad opened (user %d, handle %d)\n", user, s_handle);
            return true;
        }

        void CloseLocked()
        {
            if (s_handle >= 0 && s_ownHandle)
                scePadClose(s_handle);
            s_handle = -1;
            s_ownHandle = false;
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
            (void)userdata;
            SDL_Joystick* joystick = SDL_GetJoystickFromID(s_id);
            if (!joystick)
                return;
            PadData data{};
            bool ok = false;
            {
                std::lock_guard lock(s_mutex);
                if (OpenLocked())
                {
                    if (scePadReadState(s_handle, &data) < 0)
                        CloseLocked();
                    else
                        ok = data.connected != 0;
                }
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
            (void)userdata;
            const uint8_t motors[2] = { uint8_t(low >> 8), uint8_t(high >> 8) };  // large, small
            std::lock_guard lock(s_mutex);
            return s_handle >= 0 && scePadSetVibration(s_handle, motors) >= 0;
        }
    }

    void Attach()
    {
        static bool attached = false;
        if (attached)
            return;
        attached = true;
        sceUserServiceInitialize(nullptr);
        scePadInit();

        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0x054C;   // Sony
        desc.product_id = 0x0CE6;  // DualSense
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "DualSense";
        desc.Update = Update;
        desc.Rumble = Rumble;
        s_id = SDL_AttachVirtualJoystick(&desc);
        if (!s_id)
            fprintf(stderr, "[ps5] SDL_AttachVirtualJoystick: %s\n", SDL_GetError());
        else
            fprintf(stderr, "[ps5] DualSense attached as SDL gamepad %u\n", unsigned(s_id));
    }

    void Shutdown()
    {
        const uint8_t stop[2] = { 0, 0 };
        std::lock_guard lock(s_mutex);
        if (s_handle >= 0)
            scePadSetVibration(s_handle, stop);
        CloseLocked();
    }
}
