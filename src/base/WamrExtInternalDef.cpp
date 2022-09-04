#include "WamrExtInternalDef.h"
#include "FSUtility.h"
#include <uv.h>

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
    for (const auto& p : config.args)
        argv.push_back(p.c_str());
#ifndef _WIN32
    for (const auto& p : {
        std::make_pair(&newStdinFD, fileno(stdin)),
        std::make_pair(&newStdOutFD, fileno(stdout)),
        std::make_pair(&newStdErrFD, fileno(stderr)),
    }) {
        *p.first = -1;
        if (p.second != -1 && (*p.first = dup(p.second)) != -1)
            *p.first = uv_open_osfhandle(*p.first);
    }
#else
#error "Duplicating file handler of stdin, stdout and stderr is not supported for Win32"
#endif
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};
};
