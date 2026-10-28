// The DualSense cutout (assets/dualsense.bgra) plus native leader lines; loaded once on demand.
#include "ps5_host.h"
#include "controls_artwork.h"
#include <array>
#include <cstdio>
#include <vector>

const uint8_t* ps5::controls::ControllerMip(const uint8_t* tiled, uint32_t level)
{
    const int index=ControllerIndex(tiled);
    if(index<0 || level>8) return nullptr;
    using Chain=std::array<std::vector<uint8_t>,9>;
    static const std::array<Chain,2> images=[] {
        std::array<Chain,2> result;
        std::vector<uint8_t> cutout(256*256*4);
        FILE* file=fopen("/app0/dualsense.bgra","rb");
        if(!file) { fprintf(stderr,"[ps5 glyphs] missing DualSense loading asset\n");return result; }
        const size_t count=fread(cutout.data(),1,cutout.size(),file);
        const bool valid=count==cutout.size() && fgetc(file)==EOF;
        fclose(file);
        if(!valid) { fprintf(stderr,"[ps5 glyphs] invalid DualSense loading asset size\n");return result; }
        for(size_t variant=0;variant<2;++variant)
        {
            auto& chain=result[variant];chain[0]=cutout;Compose(chain[0].data(),variant!=0);
            uint32_t width=256;
            for(size_t mip=1;mip<9;++mip)
            {
                const uint32_t next=width/2;chain[mip].resize(next*next*4);
                // Alpha-weight RGB so transparent borders do not turn dark.
                for(uint32_t y=0;y<next;++y) for(uint32_t x=0;x<next;++x)
                {
                    unsigned sum[4]={};
                    for(uint32_t dy=0;dy<2;++dy) for(uint32_t dx=0;dx<2;++dx)
                    {
                        const uint8_t* p=chain[mip-1].data()+((y*2+dy)*width+x*2+dx)*4;
                        sum[3]+=p[3];for(int c=0;c<3;++c)sum[c]+=unsigned(p[c])*p[3];
                    }
                    uint8_t* p=chain[mip].data()+(y*next+x)*4;
                    for(int c=0;c<3;++c)p[c]=sum[3] ? uint8_t((sum[c]+sum[3]/2)/sum[3]) : 0;
                    p[3]=uint8_t((sum[3]+2)/4);
                }
                width=next;
            }
        }
        fprintf(stderr,"[ps5 glyphs] DualSense loading diagrams ready (both steering layouts)\n");
        return result;
    }();
    return images[index][level].empty() ? nullptr : images[index][level].data();
}
