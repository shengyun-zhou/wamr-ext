#include "WamrExtInternalDef.h"
#include "FSUtility.h"

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

WamrExtInstance::InstRuntimeData::InstRuntimeData(const WamrExtInstanceConfig &config) {
    for (const auto& p : config.preOpenDirs) {
        preOpenHostDirs.push_back(p.second.c_str());
        preOpenMapDirs.push_back(p.first.c_str());
    }
    for (const auto& p : config.envVars) {
        envVarsStringList.emplace_back(std::move(p.first + '=' + p.second));
        envVars.push_back(envVarsStringList.back().c_str());
    }
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};
};
