// Optional local USA asset check; the game's artwork is never a test fixture.
// c++ -std=c++20 -Iport/ps5/src tests/ps5_glyphs_test.cpp -o /tmp/ps5-glyphs-test
// /tmp/ps5-glyphs-test <extracted GLOBALB.BUN>
#include <ps5_glyphs.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <cstdio>

static uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    assert(data.size() == 18285616);
    const std::map<std::string, std::string> expected = {
        {"L1","L2"}, {"L2","L1"}, {"R1","R2"}, {"R2","R1"},
        {"R3","R3"}, {"R3_FRENCH","R3"}, {"R3_SPANISH","R3"}, {"R3_ITALIAN","R3"},
        {"BACK","Touchpad"}, {"START","Options"}, {"GUIDE","PS"}, {"SELECT","R1"},
        {"CROSS","Cross"}, {"CIRCLE","Triangle"}, {"TRIANGLE","Circle"}, {"SQUARE","Square"},
        {"LEFT_TRIGGER_FRENCH","L2"}, {"RIGHT_TRIGGER_FRENCH","R2"}
    };
    size_t matches = 0, rejected = 0;
    for (size_t i = 0; i < 89; ++i)
    {
        const auto* record = data.data() + 0x4173a4 + i * 124;
        std::string name(reinterpret_cast<const char*>(record + 12));
        const auto* pixels = data.data() + 0x41b000 + le32(record + 48);
        // Only the same fetch layout eligible in the renderer.
        if (le32(record + 68) != 0x00200020 || (le32(data.data() + 0x419ec8 + i * 32 + 20) & 63) != 20)
            continue;
        const auto* glyph = ps5::glyphs::Find(pixels);
        auto wanted = expected.find(name);
        if (wanted == expected.end()) { assert(!glyph); ++rejected; continue; }
        assert(glyph && wanted->second == glyph->name);
        std::vector<uint8_t> modified(pixels, pixels + 16384);
        modified[0] ^= 1;
        assert(!ps5::glyphs::Find(modified.data()));
        ++matches;
    }
    assert(matches == expected.size());
    std::printf("%zu button mappings matched, %zu other textures rejected; modified artwork rejected\n", matches, rejected);
}
