#include "WamrExtInternalDef.h"
#include "FSUtility.h"

WamrExtInstanceConfig::WamrExtInstanceConfig() {
    auto tempDirPath = WAMR_EXT_NS::FSUtility::GetTempDir();
#ifndef _WIN32
    preMountDirs["/etc"] = "/etc";
    preMountDirs["/dev"] = "/dev";
#else
#error "Pre-mount directories must be provided for Windows"
#endif
    if (!tempDirPath.empty()) {
        preMountDirs["/tmp"] = "/tmp";
        envVars["TMPDIR"] = "/tmp";
    }
}

WamrExtInstance::InstRuntimeData::InstRuntimeData(const WamrExtInstanceConfig &config) {
    for (const auto& p : config.preMountDirs) {
        preMountHostDirs.push_back(p.first.c_str());
        preMountMapDirs.push_back(p.second.c_str());
    }
    for (const auto& p : config.envVars) {
        envVarsStringList.emplace_back(std::move(p.first + '=' + p.second));
        envVars.push_back(envVarsStringList.back().c_str());
    }
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};
};
