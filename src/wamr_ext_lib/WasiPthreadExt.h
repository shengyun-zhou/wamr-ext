#pragma once

#include "../base/Utility.h"
#include <condition_variable>
#include <thread>

namespace WAMR_EXT_NS {
    class WasiPthreadExt {
    public:
        WasiPthreadExt() = default;
        static void Init();
        static void InitAppMainThreadInfo(wasm_exec_env_t pMainExecEnv);
        static void CleanupAppThreadInfo(wasm_exec_env_t pMainExecEnv);

        struct InstancePthreadManager {
        public:
            InstancePthreadManager();
            friend class WasiPthreadExt;
        private:
            struct ExecEnvThreadInfo {
                uint32_t handleID{0};
                std::shared_ptr<WASMExecEnv> pExecEnv;
                std::thread* pHostThread{nullptr};
                struct {
                    uint32_t funcArg{0};
                } appStartArg;
                struct {
                    uint32_t appStackAddr{0};
                    uint32_t stackSize{0};
                    bool bStackFromApp{false};
                } stackCtrl;
                struct {
                    std::mutex exitLock;
                    std::condition_variable exitCV;
                    int32_t waitCount{0};
                } exitCtrl;

                explicit ExecEnvThreadInfo(const std::shared_ptr<WASMExecEnv>& env) : pExecEnv(env) {
                    wasm_runtime_set_user_data(pExecEnv.get(), this);
                }

                ~ExecEnvThreadInfo() { delete pHostThread; }
            };

            std::mutex m_threadMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<ExecEnvThreadInfo>> m_threadMap;
            std::atomic<uint32_t> m_curHandleId{1};

            std::shared_ptr<ExecEnvThreadInfo> GetThreadInfo(uint32_t handleID);
        };
    private:
        static InstancePthreadManager::ExecEnvThreadInfo* GetExecEnvThreadInfo(wasm_exec_env_t pExecEnv);
        static InstancePthreadManager* GetInstPthreadManager(wasm_exec_env_t pExecEnv);
        static void DoHostThreadJoin(InstancePthreadManager* pManager, const std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo>& pThreadInfo);
        static void CancelAppThread(wasm_exec_env_t pExecEnv) { pExecEnv->suspend_flags.flags |= 0x01; }
        static void DoAppThreadExit(wasm_exec_env_t pExecEnv);

        static int32_t WasiThreadSpawn(wasm_exec_env_t pExecEnv, uint32_t appArg);
        static int32_t PthreadSetName(wasm_exec_env_t pExecEnv, char* name);
    };
}
