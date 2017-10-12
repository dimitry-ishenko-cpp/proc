////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013-2017 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#ifndef PGM_CHARPP_HPP
#define PGM_CHARPP_HPP

////////////////////////////////////////////////////////////////////////////////
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>

////////////////////////////////////////////////////////////////////////////////
namespace pgm
{

////////////////////////////////////////////////////////////////////////////////
struct charpp_delete
{
    void operator()(char* pp[])
    {
        for(auto pi = pp; *pi; ++pi) std::free(*pi);
        std::free(pp);
    }
};

using charpp = std::unique_ptr<char*[], charpp_delete>;

////////////////////////////////////////////////////////////////////////////////
template<typename Iter>
inline charpp
make_charpp(Iter first, Iter last)
{
    auto np = static_cast<char**>(
        std::calloc(std::distance(first, last) + 1, sizeof(char*))
    );
    if(!np) throw std::bad_alloc();

    charpp cpp(np);
    for(Iter it = first; it != last; ++it) *np++ = ::strdup(it->data());

    return cpp;
}

////////////////////////////////////////////////////////////////////////////////
template<typename Iter>
inline charpp
make_charpp(const std::string& first, Iter second, Iter last)
{
    auto np = static_cast<char**>(
        std::calloc(1 + std::distance(second, last) + 1, sizeof(char*))
    );
    if(!np) throw std::bad_alloc();

    charpp cpp(np);
    *np++ = ::strdup(first.data());
    for(Iter it = second; it != last; ++it) *np++ = ::strdup(it->data());

    return cpp;
}

////////////////////////////////////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////
#endif
