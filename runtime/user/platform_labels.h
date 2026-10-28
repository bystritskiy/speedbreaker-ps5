// Display strings only; configuration keys and guest input bits stay stable.
#pragma once
namespace platform_labels
{
    constexpr const char* Text(const char* original,const char* ps5)
    {
#ifdef __PROSPERO__
        return ps5;
#else
        return original;
#endif
    }
}
