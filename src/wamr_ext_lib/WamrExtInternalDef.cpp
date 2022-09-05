#include "WamrExtInternalDef.h"
#include "../base/FSUtility.h"
#include "uv.h"

WamrExtInstanceConfig::WamrExtInstanceConfig() {
    auto tempDirPath = WAMR_EXT_NS::FSUtility::GetTempDir();
#ifndef _WIN32
    preOpenDirs["/etc"] = "/etc";
    preOpenDirs["/dev"] = "/dev";
#else
#error "Pre-mount directories must be provided for Windows"
#endif
    if (!tempDirPath.empty()) {
        preOpenDirs["/tmp"] = tempDirPath;
        envVars["TMPDIR"] = "/tmp";
    }
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};
};
