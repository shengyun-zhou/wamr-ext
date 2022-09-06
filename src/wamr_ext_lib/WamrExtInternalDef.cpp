#include "WamrExtInternalDef.h"
#include "../base/FSUtility.h"
#include "uv.h"
extern "C" {
#include <fd_table.h>
}

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

WamrExtInstance::~WamrExtInstance() {
    if (wasmMainInstance) {
        // Close all FDs opened by app
        std::vector<uint32_t> appFDs;
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(wasmMainInstance);
        uvwasi_fd_table_lock(pUVWasi->fds);
        appFDs.reserve(pUVWasi->fds->size);
        for (uint32_t i = 0; i < pUVWasi->fds->size; i++) {
            auto pEntry = pUVWasi->fds->fds[i];
            if (pEntry)
                appFDs.push_back(pEntry->id);
        }
        uvwasi_fd_table_unlock(pUVWasi->fds);
        for (auto appFD : appFDs)
            uvwasi_fd_close(pUVWasi, appFD);
    }
    for (const auto& p : wasmExecEnvMap)
        wasm_exec_env_destroy(p.second);
    wasmExecEnvMap.clear();
    if (wasmMainInstance)
        wasm_runtime_deinstantiate(wasmMainInstance);
    wasmMainInstance = nullptr;
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};
};
