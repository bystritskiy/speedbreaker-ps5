// PS5 launchers may pass the title ID as the first application argument.
#pragma once
#include <cstring>

namespace ps5::startup
{
    inline void RemoveTitleArgument(int& argc, char** argv)
    {
        if (argc > 1 && argv && argv[1] && std::strcmp(argv[1], "PPSA90505") == 0)
        {
            for (int i = 1; i < argc; ++i)
                argv[i] = argv[i + 1];
            --argc;
        }
    }
}
