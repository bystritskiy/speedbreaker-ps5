// The game read from a disc image in place (install/game_image.h,
// install::UseImage): the image's index, case-insensitive lookups, listings,
// reads at a file's run of the image, and the record FindGameInstall finds,
// against a real supported disc image. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/game_image_test.cpp runtime/install/{game_image,placement,installer,locate,manifest,result,disc_source,sha256}.cpp runtime/user/portable.cpp runtime/platform/posix/file_io.cpp -o build/game_image_test
//   (on macOS add -framework CoreFoundation)
//   NFSMW_TEST_IMAGE=<the USA .iso> build/game_image_test <scratch folder>
// Without NFSMW_TEST_IMAGE it passes without checking anything (no game data
// comes with the source). UseImage hashes the whole image: a minute or so.
#include <install/game_image.h>
#include <install/installer.h>
#include <install/locate.h>
#include <user/paths.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <unistd.h>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace install;

int main(int argc, char** argv)
{
    const char* imageVar = std::getenv("NFSMW_TEST_IMAGE");
    if (!imageVar || !*imageVar || argc < 2)
    {
        printf("game_image_test: skipped (set NFSMW_TEST_IMAGE to a supported disc image, give a scratch folder)\n");
        return 0;
    }
    const fs::path image = fs::absolute(imageVar).lexically_normal();
    const Manifest& manifest = GameManifest();

    Result mounted = image::Mount(image);
    CHECK(mounted.Ok(), "mount: %s", mounted.message.c_str());
    CHECK(image::Contains(image) && image::Contains(image / "NFS" / "x"), "the image and below it");
    CHECK(!image::Contains(image.parent_path()) && !image::Contains(fs::path(image.string() + "x")), "not beside it");
    std::optional<image::Entry> root = image::Find(image);
    CHECK(root && root->directory, "the root is a directory");
    CHECK(image::Child(image, "nfs") == image / "NFS", "a child in any case, as the disc spells it");
    CHECK(!image::Child(image, "absent") && !image::Find(image / "absent"), "an absent name");
    std::optional<image::Entry> nested = image::Find(image / "nfs" / "./zzdata0.bin");
    CHECK(nested && !nested->directory && nested->size == 381569024, "a file below, any case");
    std::vector<fs::path> top = image::List(image);
    CHECK(std::find(top.begin(), top.end(), image / "default.xex") != top.end()
        && std::find(top.begin(), top.end(), image / "NFS") != top.end(), "the root's listing: %zu entries", top.size());

    // Every manifest file, with its size; default.xex whole, by its hash; a
    // run in the middle of a big file as DiscSource reads it.
    for (const ManifestFile& f : manifest.files)
    {
        std::optional<image::Entry> e = image::Find(image / std::string(f.path));
        CHECK(e && !e->directory && e->size == f.size, "%.*s", int(f.path.size()), f.path.data());
    }
    std::vector<uint8_t> xex;
    CHECK(image::ReadAll(image / "DEFAULT.XEX", xex) && ToHex(Sha256Of(xex.data(), xex.size())) == manifest.Xex().sha256,
        "default.xex");
    std::unique_ptr<DiscSource> source;
    CHECK(OpenDiscSource(image, source).Ok() && source, "the image as a source");
    if (source && nested)
    {
        std::vector<uint8_t> expected(1 << 20), got(expected.size());
        std::string error;
        CHECK(source->Open(*source->Find("NFS/ZZDATA0.BIN"), error)->Read(123456789, expected.data(), expected.size(), error), "%s", error.c_str());
        int fd = image::OpenImage();
        CHECK(fd >= 0 && pread(fd, got.data(), got.size(), off_t(nested->offset + 123456789)) == ssize_t(got.size()), "pread");
        close(fd);
        CHECK(got == expected, "a run of ZZDATA0.BIN");
    }

    // UseImage records it; FindGameInstall finds it; a changed image isn't.
    const fs::path scratch = fs::absolute(argv[1]) / "game-image-test";
    fs::remove_all(scratch);
    fs::create_directories(scratch / "home");
    setenv("HOME", (scratch / "home").c_str(), 1);
    unsetenv("XDG_DATA_HOME");
    unsetenv("NFSMW_GAME_DIR");
    CHECK(!FindGameInstall(), "nothing installed yet");
    if (source)
    {
        Result used = UseImage(*source);
        CHECK(used.Ok(), "UseImage: %s", used.message.c_str());
    }
    std::optional<GameInstall> found = FindGameInstall();
    CHECK(found && found->image && found->path == image && found->manifest == &manifest, "found the image");
    std::ifstream in(GetUserPath() / "game-image");
    std::string path, version, size;
    std::getline(in, path), std::getline(in, version), std::getline(in, size);
    in.close();
    std::ofstream(GetUserPath() / "game-image") << path << "\n" << version << "\n" << size << "1\n";
    CHECK(!FindGameInstall(), "another size: not this image");
    std::ofstream(GetUserPath() / "game-image") << path << "\n0123456789abcdef\n" << size << "\n";
    CHECK(!FindGameInstall(), "another manifest version: checked again");
    CHECK(RecordInstallPath(DefaultInstallPath()) && !fs::exists(GetUserPath() / "game-image"), "a folder install takes over");
    fs::remove_all(scratch);

    printf(g_failures ? "game_image_test: %d failures\n" : "game_image_test: all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
