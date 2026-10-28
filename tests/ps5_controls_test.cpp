#include "controls_artwork.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
int main(int argc,char** argv)
{
    std::vector<uint8_t> image(256*256*4);ps5::controls::Compose(image.data(),false);
    for(auto at: {11u,80u,250u}) assert(image[(at*256)*4+3]>100);
    assert(ps5::controls::ControllerIndex(image.data())==-1);
    if(argc==3)
    {
        for(int i=0;i<2;++i)
        {
            std::ifstream input(argv[i+1],std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
            assert(data.size()>=256*256*4);
            assert(ps5::controls::ControllerIndex(data.data())==i);
            data[ps5::controls::Tiled(55,55)]^=1;
            assert(ps5::controls::ControllerIndex(data.data())==-1);
        }
    }
    puts("PS5 controller fingerprints and native callouts passed");
}
