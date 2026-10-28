// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See car_telemetry.h.
#include <stdafx.h>
#include "car_telemetry.h"
#include <kernel/memory.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#ifndef __PROSPERO__
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
    // The speed in m/s (the image's data: the same address every run).
    constexpr uint32_t kSpeed = 0x82A2C250;
    // The engine struct, by its shape: revs, redline, maximum revs, gear,
    // then the allocator's 0x0000AAAA fill. Found at 0xA8D99904 in the
    // first session; looked for in the physical heap.
    constexpr uint32_t kEngineFrom = 0xA0000000, kEngineTo = 0xC0000000;

    uint32_t U32(uint32_t addr)
    {
        const uint8_t* p = g_memory.base + addr;
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }

    float F32(uint32_t addr)
    {
        uint32_t bits = U32(addr);
        float f;
        memcpy(&f, &bits, 4);
        return f;
    }

    // Reading a page the game never touched would commit it on hosts that
    // map guest memory lazily: those pages are skipped. (The PS5 commits all
    // of it up front: kernel/memory.cpp.)
    bool Resident(uint32_t addr)
    {
#ifdef __PROSPERO__
        (void)addr;
        return true;
#else
        const uint64_t page = uint64_t(getpagesize());
        unsigned char vec = 0;
        return mincore(g_memory.base + (addr & ~(page - 1)), page, (char*)&vec) == 0 && (vec & 1);
#endif
    }

    // The engine's own fields. (The word after the gear reads 0x0000AAAA
    // when the search looks, but not during a gear change: only the search
    // asks for it.)
    bool LooksLikeEngine(uint32_t a)
    {
        const float rpm = F32(a), redline = F32(a + 4), max = F32(a + 8);
        const uint32_t gear = U32(a + 12);
        return gear <= 9 && redline >= 3000 && redline <= 15000 && max >= redline && max <= 20000
            && std::isfinite(rpm) && rpm >= 0 && rpm <= max * 1.2f;
    }

    std::atomic<uint32_t> s_engine{ 0 };
    std::atomic<bool> s_searching{ false };
    std::chrono::steady_clock::time_point s_nextSearch{};

    void Search()
    {
        constexpr uint32_t kPage = 0x1000;
        uint32_t found = 0;
        int matches = 0;
        for (uint64_t page = kEngineFrom; page < kEngineTo; page += kPage)
        {
            if (!Resident(uint32_t(page)))
                continue;
            // (the struct's 20 bytes within one page: a 4-aligned field
            // never straddles, and the shape check reads only this page)
            for (uint64_t a = page; a + 20 <= page + kPage; a += 4)
                if (U32(uint32_t(a + 16)) == 0x0000AAAA && LooksLikeEngine(uint32_t(a)))
                {
                    if (matches < 8)
                        fprintf(stderr, "[telemetry] engine candidate %08X: %.0f rpm, redline %.0f, max %.0f, gear %u\n", uint32_t(a),
                            F32(uint32_t(a)), F32(uint32_t(a + 4)), F32(uint32_t(a + 8)), U32(uint32_t(a + 12)));
                    matches++;
                    if (!found)
                        found = uint32_t(a);
                }
        }
        fprintf(stderr, "[telemetry] engine: %s (%d match%s)\n", found ? "found" : "not found", matches, matches == 1 ? "" : "es");
        // More than one (other cars' engines, in a race): the first, for
        // now; the log's candidates tell which was the player's.
        s_engine = found;
        s_searching = false;
    }

    // Since when the engine found stopped looking like one: searched for
    // again only after kLostFor (not over one odd frame).
    std::chrono::steady_clock::time_point s_lostSince{};
    constexpr auto kLostFor = std::chrono::seconds(1);
    telemetry::Car s_lastEngine;  // the last good reading, given meanwhile

    float s_lastSpeed = 0;
    float s_decel = 0;
    std::chrono::steady_clock::time_point s_lastAt{};
}

namespace telemetry
{
    Car Read()
    {
        Car car;
        if (!g_memory.base)
            return car;
        const auto now = std::chrono::steady_clock::now();
        if (Resident(kSpeed))
        {
            const float speed = F32(kSpeed);
            if (std::isfinite(speed) && speed >= 0 && speed < 200)
            {
                car.speedValid = true;
                car.speed = speed;
                const float dt = std::chrono::duration<float>(now - s_lastAt).count();
                if (dt > 0.001f && dt < 0.25f)
                {
                    // Over about a tenth of a second: the game's speed steps
                    // with its own 60 Hz, not with each of our frames.
                    const float a = std::min(1.0f, dt / 0.1f);
                    s_decel += ((s_lastSpeed - speed) / dt - s_decel) * a;
                }
                s_lastSpeed = speed;
                s_lastAt = now;
                car.decel = s_decel;
            }
        }
        const uint32_t engine = s_engine;
        if (engine && Resident(engine) && LooksLikeEngine(engine))
        {
            car.engineValid = true;
            car.rpm = F32(engine);
            car.redline = F32(engine + 4);
            car.gear = int(U32(engine + 12));
            s_lostSince = {};
            s_lastEngine = car;
        }
        else if (engine && (s_lostSince == std::chrono::steady_clock::time_point{} || now - s_lostSince < kLostFor))
        {
            if (s_lostSince == std::chrono::steady_clock::time_point{})
                s_lostSince = now;
            car.engineValid = true;
            car.rpm = s_lastEngine.rpm;
            car.redline = s_lastEngine.redline;
            car.gear = s_lastEngine.gear;
        }
        else if (car.speedValid && car.speed > 1 && !s_searching && now >= s_nextSearch)
        {
            // Only while the car moves: in the menus there is none to find.
            s_engine = 0;
            s_searching = true;
            s_nextSearch = now + std::chrono::seconds(3);
            std::thread(Search).detach();
        }
        return car;
    }
}
