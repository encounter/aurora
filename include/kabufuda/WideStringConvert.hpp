#ifndef __KABU_WIDESTRINGCONVERT_HPP__
#define __KABU_WIDESTRINGCONVERT_HPP__

#include <string>

namespace kabufuda
{
std::string WideToUTF8(std::wstring_view src);
std::wstring UTF8ToWide(std::string_view src);
}

#endif // __KABU_WIDESTRINGCONVERT_HPP__
