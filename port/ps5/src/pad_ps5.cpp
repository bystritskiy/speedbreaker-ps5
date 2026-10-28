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
#include <user/paths.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <mutex>
#include <string>

#include <sys/stat.h>

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
            { 3, 2, 5 },  // Medium
            { 5, 3, 7 },  // Strong
        };

        // Trigger Effects, from the car (game/car_telemetry.h). Tuned by hand
        // on an iPhone with a DualSense through Apple's GameController, whose
        // scale is 0-1 for everything; the Hz below are that scale x 255, a
        // guess at its mapping: tune them on the console (triggers.txt).
        // - The brake firmer the faster the car goes: 60% of the level at a
        //   standstill, 100% from kFullBrakeSpeed.
        // - ABS: braking past half with the car slowing faster than
        //   kAbsDecel: a full-amplitude, low-frequency vibration on the brake
        //   (separate knocks, not a buzz).
        // - A gear change (the game's own gear, so automatic too): one hard
        //   knock on the accelerator, then the trigger at its hardest for a
        //   moment.
        // - The rev limiter with the accelerator past half: the controller's
        //   motors buzz (Rumble, on top of the game's own), not the trigger.
        constexpr float kFullBrakeSpeed = 45;  // m/s (160 km/h)
        constexpr float kAbsDecel = 9;         // m/s^2
        constexpr float kLimiter = 0.97f;      // of the redline
        constexpr uint8_t kHalf = 128;         // a trigger past half (of 255)
        constexpr uint8_t kLimiterLow = 0x30, kLimiterHigh = 0x90;
        // Changes within an effect are sent at most this often (ms); a
        // change of mode at once.
        constexpr uint32_t kSendInterval = 50;

        // The knocks' shape: /data/speedbreaker/triggers.txt ("key = value"
        // lines, read again within a second of a change; FTP it over).
        //   abs_amp (1-8), abs_freq (Hz)          ABS
        //   kick_amp (1-8), kick_freq (Hz)        a gear change's knock...
        //   kick_thump_ms, kick_hold_ms           ...and how long it and the hold after it last
        // The defaults are the values that felt right on the iPhone: one knock
        // per gear change (two at 160 ms), ABS knocks that hit the finger.
        struct Tuning
        {
            uint8_t absAmp = 8, absFreq = 13;
            uint8_t kickAmp = 8, kickFreq = 20;
            uint32_t kickThumpMs = 80, kickHoldMs = 60;
        };
        Tuning s_tuning;
        Uint64 s_tuningCheck = 0;
        long long s_tuningStamp = -1;

        void LoadTuning(Uint64 now)
        {
            if (now < s_tuningCheck)
                return;
            s_tuningCheck = now + 1000;
            const std::string path = (GetUserPath() / "triggers.txt").string();
            struct stat st;
            if (stat(path.c_str(), &st) != 0)
                return;
            const long long stamp = (long long)st.st_mtime * 1000 + (long long)st.st_size;
            if (stamp == s_tuningStamp)
                return;
            s_tuningStamp = stamp;
            FILE* f = fopen(path.c_str(), "r");
            if (!f)
                return;
            Tuning t;
            char key[64], value[64];
            auto clamp = [](long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; };
            while (fscanf(f, " %63[a-z_] = %63s", key, value) == 2)
            {
                const std::string k = key;
                const long v = strtol(value, nullptr, 10);
                if (k == "abs_amp") t.absAmp = uint8_t(clamp(v, 1, 8));
                else if (k == "abs_freq") t.absFreq = uint8_t(clamp(v, 1, 255));
                else if (k == "kick_amp") t.kickAmp = uint8_t(clamp(v, 1, 8));
                else if (k == "kick_freq") t.kickFreq = uint8_t(clamp(v, 1, 255));
                else if (k == "kick_thump_ms") t.kickThumpMs = uint32_t(clamp(v, 0, 1000));
                else if (k == "kick_hold_ms") t.kickHoldMs = uint32_t(clamp(v, 0, 1000));
            }
            fclose(f);
            s_tuning = t;
            fprintf(stderr, "[ps5] trigger tuning: ABS amp %u, %u Hz; kick amp %u, %u Hz, %u ms, hold %u ms\n", t.absAmp, t.absFreq,
                t.kickAmp, t.kickFreq, unsigned(t.kickThumpMs), unsigned(t.kickHoldMs));
        }

        std::mutex s_mutex;
        int s_handle = -1;
        bool s_ownHandle = false;
        time_t s_nextOpen = 0;
        SDL_JoystickID s_id = 0;
        // What Settings asks for, and what the car and the pad say now.
        int s_triggerLevel = 0;
        bool s_triggerEffects = false;
        telemetry::Car s_car;
        uint8_t s_l2 = 0, s_r2 = 0;
        int s_lastGear = -1;
        Uint64 s_kickUntil = 0;  // SDL ticks
        // The game's last rumble, and whether the rev limiter's buzz is on top.
        uint16_t s_rumbleLow = 0, s_rumbleHigh = 0;
        bool s_limiter = false;
        // What the open pad was sent (sentValid false: nothing yet).
        TriggerEffectParam s_sent{};
        bool s_sentValid = false;
        Uint64 s_sentAt = 0;

        uint8_t Strength(float v)
        {
            const int s = int(v + 0.5f);
            return uint8_t(s < 1 ? 1 : s > 8 ? 8 : s);
        }

        TriggerEffectParam DesiredLocked(Uint64 now)
        {
            TriggerEffectParam param{};
            param.triggerMask = kTriggerL2 | kTriggerR2;
            const Level& l = kLevels[s_triggerLevel];
            TriggerEffectCommand& brake = param.command[0];
            TriggerEffectCommand& accel = param.command[1];
            const telemetry::Car& car = s_car;
            const bool effects = s_triggerEffects;
            if (l.brakeEnd)
            {
                float k = 1;
                if (effects && car.speedValid)
                    k = 0.6f + 0.4f * (car.speed < kFullBrakeSpeed ? car.speed / kFullBrakeSpeed : 1.0f);
                brake.mode = kTriggerEffectSlopeFeedback;
                brake.slope = { 1, 9, Strength(l.brakeStart * k), Strength(l.brakeEnd * k) };
            }
            if (l.accelerator)
            {
                accel.mode = kTriggerEffectFeedback;
                accel.feedback = { 1, l.accelerator };
            }
            s_limiter = false;
            if (!effects)
            {
                s_lastGear = -1;
                return param;
            }
            if (car.speedValid && car.decel > kAbsDecel && s_l2 > kHalf && car.speed > 3)
            {
                brake.mode = kTriggerEffectVibration;
                brake.vibration = { 0, s_tuning.absAmp, s_tuning.absFreq };
            }
            if (car.engineValid)
            {
                if (s_lastGear >= 0 && car.gear != s_lastGear)
                    s_kickUntil = now + s_tuning.kickThumpMs + s_tuning.kickHoldMs;
                s_lastGear = car.gear;
            }
            else
                s_lastGear = -1;
            if (now + s_tuning.kickHoldMs < s_kickUntil)
            {
                accel.mode = kTriggerEffectVibration;
                accel.vibration = { 0, s_tuning.kickAmp, s_tuning.kickFreq };
            }
            else if (now < s_kickUntil)
            {
                accel.mode = kTriggerEffectFeedback;
                accel.feedback = { 0, 8 };
            }
            s_limiter = car.engineValid && car.redline > 0 && car.rpm > car.redline * kLimiter && s_r2 > kHalf;
            return param;
        }

        void SendRumbleLocked()
        {
            uint8_t motors[2] = { uint8_t(s_rumbleLow >> 8), uint8_t(s_rumbleHigh >> 8) };  // large, small
            if (s_limiter)
            {
                motors[0] = motors[0] > kLimiterLow ? motors[0] : kLimiterLow;
                motors[1] = motors[1] > kLimiterHigh ? motors[1] : kLimiterHigh;
            }
            if (s_handle >= 0)
                scePadSetVibration(s_handle, motors);
        }

        // Sends what the triggers should do now, if the open pad doesn't have it.
        void SendTriggersLocked(bool force = false)
        {
            if (s_handle < 0)
                return;
            const Uint64 now = SDL_GetTicks();
            LoadTuning(now);
            const bool limiter = s_limiter;
            const TriggerEffectParam want = DesiredLocked(now);
            if (s_limiter != limiter)
                SendRumbleLocked();
            if (s_sentValid && memcmp(&want, &s_sent, sizeof want) == 0)
                return;
            const bool modeChanged = !s_sentValid || want.command[0].mode != s_sent.command[0].mode
                || want.command[1].mode != s_sent.command[1].mode;
            if (!force && !modeChanged && now - s_sentAt < kSendInterval)
                return;
            const int result = scePadSetTriggerEffect(s_handle, &want);
            // Only the changes of mode, and failures: not a line a frame.
            if (modeChanged || result < 0)
            {
                const telemetry::Car& car = s_car;
                fprintf(stderr, "[ps5] triggers: L2 mode %u, R2 mode %u (resistance %d, effects %s; %.1f m/s, decel %.1f, rpm %.0f/%.0f, gear %d%s): "
                    "scePadSetTriggerEffect %s (0x%08x)\n", unsigned(want.command[0].mode), unsigned(want.command[1].mode), s_triggerLevel,
                    s_triggerEffects ? "on" : "off", car.speed, car.decel, car.rpm, car.redline, car.gear, car.engineValid ? "" : ", no engine",
                    result >= 0 ? "ok" : "failed", unsigned(result));
            }
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
                    s_l2 = ok ? data.l2 : 0;
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
            std::lock_guard lock(s_mutex);
            s_rumbleLow = low;
            s_rumbleHigh = high;
            SendRumbleLocked();  // (with the rev limiter's buzz, while it's on)
            return s_handle >= 0;
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

    void SetTriggers(int level, bool effects, const telemetry::Car& car)
    {
        level = level < 0 ? 0 : level >= int(std::size(kLevels)) ? int(std::size(kLevels)) - 1 : level;
        std::lock_guard lock(s_mutex);
        s_triggerLevel = level;
        s_triggerEffects = effects;
        s_car = car;
        SendTriggersLocked();
    }
}
