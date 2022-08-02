#pragma once

#include "../base/Utility.h"

namespace WAMR_EXT_NS {
class PthreadExt {
public:
    PthreadExt();
    static void Init();
private:
    std::mutex m_pthreadMutexMapLock;
    std::unordered_map<uint32_t, std::shared_ptr<pthread_mutex_t>> m_pthreadMutexMap;
    std::mutex m_pthreadCondLock;
    std::unordered_map<uint32_t, std::shared_ptr<pthread_cond_t>> m_pthreadCondMap;
    std::mutex m_pthreadRWLockMapLock;
    std::unordered_map<uint32_t, std::shared_ptr<pthread_rwlock_t>> m_pthreadRWLockMap;
    std::atomic<uint32_t> m_curHandleId{1};

    static PthreadExt g_instance;

    static void GetTimeoutTimespec(struct timespec& ts, uint64_t useconds);
    std::shared_ptr<pthread_mutex_t> GetMutex(uint32_t* pHandleID, uint8_t autoCreatedMutexType);
    static uint32_t PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t* attr);
    static uint32_t PthreadMutexLock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
    static uint32_t PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
    static uint32_t PthreadMutexTryLock(wasm_exec_env_t pExecEnv, uint32_t *mutex);
    static uint32_t PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds);
    static uint32_t PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex);

    std::shared_ptr<pthread_cond_t> GetCond(uint32_t* pHandleID, bool bAutoCreated);
    static uint32_t PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond, void* attr);
    static uint32_t PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond);
    static uint32_t PthreadCondWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t* mutex);
    static uint32_t PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t* mutex, uint64_t useconds);
    static uint32_t PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond);
    static uint32_t PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond);

    std::shared_ptr<pthread_rwlock_t> GetRWLock(uint32_t* pHandleID, bool bAutoCreated);
    static uint32_t PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock, void* attr);
    static uint32_t PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
    static uint32_t PthreadRWLockRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
    static uint32_t PthreadRWLockTryRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
    static uint32_t PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
    static uint32_t PthreadRWLockWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
    static uint32_t PthreadRWLockTryWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
    static uint32_t PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds);
    static uint32_t PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock);
};
}
