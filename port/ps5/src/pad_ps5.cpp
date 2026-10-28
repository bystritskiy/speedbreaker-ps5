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
#include <iterator>
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
    int scePadSetTriggerEffect(int handle, const void* param);
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

        // ScePadTriggerEffectParam, as the public reverse-engineered headers
        // lay it out (not yet checked on a console): a mask of the triggers
        // it sets, then one command per trigger, L2 first. A command's data
        // is a 48-byte union. Positions are 0-9 of the trigger's travel,
        // strengths and amplitudes 1-8, a frequency in Hz.
        enum : uint8_t { kTriggerL2 = 0x01, kTriggerR2 = 0x02 };
        enum : uint32_t
        {
            kTriggerEffectOff = 0, kTriggerEffectFeedback = 1, kTriggerEffectVibration = 3,
            kTriggerEffectSlopeFeedback = 5,
        };
        struct TriggerEffectCommand
        {
            uint32_t mode;
            uint8_t padding[4];
            union
            {
                struct { uint8_t position, strength; } feedback;
                struct { uint8_t position, amplitude, frequency; } vibration;
                struct { uint8_t startPosition, endPosition, startStrength, endStrength; } slope;
                uint8_t data[48];
            };
        };
        struct TriggerEffectParam
        {
            uint8_t triggerMask;
            uint8_t padding[7];
            TriggerEffectCommand command[2];  // L2, R2
        };
        static_assert(sizeof(TriggerEffectCommand) == 56, "libScePad trigger effect command ABI");
        static_assert(sizeof(TriggerEffectParam) == 120, "libScePad trigger effect ABI");

        // Trigger Resistance's levels: the accelerator's steady strength, and
        // the brake's, which builds up along its travel like a pedal's
        // (start, end).
        struct Level
        {
            uint8_t accelerator, brakeStart, brakeEnd;
        };
        constexpr Level kLevels[] = {
            { 0, 0, 0 },  // Off
            { 2, 1, 4 },  // Light
            { 3, 2, 6 },  // Medium
            { 5, 3, 8 },  // Strong
        };

        // Trigger Effects (the dynamic part, on top of the resistance):
        // - the game's own rumble (crashes, bumps, kerbs, rough ground) above
        //   kRumbleThreshold shakes the accelerator, as strongly as the game
        //   rumbles: low notes for the big motor, high ones for the small;
        // - nitrous (Circle, the 360's B) held with the accelerator down
        //   buzzes the accelerator harder.
        // First guesses, to be tuned on a console: the log's "[ps5] triggers:
        // game rumble peak" line gives what the game sends.
        constexpr float kRumbleThreshold = 0.35f;
        constexpr uint32_t kNitrous = 0x2000;  // kCircle
        constexpr uint8_t kNitrousAccelerator = 77;  // r2 above 30%
        // A change is sent at most this often (ms), but a stop or start of
        // an effect straight away.
        constexpr uint32_t kSendInterval = 50;

        std::mutex s_mutex;
        int s_handle = -1;
        bool s_ownHandle = false;
        time_t s_nextOpen = 0;
        SDL_JoystickID s_id = 0;
        // What Settings asks for, and what the game and the pad say now.
        int s_triggerLevel = 0;
        bool s_triggerEffects = false;
        uint16_t s_rumbleLow = 0, s_rumbleHigh = 0;
        uint32_t s_buttons = 0;
        uint8_t s_r2 = 0;
        // What the open pad was sent (sentValid false: nothing yet).
        TriggerEffectParam s_sent{};
        bool s_sentValid = false;
        Uint64 s_sentAt = 0;
        // The game's strongest rumble in the last 5 s, for the log.
        uint16_t s_peakLow = 0, s_peakHigh = 0;
        Uint64 s_peakSince = 0;

        uint8_t Clamp8(int v, int lo, int hi)
        {
            return uint8_t(v < lo ? lo : v > hi ? hi : v);
        }

        TriggerEffectParam DesiredLocked()
        {
            TriggerEffectParam param{};
            param.triggerMask = kTriggerL2 | kTriggerR2;
            const Level& l = kLevels[s_triggerLevel];
            TriggerEffectCommand& brake = param.command[0];
            TriggerEffectCommand& accel = param.command[1];
            if (l.brakeEnd)
            {
                brake.mode = kTriggerEffectSlopeFeedback;
                brake.slope = { 1, 9, l.brakeStart, l.brakeEnd };
            }
            if (l.accelerator)
            {
                accel.mode = kTriggerEffectFeedback;
                accel.feedback = { 1, l.accelerator };
            }
            if (!s_triggerEffects)
                return param;
            const float low = s_rumbleLow / 65535.0f, high = s_rumbleHigh / 65535.0f;
            const float rumble = low > high ? low : high;
            if ((s_buttons & kNitrous) && s_r2 > kNitrousAccelerator)
            {
                accel.mode = kTriggerEffectVibration;
                accel.vibration = { 2, Clamp8(5 + s_triggerLevel, 1, 8), 60 };
            }
            else if (rumble > kRumbleThreshold)
            {
                accel.mode = kTriggerEffectVibration;
                accel.vibration = { 1, Clamp8(int(rumble * 8.0f + 0.5f), 1, 8), uint8_t(high > low ? 55 : 25) };
            }
            return param;
        }

        // Sends what the triggers should do now, if the open pad doesn't have it.
        void SendTriggersLocked(bool force = false)
        {
            if (s_handle < 0)
                return;
            const Uint64 now = SDL_GetTicks();
            if (s_triggerEffects)
            {
                if (!s_peakSince)
                    s_peakSince = now;
                if (s_rumbleLow > s_peakLow) s_peakLow = s_rumbleLow;
                if (s_rumbleHigh > s_peakHigh) s_peakHigh = s_rumbleHigh;
                if (now - s_peakSince >= 5000)
                {
                    if (s_peakLow || s_peakHigh)
                        fprintf(stderr, "[ps5] triggers: game rumble peak low %.2f, high %.2f\n", s_peakLow / 65535.0, s_peakHigh / 65535.0);
                    s_peakLow = s_peakHigh = 0;
                    s_peakSince = now;
                }
            }
            const TriggerEffectParam want = DesiredLocked();
            if (s_sentValid && memcmp(&want, &s_sent, sizeof want) == 0)
                return;
            const bool modeChanged = !s_sentValid || want.command[0].mode != s_sent.command[0].mode
                || want.command[1].mode != s_sent.command[1].mode;
            if (!force && !modeChanged && now - s_sentAt < kSendInterval)
                return;
            const int result = scePadSetTriggerEffect(s_handle, &want);
            // Every send while an effect runs would be a line a frame: only
            // the changes of mode, and failures.
            if (modeChanged || result < 0)
                fprintf(stderr, "[ps5] triggers: resistance %d, effects %s, L2 mode %u, R2 mode %u: scePadSetTriggerEffect %s (0x%08x)\n",
                    s_triggerLevel, s_triggerEffects ? "on" : "off", unsigned(want.command[0].mode), unsigned(want.command[1].mode),
                    result >= 0 ? "ok" : "failed", unsigned(result));
            // A failure isn't retried every frame: the next change tries again.
            s_sent = want;
            s_sentValid = true;
            s_sentAt = now;
        }

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
            s_sentValid = false;
            SendTriggersLocked(true);
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
                    s_buttons = ok ? data.buttons : 0;
                    s_r2 = ok ? data.r2 : 0;
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
            s_rumbleLow = low;
            s_rumbleHigh = high;
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
        s_triggerLevel = 0;
        s_triggerEffects = false;
        SendTriggersLocked(true);
        CloseLocked();
    }

    void SetTriggers(int level, bool effects)
    {
        level = level < 0 ? 0 : level >= int(std::size(kLevels)) ? int(std::size(kLevels)) - 1 : level;
        std::lock_guard lock(s_mutex);
        s_triggerLevel = level;
        s_triggerEffects = effects;
        SendTriggersLocked();
    }
}
