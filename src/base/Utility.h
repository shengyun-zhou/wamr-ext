#pragma once
#include "BaseDef.h"

namespace WAMR_EXT_NS {
class Utility {
public:
    static wasi_errno_t ConvertErrnoToWasiErrno(int err);
};
}