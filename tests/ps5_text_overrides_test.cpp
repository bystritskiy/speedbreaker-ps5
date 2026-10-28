// Read-time text edits: a synthetic archive, then (optional) the player's own
// USA archives, which hold the only copy of the original text.
//   c++ -O2 -std=c++20 -Iport/ps5/src tests/ps5_text_overrides_test.cpp -o /tmp/ps5-text-test
//   /tmp/ps5-text-test [game/files/NFS]
#include "text_overrides.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    // Every split of a slot into two reads gives the result of one whole read.
    template<class Table,class ReadSlot>
    std::vector<uint8_t> Whole(Table& table,const std::string& path,uint64_t at,std::vector<uint8_t> bytes,ReadSlot read)
    {
        table.Apply(path,at,bytes.data(),bytes.size(),read);
        return bytes;
    }

    void Synthetic()
    {
        const std::string text="Press the START button now.";
        std::vector<uint8_t> archive(64,'#');
        std::copy(text.begin(),text.end(),archive.begin()+16);
        archive[16+text.size()]=0;
        const uint32_t size=uint32_t(text.size()+1);
        const ps5::text::Edit edits[]={ { 6, 16, "Options", 7 } };
        const ps5::text::Patch patches[]={ { 3, 16, size, ps5::text::Fnv1a(archive.data()+16,size), 0, 1 } };
        auto read=[&](uint64_t offset,uint8_t* out,size_t n) { memcpy(out,archive.data()+offset,n); return true; };
        const std::string path="D:\\NFS\\ZZDATA3.BIN";
        ps5::text::Overrides table(patches,edits);
        auto whole=Whole(table,path,0,archive,read);
        assert(std::string(reinterpret_cast<const char*>(whole.data()+16))=="Press Options now.");
        // Only the slot changed, padding included.
        for(size_t i=0;i<archive.size();++i)
            if(i<16 || i>=16+size)assert(whole[i]==archive[i]);
        for(size_t split=0;split<=archive.size();++split)
        {
            ps5::text::Overrides fresh(patches,edits);
            auto bytes=archive;
            fresh.Apply(path,0,bytes.data(),split,read);
            fresh.Apply(path,split,bytes.data()+split,bytes.size()-split,read);
            assert(bytes==whole);
        }
        // A disc whose slot is not the expected text, another file, an empty read.
        auto mutated=archive;mutated[20]^=0x20;
        auto readMutated=[&](uint64_t offset,uint8_t* out,size_t n) { memcpy(out,mutated.data()+offset,n); return true; };
        ps5::text::Overrides other(patches,edits);
        assert(Whole(other,path,0,mutated,readMutated)==mutated);
        ps5::text::Overrides third(patches,edits);
        assert(Whole(third,"D:\\NFS\\OTHER.BIN",0,archive,read)==archive);
        assert(third.Apply(path,16,archive.data(),0,read)==0);
        // A read that differs from the disc is not touched, even once resolved.
        ps5::text::Overrides fourth(patches,edits);
        auto bytes=archive;bytes[17]='x';
        fourth.Apply(path,0,bytes.data(),bytes.size(),read);
        assert(bytes[16]=='P' && bytes[17]=='x');
        // Edits that would overrun the slot are refused.
        const ps5::text::Edit longer[]={ { 0, 0, "0123456789012345678901234567890123", 34 } };
        ps5::text::Overrides fifth(patches,longer);
        assert(Whole(fifth,path,0,archive,read)==archive);
        assert(ps5::text::Archive("D:\\nfs\\zZdAtA4.bIn")==4);
        assert(ps5::text::Archive("ZZDATA40.BIN")==-1);
    }
}

int main(int argc,char** argv)
{
    Synthetic();
    if(argc!=2)
    {
        printf("synthetic archive checked; pass the extracted NFS folder for the disc's slots\n");
        return 0;
    }
    std::ifstream files[10];
    for(unsigned archive=0;archive<10;++archive)
        files[archive].open(std::string(argv[1])+"/ZZDATA"+std::to_string(archive)+".BIN",std::ios::binary);
    auto readFrom=[&](unsigned archive) {
        return [&files,archive](uint64_t offset,uint8_t* out,size_t n) {
            files[archive].clear();files[archive].seekg(std::streamoff(offset));
            files[archive].read(reinterpret_cast<char*>(out),std::streamsize(n));
            return size_t(files[archive].gcount())==n;
        };
    };
    size_t checks=0;
    for(const auto& p:ps5::text::kPatches)
    {
        const auto path="D:\\NFS\\ZZDATA"+std::to_string(p.archive)+".BIN";
        auto read=readFrom(p.archive);
        constexpr size_t padding=31;
        std::vector<uint8_t> window(p.size+padding*2);
        assert(read(p.offset-padding,window.data(),window.size()));
        std::vector<uint8_t> text(p.size);
        assert(ps5::text::Rewrite(p,ps5::text::kEdits,window.data()+padding,text.data()));
        assert(text!=std::vector<uint8_t>(window.begin()+padding,window.end()-padding));
        ps5::text::Overrides table(ps5::text::kPatches,ps5::text::kEdits);
        auto whole=window;
        assert(table.Apply(path,p.offset-padding,whole.data(),whole.size(),read)>=1);
        assert(memcmp(whole.data()+padding,text.data(),p.size)==0);
        // Every changed byte in the window belongs to a known patch.
        for(size_t i=0;i<window.size();++i)
            if(whole[i]!=window[i])
            {
                const uint64_t at=p.offset-padding+i;bool known=false;
                for(const auto& q:ps5::text::kPatches)
                    known|=q.archive==p.archive && at>=q.offset && at<q.offset+q.size;
                assert(known);
            }
        for(size_t split=0;split<=window.size();++split,++checks)
        {
            auto bytes=window;
            table.Apply(path,p.offset-padding,bytes.data(),split,read);
            table.Apply(path,p.offset-padding+split,bytes.data()+split,bytes.size()-split,read);
            assert(bytes==whole);
        }
    }
    printf("%zu disc slots, %zu read splits; hashes, edits, boundaries and unrelated bytes checked\n",
        std::size(ps5::text::kPatches),checks);
}
