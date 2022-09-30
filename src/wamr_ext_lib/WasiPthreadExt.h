#pragma once

#include "../base/Utility.h"
#include <condition_variable>

namespace WAMR_EXT_NS {
    class WasiPthreadExt {
    public:
        WasiPthreadExt();
        static void Init();
        static void InitAppMainThreadInfo(wasm_exec_env_t pMainExecEnv);
        static void CleanupAppThreadInfo(wasm_exec_env_t pMainExecEnv);

        struct InstancePthreadManager {
        public:
            InstancePthreadManager();
            friend class WasiPthreadExt;
        private:
            const static uint32_t MAX_KEY_NUM = 128;

            struct ExecEnvThreadInfo {
                uint32_t handleID{0};
                wasm_exec_env_t pExecEnv;
                pthread_t hostThreadHandler;
                uint32_t keyData[MAX_KEY_NUM]{0};
                struct {
                    uint32_t appStartFunc{0};
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
                    uint32_t appRetValAddr{0};
                    bool bAppDetached{false};
                } exitCtrl;

                explicit ExecEnvThreadInfo(wasm_exec_env_t env) : pExecEnv(env) {
                    wasm_runtime_set_user_data(pExecEnv, this);
                }
            };
            struct KeyInfo {
                uint32_t appDestructorFunc{0};
                bool bCreated{false};
            };

            std::mutex m_threadMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<ExecEnvThreadInfo>> m_threadMap;
            std::mutex m_keyInfoLock;
            KeyInfo m_keyInfoArr[MAX_KEY_NUM];
            std::mutex m_pthreadMutexMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_mutex_t>> m_pthreadMutexMap;
            std::mutex m_pthreadCondLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_cond_t>> m_pthreadCondMap;
            std::mutex m_pthreadRWLockMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_rwlock_t>> m_pthreadRWLockMap;
            std::atomic<uint32_t> m_curHandleId{1};

            std::shared_ptr<ExecEnvThreadInfo> GetThreadInfo(uint32_t handleID);
            std::shared_ptr<pthread_mutex_t> GetMutex(uint32_t* pHandleID, uint8_t autoCreatedMutexType);
            std::shared_ptr<pthread_cond_t> GetCond(uint32_t* pHandleID, bool bAutoCreated);
            std::shared_ptr<pthread_rwlock_t> GetRWLock(uint32_t* pHandleID, bool bAutoCreated);
        };
    private:
        static InstancePthreadManager::ExecEnvThreadInfo* GetExecEnvThreadInfo(wasm_exec_env_t pExecEnv);
        static InstancePthreadManager* GetInstPthreadManager(wasm_exec_env_t pExecEnv);
        static void DoHostThreadJoin(InstancePthreadManager* pManager, const std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo>& pThreadInfo);
        static void CancelAppThread(wasm_exec_env_t pExecEnv) { pExecEnv->suspend_flags.flags |= 0x01; }
        static void DoAppThreadExit(wasm_exec_env_t pExecEnv);
        static void GetTimeoutTimespec(struct timespec& ts, uint64_t useconds);

        static int32_t PthreadCreate(wasm_exec_env_t pExecEnv, void* _pAppCreateThreadReq);
        static int32_t PthreadSelf(wasm_exec_env_t pExecEnv, uint32_t *thread);
        static int32_t PthreadExit(wasm_exec_env_t pExecEnv, uint32_t retval);
        static int32_t PthreadJoin(wasm_exec_env_t pExecEnv, uint32_t thread, uint32_t* retVal);
        static int32_t PthreadDetach(wasm_exec_env_t pExecEnv, uint32_t thread);

        static int32_t PthreadKeyCreate(wasm_exec_env_t pExecEnv, uint32_t* key, uint32_t appDestructorFunc);
        static int32_t PthreadKeyDelete(wasm_exec_env_t pExecEnv, uint32_t key);
        static int32_t PthreadSetSpecific(wasm_exec_env_t pExecEnv, uint32_t key, uint32_t val);
        static int32_t PthreadGetSpecific(wasm_exec_env_t pExecEnv, uint32_t key, uint32_t* val);

        static int32_t PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t mutexType);
        static int32_t PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
        static int32_t PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds);
        static int32_t PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex);

        static int32_t PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond);
        static int32_t PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond);
        static int32_t PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t* mutex, uint64_t useconds);
        static int32_t PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond);
        static int32_t PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond);

        static int32_t PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
        static int32_t PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
        static int32_t PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadSetName(wasm_exec_env_t pExecEnv, char* name);
    };
}
