// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See game_image.h.
#include "game_image.h"
#include "disc_source.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <fcntl.h>
#ifdef _WIN32
#include <platform/os.h>
#else
#include <unistd.h>
#endif

#include <platform/file_io.h>

namespace install::image
{
    namespace
    {
        namespace fs = std::filesystem;

        struct Node
        {
            Entry entry;
            std::string name;                   // the disc's spelling
            std::vector<std::string> children;  // keys of the entries in a directory
        };

        struct Index
        {
            fs::path root;
            std::string rootText;
            int64_t seconds = 0, nanoseconds = 0;
            std::unordered_map<std::string, Node> nodes;  // by lower-case relative path, '/'-separated; "" the root
        };
        std::unique_ptr<const Index> s_index;

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = char(std::tolower(uint8_t(c)));
            return out;
        }

        // `host`'s key in the index, if it is the image or below it.
        std::optional<std::string> Key(const fs::path& host)
        {
            if (!s_index)
                return std::nullopt;
            const std::string text = host.generic_string();
            const std::string& root = s_index->rootText;
            if (text.compare(0, root.size(), root) != 0 || (text.size() > root.size() && text[root.size()] != '/'))
                return std::nullopt;
            std::string key;
            for (size_t at = root.size(); at < text.size();)
            {
                size_t start = at + 1, end = text.find('/', start);
                if (end == std::string::npos)
                    end = text.size();
                std::string_view part(text.data() + start, end - start);
                if (!part.empty() && part != ".")
                {
                    if (!key.empty())
                        key += '/';
                    key += Lower(part);
                }
                at = end;
            }
            return key;
        }

        const Node* NodeAt(const fs::path& host)
        {
            std::optional<std::string> key = Key(host);
            if (!key)
                return nullptr;
            auto it = s_index->nodes.find(*key);
            return it == s_index->nodes.end() ? nullptr : &it->second;
        }
    }

    Result Mount(const fs::path& image)
    {
        std::unique_ptr<DiscSource> source;
        Result r = OpenDiscSource(image, source);
        if (!r.Ok())
            return r;
        if (source->Kind() != SourceKind::Image)
        {
            r.error = Error::NotADiscImage;
            r.message = image.string() + " is not a disc image.";
            return r;
        }
        auto index = std::make_unique<Index>();
        std::error_code ec;
        index->root = fs::absolute(image, ec).lexically_normal();
        index->rootText = index->root.generic_string();
        platform::FileInfo info;
        if (platform::InfoOf(index->root, info) == 0)
        {
            index->seconds = info.modifiedSeconds;
            index->nanoseconds = info.modifiedNanoseconds;
        }
        index->nodes[""].entry.directory = true;
        for (const DiscFile& file : source->Files())
        {
            // Each directory on the way, then the file.
            std::string parent;
            std::string_view rest = file.path;
            while (true)
            {
                size_t slash = rest.find('/');
                std::string_view name = rest.substr(0, slash);
                std::string key = parent.empty() ? Lower(name) : parent + '/' + Lower(name);
                auto [it, added] = index->nodes.try_emplace(key);
                if (added)
                {
                    it->second.name = std::string(name);
                    index->nodes[parent].children.push_back(key);
                }
                if (slash == std::string_view::npos)
                {
                    it->second.entry = { false, file.offset, file.size };
                    break;
                }
                it->second.entry.directory = true;
                parent = std::move(key);
                rest.remove_prefix(slash + 1);
            }
        }
        fprintf(stderr, "[image] %s: %zu files, read in place\n", index->rootText.c_str(), source->Files().size());
        s_index = std::move(index);
        return {};
    }

    bool Contains(const fs::path& host)
    {
        return Key(host).has_value();
    }

    std::optional<Entry> Find(const fs::path& host)
    {
        const Node* node = NodeAt(host);
        return node ? std::optional<Entry>(node->entry) : std::nullopt;
    }

    std::optional<fs::path> Child(const fs::path& dir, std::string_view name)
    {
        const Node* node = NodeAt(dir);
        if (!node || !node->entry.directory)
            return std::nullopt;
        std::string lower = Lower(name);
        for (const std::string& key : node->children)
        {
            size_t slash = key.rfind('/');
            if (key.compare(slash == std::string::npos ? 0 : slash + 1, std::string::npos, lower) == 0)
                return dir / s_index->nodes.at(key).name;
        }
        return std::nullopt;
    }

    std::vector<fs::path> List(const fs::path& dir)
    {
        std::vector<fs::path> out;
        const Node* node = NodeAt(dir);
        if (node && node->entry.directory)
            for (const std::string& key : node->children)
                out.push_back(dir / s_index->nodes.at(key).name);
        return out;
    }

    int OpenImage()
    {
        return s_index ? platform::OpenFile(s_index->root, O_RDONLY | O_CLOEXEC) : -1;
    }

    void ModifiedTime(int64_t& seconds, int64_t& nanoseconds)
    {
        seconds = s_index ? s_index->seconds : 0;
        nanoseconds = s_index ? s_index->nanoseconds : 0;
    }

    bool ReadAll(const fs::path& host, std::vector<uint8_t>& data)
    {
        std::optional<Entry> entry = Find(host);
        if (!entry || entry->directory)
            return false;
        int fd = OpenImage();
        if (fd < 0)
            return false;
        data.resize(size_t(entry->size));
        size_t done = 0;
        while (done < data.size())
        {
            int64_t n = platform::ReadAt(fd, data.data() + done, data.size() - done, entry->offset + done);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            done += size_t(n);
        }
        close(fd);
        return done == data.size();
    }
}
