#include <string>

namespace kabufuda
{
std::string WideToUTF8(const std::wstring& src);
std::wstring UTF8ToWide(const std::string& src);
}
