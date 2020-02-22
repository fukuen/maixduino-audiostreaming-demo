/*
    sha1.hpp - header of
    ============
    SHA-1 in C++
    ============
    100% Public Domain.
    Original C Code
        -- Steve Reid <steve@edmweb.com>
    Small changes to fit into bglibs
        -- Bruce Guenter <bruce@untroubled.org>
    Translation to simpler C++ Code
        -- Volker Diels-Grabsch <v@njh.eu>
    Safety fixes
        -- Eugene Hopkinson <slowriot at voxelstorm dot com>
*/

#ifndef SHA1_HPP
#define SHA1_HPP


//#include <cstdint>
//#include <iostream>
//#include <string>
#include <String.h>
#include <StringStream.h>


class SHA1
{
public:
    SHA1();
    void update(const String &s);
    void update(StringStream &is);
    String final();
//    static std::string from_file(const std::string &filename);

private:
    uint32_t digest[5];
    String buffer;
    uint64_t transforms;
};


#endif /* SHA1_HPP */