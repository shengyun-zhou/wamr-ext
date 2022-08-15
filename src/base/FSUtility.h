#pragma once

#include "BaseDef.h"

namespace WAMR_EXT_NS {
    class FSUtility {
    public:
        static std::filesystem::path GetTempDir();
    };
}
