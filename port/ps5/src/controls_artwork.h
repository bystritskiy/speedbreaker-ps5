// SpeedBreaker PS5: native callout composition and scoped disc-label correction.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ps5::controls
{
    inline uint32_t Tiled(uint32_t x, uint32_t y)
    {
        const uint32_t outer = (((y >> 5) * 8) + (x >> 5)) << 6;
        const uint32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
        const uint32_t oi = (outer | inner) << 2;
        return ((y & 1) << 4) | ((((x >> 3) & 3) ^ (((y >> 3) & 1) << 1)) << 6) |
            (((y >> 4) & 1) << 11) | (oi & 15) | (((oi >> 4) & 1) << 5) |
            (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
    }
    inline int ControllerIndex(const uint8_t* tiled)
    {
        constexpr uint64_t quick[] = { 0x95503d802c3398c7ull, 0x63b664fc4d46d4d2ull };
        constexpr uint64_t full[] = { 0xd48b207314995fdcull, 0xa6d52a7f0b3228e0ull };
        uint64_t hash = 0xcbf29ce484222325ull;
        for (uint32_t y : { 0u, 63u, 127u, 191u, 255u })
            for (uint32_t x : { 0u, 63u, 127u, 191u, 255u })
                for (uint32_t k = 0; k < 4; ++k)
                    hash = (hash ^ tiled[Tiled(x,y) + k]) * 0x100000001b3ull;
        int candidate = hash == quick[0] ? 0 : hash == quick[1] ? 1 : -1;
        if (candidate < 0) return -1;
        hash = 0xcbf29ce484222325ull;
        for (uint32_t y = 0; y < 256; ++y)
            for (uint32_t x = 0; x < 256; ++x)
                for (uint32_t k = 0; k < 4; ++k)
                    hash = (hash ^ tiled[Tiled(x,y) + k]) * 0x100000001b3ull;
        return hash == full[candidate] ? candidate : -1;
    }

    struct Point { float x, y; };
    // White leaders are native UI primitives, composed over the generated
    // cutout. Exact outer positions belong to the game's existing FEng text.
    inline void Compose(uint8_t* bgra, bool dpadSteering)
    {
        auto line = [&](Point a, Point b) {
            const float ax=a.x*256, ay=a.y*256, bx=b.x*256, by=b.y*256;
            const float dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
            const int x0=std::max(0,int(std::min(ax,bx)-2)),x1=std::min(255,int(std::max(ax,bx)+2));
            const int y0=std::max(0,int(std::min(ay,by)-2)),y1=std::min(255,int(std::max(ay,by)+2));
            for (int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x)
            {
                float t=length>0 ? std::clamp(((x+.5f-ax)*dx+(y+.5f-ay)*dy)/length,0.f,1.f) : 0;
                float distance=std::hypot(x+.5f-ax-t*dx,y+.5f-ay-t*dy);
                float coverage=std::clamp(1.25f-distance,0.f,1.f);
                if (coverage<=0) continue;
                uint8_t* p=bgra+(y*256+x)*4;
                float alpha=p[3]/255.f, result=coverage+alpha*(1-coverage);
                for(int c=0;c<3;++c) p[c]=uint8_t(std::clamp((255*coverage+p[c]*alpha*(1-coverage))/result,0.f,255.f)+.5f);
                p[3]=uint8_t(result*255+.5f);
            }
        };
        auto path = [&](std::initializer_list<Point> points) {
            auto a=points.begin();for(auto b=a+1;b!=points.end();++a,++b)line(*a,*b);
        };
        path({{0,11.f/256},{.17f,11.f/256},{.25f,.13f},{.25f,.23f}}); // L2
        path({{1,12.f/256},{.84f,12.f/256},{.75f,.13f},{.75f,.23f}}); // R2
        path({{0,80.f/256},{.44f,80.f/256},{.50f,.37f},{.50f,.535f}}); // PS
        if(dpadSteering)
            path({{0,250.f/256},{.19f,250.f/256},{.21f,.85f},{.21f,.42f}});
        else
            path({{0,250.f/256},{.19f,250.f/256},{.355f,.80f},{.355f,.547f}});
        path({{1,80.f/256},{.82f,80.f/256},{.792f,.355f}}); // Triangle
        path({{1,140.f/256},{.92f,140.f/256},{.858f,.42f}}); // Circle
        path({{1,200.f/256},{.86f,200.f/256},{.792f,.66f},{.792f,.49f}}); // Cross
        path({{1,250.f/256},{.78f,250.f/256},{.725f,.90f},{.725f,.42f}}); // Square
    }

}
