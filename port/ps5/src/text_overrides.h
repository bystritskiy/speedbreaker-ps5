// PS5 presentation text, exact USA disc ranges; no generic byte replacement.
// The table holds no game text: each slot's original bytes are read from the
// player's own disc, checked against their hash, and edited into the new text.
#pragma once
#include "text_patches.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace ps5::text
{
    inline int Archive(std::string_view path)
    {
        const size_t slash=path.find_last_of("\\/");
        auto file=path.substr(slash==path.npos ? 0 : slash+1);
        if(file.size()!=11 || file[6]<'0' || file[6]>'9')return -1;
        constexpr char pattern[]="ZZDATA0.BIN";
        for(size_t i=0;i<file.size();++i)
            if(i!=6 && (file[i]>='a' && file[i]<='z' ? file[i]-'a'+'A' : file[i])!=pattern[i])return -1;
        return file[6]-'0';
    }

    inline uint64_t Fnv1a(const uint8_t* data,size_t size)
    {
        uint64_t hash=0xcbf29ce484222325ull;
        for(size_t i=0;i<size;++i)hash=(hash^data[i])*0x100000001b3ull;
        return hash;
    }

    // The new text of one slot from its original bytes (size, NUL included),
    // padded with NULs; false when they are not the expected ones.
    inline bool Rewrite(const Patch& p,const Edit* edits,const uint8_t* old,uint8_t* out)
    {
        if(!p.size || old[p.size-1]!=0 || Fnv1a(old,p.size)!=p.hash)return false;
        size_t written=0,at=0;
        auto put=[&](const void* bytes,size_t length) {
            if(written+length>=p.size)return false;  // the text and at least its NUL
            memcpy(out+written,bytes,length);written+=length;return true;
        };
        for(uint32_t i=0;i<p.edits;++i)
        {
            const Edit& e=edits[p.edit+i];
            if(e.at<at || e.at+e.remove>p.size-1)return false;
            if(!put(old+at,e.at-at) || !put(e.insert,e.length))return false;
            at=e.at+e.remove;
        }
        if(!put(old+at,p.size-1-at))return false;
        memset(out+written,0,p.size-written);
        return true;
    }

    // Read-time replacement over one patch table. A slot is resolved on the
    // first read that touches it: ReadSlot(offset, buffer, size) reads it whole
    // from the same archive, so reads of any part of it, or across two, work.
    // Then only bytes of this read equal to the disc's original are replaced;
    // an unsupported or mutated resource is left untouched.
    class Overrides
    {
    public:
        Overrides(std::span<const Patch> patches,const Edit* edits):m_patches(patches),m_edits(edits),m_slots(patches.size()){}

        template<class ReadSlot>
        size_t Apply(std::string_view path,uint64_t position,uint8_t* data,size_t size,ReadSlot&& readSlot)
        {
            const int archive=Archive(path);
            if(archive<0 || !size || size>std::numeric_limits<uint64_t>::max()-position)return 0;
            const uint64_t limit=position+size;
            auto first=std::lower_bound(m_patches.begin(),m_patches.end(),position,
                [archive](const Patch& p,uint64_t at) {
                    return p.archive<uint32_t(archive) || (p.archive==uint32_t(archive) && p.offset+p.size<=at);
                });
            size_t changed=0;
            for(auto it=first;it!=m_patches.end() && it->archive==uint32_t(archive) && it->offset<limit;++it)
            {
                const uint64_t start=std::max(position,it->offset),end=std::min(limit,it->offset+it->size);
                const size_t destination=size_t(start-position),source=size_t(start-it->offset),length=size_t(end-start);
                std::lock_guard lock(m_mutex);
                Slot& slot=m_slots[size_t(it-m_patches.begin())];
                if(slot.state==Slot::Unread)
                {
                    slot.old.resize(it->size);slot.text.resize(it->size);
                    slot.state=readSlot(it->offset,slot.old.data(),size_t(it->size)) &&
                        Rewrite(*it,m_edits,slot.old.data(),slot.text.data()) ? Slot::Valid : Slot::Invalid;
                }
                if(slot.state==Slot::Valid && length && memcmp(data+destination,slot.old.data()+source,length)==0)
                {
                    memcpy(data+destination,slot.text.data()+source,length);
                    ++changed;
                }
            }
            return changed;
        }

    private:
        struct Slot { enum { Unread, Valid, Invalid } state=Unread; std::vector<uint8_t> old,text; };
        std::span<const Patch> m_patches;
        const Edit* m_edits;
        std::vector<Slot> m_slots;
        std::mutex m_mutex;
    };

    template<class ReadSlot>
    size_t Apply(std::string_view path,uint64_t position,uint8_t* data,size_t size,ReadSlot&& readSlot)
    {
        static Overrides disc(kPatches,kEdits);
        return disc.Apply(path,position,data,size,readSlot);
    }
}
