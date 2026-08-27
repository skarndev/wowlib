#include <wowlib/core/path.hpp>

namespace wowlib {
  std::string normalizePath(std::string_view path) {
    std::string result;
    result.reserve(path.size());

    for (char c : path) {
      if (c == '/') c = '\\';
      else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

      // collapse duplicate separators, drop a leading one
      if (c == '\\' && (result.empty() || result.back() == '\\')) continue;

      result.push_back(c);
    }

    if (!result.empty() && result.back() == '\\') result.pop_back();

    return result;
  }

  std::string toNativeRelative(std::string_view canonical) {
    std::string result{canonical};
    for (char& c : result)
      if (c == '\\') c = '/';
    return result;
  }
}
