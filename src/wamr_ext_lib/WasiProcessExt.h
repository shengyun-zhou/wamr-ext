#pragma once
#include "../base/Utility.h"

namespace WAMR_EXT_NS {
    class WasiProcessExt {
    public:
        struct ProcManager {
        public:
            ProcManager() = default;
            ProcManager(const ProcManager&) = delete;
            ProcManager& operator=(const ProcManager&) = delete;
            friend class WasiProcessExt;
        private:
            struct ChildProcInfo {
#ifndef _WIN32
                int pollPipeFD{-1};
#endif
            };

            std::mutex lock;
            std::unordered_map<uint32_t, ChildProcInfo> childProcMap;
#ifndef _WIN32
            std::unordered_map<int, std::unordered_map<uint32_t, ChildProcInfo>::iterator> pollFDToProcMap;
#endif
        };

        static void Init();
    private:
        static std::mutex m_gProcessSpawnLock;

        static int32_t ProcessSpawn(wasm_exec_env_t pExecEnv, void* _pAppReq);
        static int32_t ProcessWaitPID(wasm_exec_env_t pExecEnv, int32_t pid, int32_t opt, int32_t* exitStatus, int32_t* retPID);
    };
}
