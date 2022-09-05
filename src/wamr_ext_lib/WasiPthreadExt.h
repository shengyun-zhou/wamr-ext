#pragma once

#include "../base/Utility.h"

namespace WAMR_EXT_NS {
    class WasiPthreadExt {
    public:
        WasiPthreadExt();
        static void Init();

        struct InstancePthreadManager {
        public:
            InstancePthreadManager();
            friend class WasiPthreadExt;
        private:
            std::mutex m_pthreadMutexMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_mutex_t>> m_pthreadMutexMap;
            std::mutex m_pthreadCondLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_cond_t>> m_pthreadCondMap;
            std::mutex m_pthreadRWLockMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<pthread_rwlock_t>> m_pthreadRWLockMap;
            std::mutex m_semaphoreMapLock;
            std::unordered_map<uint32_t, std::shared_ptr<sem_t>> m_semaphoreMap;
            std::atomic<uint32_t> m_curHandleId{1};

            std::shared_ptr<pthread_mutex_t> GetMutex(uint32_t* pHandleID, uint8_t autoCreatedMutexType);
            std::shared_ptr<pthread_cond_t> GetCond(uint32_t* pHandleID, bool bAutoCreated);
            std::shared_ptr<pthread_rwlock_t> GetRWLock(uint32_t* pHandleID, bool bAutoCreated);
            std::shared_ptr<sem_t> CreateSemaphore(uint32_t* pHandleID, uint32_t initVal);
            std::shared_ptr<sem_t> GetSemaphore(uint32_t handleID);
        };
    private:
        static InstancePthreadManager* GetInstPthreadManager(wasm_exec_env_t pExecEnv);
        static void GetTimeoutTimespec(struct timespec& ts, uint64_t useconds);
        static int32_t PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t* attr);
        static int32_t PthreadMutexLock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
        static int32_t PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
        static int32_t PthreadMutexTryLock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
        static int32_t PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds);
        static int32_t PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex);

        static int32_t PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond, void* attr);
        static int32_t PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond);
        static int32_t PthreadCondWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t* mutex);
        static int32_t PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t* mutex, uint64_t useconds);
        static int32_t PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond);
        static int32_t PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond);

        static int32_t PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock, void* attr);
        static int32_t PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockTryRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
        static int32_t PthreadRWLockWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockTryWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
        static int32_t PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
        static int32_t PthreadSetName(wasm_exec_env_t pExecEnv, char* name);
        static int32_t PthreadGetName(wasm_exec_env_t pExecEnv, char* nameBuf, uint32_t bufLen);

        static int32_t SemaphoreInit(wasm_exec_env_t pExecEnv, uint32_t *sem, uint32_t initVal);
        static int32_t SemaphoreWait(wasm_exec_env_t pExecEnv, uint32_t *sem);
        static int32_t SemaphoreTimedWait(wasm_exec_env_t pExecEnv, uint32_t *sem, uint64_t useconds);
        static int32_t SemaphoreTryWait(wasm_exec_env_t pExecEnv, uint32_t *sem);
        static int32_t SemaphorePost(wasm_exec_env_t pExecEnv, uint32_t *sem);
        static int32_t SemaphoreGetValue(wasm_exec_env_t pExecEnv, uint32_t *sem, int32_t *pAppOutVal);
        static int32_t SemaphoreDestroy(wasm_exec_env_t pExecEnv, uint32_t *sem);
    };
}
