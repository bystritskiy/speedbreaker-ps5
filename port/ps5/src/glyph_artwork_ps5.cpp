// Lossless programmatic UI artwork; optional, legacy BC3 remains a fallback.
#include "ps5_host.h"
#include "ps5_glyphs.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

const uint8_t* ps5::glyphs::RgbaMip(size_t index,uint32_t level)
{
    constexpr size_t chainBytes=87380; // sum(128^2..1^2)*4
    constexpr size_t count=sizeof(kGlyphs)/sizeof(kGlyphs[0]);
    static const std::vector<uint8_t> artwork=[] {
        std::vector<uint8_t> data(16+count*chainBytes);
        FILE* f=fopen("/app0/glyphs.rgba","rb");
        if(!f) { fprintf(stderr,"[ps5 glyphs] no lossless artwork; using BC3\n");return std::vector<uint8_t>{}; }
        const size_t read=fread(data.data(),1,data.size(),f);
        const bool sizeOk=read==data.size() && fgetc(f)==EOF;
        fclose(f);
        auto word=[&](size_t at) { uint32_t value;memcpy(&value,data.data()+at,4);return value; };
        if(!sizeOk || memcmp(data.data(),"PSG1",4) || word(4)!=128 || word(8)!=count || word(12)!=8)
        { fprintf(stderr,"[ps5 glyphs] invalid lossless artwork; using BC3\n");return std::vector<uint8_t>{}; }
        return data;
    }();
    if(artwork.empty() || index>=count || level>=8)return nullptr;
    size_t offset=16+index*chainBytes;
    for(uint32_t l=0;l<level;++l)offset+=size_t(128>>l)*(128>>l)*4;
    return artwork.data()+offset;
}
