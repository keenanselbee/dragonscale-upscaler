#pragma once

#include <string_view>
#include "REL/Version.h"

namespace Plugin
{
    inline constexpr std::string_view NAME = "DragonScale - DLSS and FSR2 Upscaler";
    inline constexpr std::string_view DLL_NAME = "dragonscale-upscaler";
    inline constexpr REL::Version VERSION{ 1, 0, 0, 0 };
}
