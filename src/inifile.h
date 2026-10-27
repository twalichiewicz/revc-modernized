#pragma once
// One setting written back into an ini's text, the rest of it as it was: the
// Controls page saves what it changes (the mod's ini, vcmodernhud.ini).
// Unit-tested (tests/test_inifile.cpp).
#include <string>

namespace inifile {

// `ini` with `key=value`: its first `key=` line (any case, not a comment)
// replaced, keeping a trailing "; comment"; else added at the end of
// [section]; else [section] added with it.
std::string WithValue(const std::string &ini, const std::string &section, const std::string &key,
                      const std::string &value);

} // namespace inifile
