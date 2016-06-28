#ifndef __KABU_WIDESTRINGCONVERT_HPP__
#define __KABU_WIDESTRINGCONVERT_HPP__

#include <string>

namespace kabufuda
{
std::string WideToUTF8(const std::wstring& src);
std::wstring UTF8ToWide(const std::string& src);
}

#endif // __KABU_WIDESTRINGCONVERT_HPP__
