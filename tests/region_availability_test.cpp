// Only releases whose recompiled code is linked may be installed.
#include <install/manifest.h>
#include <cassert>

#ifndef SB_PAL_ES
#define SB_PAL_ES 0
#endif
#ifndef SB_PAL_DE
#define SB_PAL_DE 0
#endif
#ifndef SB_PAL_EN
#define SB_PAL_EN 0
#endif

int main()
{
    using namespace install;
    struct Case { const char* hash; const char* id; const char* folder; bool enabled; };
    const Case cases[] = {
        { "aad15fc218d034de3131f746e0b763dd8049a40dd676efc87667fcaf4fc72694", "pal-es", "ppc-pal-es", SB_PAL_ES },
        { "23ece29d18724ea28971d52f936964d127148bd2359ff24795da7442b9069bf4", "pal-de", "ppc-pal-de", SB_PAL_DE },
        { "e2ea2ff11bf84cb30430d824d7bfe699a3acf3e7400d21b9a7c938e3d7a10474", "pal-en", "ppc-pal-en", SB_PAL_EN },
    };
    assert(GameManifests().size() == 1 + SB_PAL_ES + SB_PAL_DE + SB_PAL_EN);
    for (const auto& c : cases)
    {
        const Manifest* m = ManifestForXexHash(c.hash);
        const auto missing = ReleaseWithoutCode(c.hash);
        assert(bool(m) == c.enabled);
        if (m)
        {
            assert(m->region == c.id && missing.codeFolder.empty());
            assert(ManifestForVersion(m->version) == m);
            const auto* bySize = ManifestForXexSize(m->Xex().size);
            if (m->Xex().size == 3940352 && SB_PAL_ES && SB_PAL_EN) assert(!bySize);
            else assert(bySize == m);
        }
        else assert(missing.codeFolder == c.folder);
    }
    assert(ReleaseWithoutCode(GameManifest().Xex().sha256).codeFolder.empty());
}
