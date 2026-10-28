#pragma once

#include <string_view>

namespace rrs {

// Application identity shown in the console banner.
std::string_view app_name();
std::string_view app_version();

}  // namespace rrs
