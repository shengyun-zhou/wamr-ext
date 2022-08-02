#include "PthreadExt.h"

namespace WAMR_EXT_NS {

void PthreadExt::Init() {
    static NativeSymbol nativeSymbols[] = {
        {"pthread_mutex_init", (void*)PthreadMutexInit, "(**)i", &g_instance},
        {"pthread_mutex_lock", (void*)PthreadMutexLock, "(*)i", &g_instance},
        {"pthread_mutex_unlock", (void*)PthreadMutexUnlock, "(*)i", &g_instance},
        {"pthread_mutex_trylock", (void*)PthreadMutexTryLock, "(*)i", &g_instance},
        {"pthread_mutex_timedlock", (void*)PthreadMutexTimedLock, "(*I)i", &g_instance},
        {"pthread_mutex_destroy", (void*)PthreadMutexDestroy, "(*)i", &g_instance},

        {"pthread_cond_init", (void*)PthreadCondInit, "(**)i", &g_instance},
        {"pthread_cond_destroy", (void*)PthreadCondDestroy, "(*)i", &g_instance},
        {"pthread_cond_wait", (void*)PthreadCondWait, "(**)i", &g_instance},
        {"pthread_cond_timedwait", (void*)PthreadCondTimedWait, "(**I)i", &g_instance},
        {"pthread_cond_broadcast", (void*)PthreadCondBroadcast, "(*)i", &g_instance},
        {"pthread_cond_signal", (void*)PthreadCondSignal, "(*)i", &g_instance},

        {"pthread_rwlock_init", (void*)PthreadRWLockInit, "(**)i", &g_instance},
        {"pthread_rwlock_destroy", (void*)PthreadRWLockDestroy, "(*)i", &g_instance},
        {"pthread_rwlock_rdlock", (void*)PthreadRWLockRdLock, "(*)i", &g_instance},
        {"pthread_rwlock_tryrdlock", (void*)PthreadRWLockTryRdLock, "(*)i", &g_instance},
        {"pthread_rwlock_timedrdlock", (void*)PthreadRWLockTimedRdLock, "(*I)i", &g_instance},
        {"pthread_rwlock_wrlock", (void*)PthreadRWLockWrLock, "(*)i", &g_instance},
        {"pthread_rwlock_trywrlock", (void*)PthreadRWLockTryWrLock, "(*)i", &g_instance},
        {"pthread_rwlock_timedwrlock", (void*)PthreadRWLockTimedWrLock, "(*I)i", &g_instance},
        {"pthread_rwlock_unlock", (void*)PthreadRWLockUnlock, "(*)i", &g_instance},
    };
    wasm_runtime_register_natives("pthread_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
}

PthreadExt::PthreadExt() {
    m_pthreadMutexMap.reserve(200);
    m_pthreadCondMap.reserve(100);
    m_pthreadRWLockMap.reserve(50);
}

void PthreadExt::GetTimeoutTimespec(struct timespec &ts, uint64_t useconds) {
#ifdef _WIN32
    timeval tv;
    gettimeofday(&tv, nullptr);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    ts.tv_sec += (time_t)useconds / (1000 * 1000);
    ts.tv_nsec += long(useconds % (1000 * 1000)) * 1000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_nsec -= 1000000000;
        ++ts.tv_sec;
    }
}

#define PTHREAD_EXT_MUTEX_UNINIT_ID 0
#define PTHREAD_EXT_COND_UNINIT_ID 0
#define PTHREAD_EXT_RWLOCK_UNINIT_ID 0

std::shared_ptr<pthread_mutex_t> PthreadExt::GetMutex(uint32_t* pHandleID, uint8_t autoCreatedMutexType) {
    std::shared_ptr<pthread_mutex_t> pRetMutex;
    std::lock_guard<std::mutex> _al(m_pthreadMutexMapLock);
    if (*pHandleID == PTHREAD_EXT_MUTEX_UNINIT_ID) {
        if (autoCreatedMutexType == 0)
            return nullptr;
        while (true) {
            *pHandleID = m_curHandleId++;
            if (*pHandleID == PTHREAD_EXT_MUTEX_UNINIT_ID)
                continue;
            auto &pMutex = m_pthreadMutexMap[*pHandleID];
            if (!pMutex) {
                pRetMutex = pMutex = std::make_shared<pthread_mutex_t>();
                pthread_mutexattr_t mutexAttr;
                pthread_mutexattr_init(&mutexAttr);
                if (autoCreatedMutexType == 2)
                    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
                else
                    pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_DEFAULT);
                pthread_mutex_init(pMutex.get(), &mutexAttr);
                pthread_mutexattr_destroy(&mutexAttr);
                break;
            }
        }
    } else {
        auto it = m_pthreadMutexMap.find(*pHandleID);
        if (it != m_pthreadMutexMap.end())
            pRetMutex = it->second;
    }
    return pRetMutex;
}

uint32_t PthreadExt::PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t *attr) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    bool bRecursive = false;
    if (attr)
        bRecursive = (*(attr) & 1);
    *mutex = PTHREAD_EXT_MUTEX_UNINIT_ID;
    pThis->GetMutex(mutex, bRecursive ? 2 : 1);
    return 0;
}

uint32_t PthreadExt::PthreadMutexLock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pMutex = pThis->GetMutex(mutex, 1);
    if (!pMutex)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_mutex_lock(pMutex.get()));
}

uint32_t PthreadExt::PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pMutex = pThis->GetMutex(mutex, 0);
    if (!pMutex)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_mutex_unlock(pMutex.get()));
}

uint32_t PthreadExt::PthreadMutexTryLock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pMutex = pThis->GetMutex(mutex, 1);
    if (!pMutex)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_mutex_trylock(pMutex.get()));
}

uint32_t PthreadExt::PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pMutex = pThis->GetMutex(mutex, 1);
    if (!pMutex)
        return __WASI_EINVAL;
    struct timespec ts;
    GetTimeoutTimespec(ts, useconds);
    return Utility::ConvertErrnoToWasiErrno(pthread_mutex_timedlock(pMutex.get(), &ts));
}

uint32_t PthreadExt::PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
    if (*mutex == PTHREAD_EXT_MUTEX_UNINIT_ID)
        return 0;
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    std::lock_guard<std::mutex> _al(pThis->m_pthreadMutexMapLock);
    auto it = pThis->m_pthreadMutexMap.find(*mutex);
    if (it == pThis->m_pthreadMutexMap.end()) {
        assert(false);
        return __WASI_EINVAL;
    }
    pthread_mutex_destroy(it->second.get());
    pThis->m_pthreadMutexMap.erase(it);
    return 0;
}

std::shared_ptr<pthread_cond_t> PthreadExt::GetCond(uint32_t *pHandleID, bool bAutoCreated) {
    std::shared_ptr<pthread_cond_t> pRetCond;
    std::lock_guard<std::mutex> _al(m_pthreadCondLock);
    if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID) {
        if (!bAutoCreated)
            return nullptr;
        while (true) {
            *pHandleID = m_curHandleId++;
            if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID)
                continue;
            auto& pCond = m_pthreadCondMap[*pHandleID];
            if (!pCond) {
                pRetCond = pCond = std::make_shared<pthread_cond_t>();
                pthread_cond_init(pCond.get(), nullptr);
                break;
            }
        }
    } else {
        auto it = m_pthreadCondMap.find(*pHandleID);
        if (it != m_pthreadCondMap.end())
            pRetCond = it->second;
    }
    return pRetCond;
}

uint32_t PthreadExt::PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond, void*) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    *cond = PTHREAD_EXT_COND_UNINIT_ID;
    pThis->GetCond(cond, true);
    return 0;
}

uint32_t PthreadExt::PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond) {
    if (*cond == PTHREAD_EXT_COND_UNINIT_ID)
        return 0;
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    std::lock_guard<std::mutex> _al(pThis->m_pthreadCondLock);
    auto it = pThis->m_pthreadCondMap.find(*cond);
    if (it == pThis->m_pthreadCondMap.end()) {
        assert(false);
        return __WASI_EINVAL;
    }
    pthread_cond_destroy(it->second.get());
    pThis->m_pthreadCondMap.erase(it);
    return 0;
}

uint32_t PthreadExt::PthreadCondWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pCond = pThis->GetCond(cond, true);
    auto pMutex = pThis->GetMutex(mutex, 1);
    if (!pCond || !pMutex)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_cond_wait(pCond.get(), pMutex.get()));
}

uint32_t PthreadExt::PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex, uint64_t useconds) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pCond = pThis->GetCond(cond, true);
    auto pMutex = pThis->GetMutex(mutex, 1);
    if (!pCond || !pMutex)
        return __WASI_EINVAL;
    struct timespec ts;
    GetTimeoutTimespec(ts, useconds);
    return Utility::ConvertErrnoToWasiErrno(pthread_cond_timedwait(pCond.get(), pMutex.get(), &ts));
}

uint32_t PthreadExt::PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pCond = pThis->GetCond(cond, true);
    if (!pCond)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_cond_signal(pCond.get()));
}

uint32_t PthreadExt::PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pCond = pThis->GetCond(cond, true);
    if (!pCond)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_cond_broadcast(pCond.get()));
}

std::shared_ptr<pthread_rwlock_t> PthreadExt::GetRWLock(uint32_t *pHandleID, bool bAutoCreated) {
    std::shared_ptr<pthread_rwlock_t> pRetRWLock;
    std::lock_guard<std::mutex> _al(m_pthreadRWLockMapLock);
    if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID) {
        if (!bAutoCreated)
            return nullptr;
        while (true) {
            *pHandleID = m_curHandleId++;
            if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID)
                continue;
            auto& pRWLock = m_pthreadRWLockMap[*pHandleID];
            if (!pRWLock) {
                pRetRWLock = pRWLock = std::make_shared<pthread_rwlock_t>();
                pthread_rwlock_init(pRWLock.get(), nullptr);
                break;
            }
        }
    } else {
        auto it = m_pthreadRWLockMap.find(*pHandleID);
        if (it != m_pthreadRWLockMap.end())
            pRetRWLock = it->second;
    }
    return pRetRWLock;
}

uint32_t PthreadExt::PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock, void*) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    *rwlock = PTHREAD_EXT_RWLOCK_UNINIT_ID;
    pThis->GetRWLock(rwlock, true);
    return 0;
}

uint32_t PthreadExt::PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    if (*rwlock == PTHREAD_EXT_RWLOCK_UNINIT_ID)
        return 0;
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    std::lock_guard<std::mutex> _al(pThis->m_pthreadRWLockMapLock);
    auto it = pThis->m_pthreadRWLockMap.find(*rwlock);
    if (it == pThis->m_pthreadRWLockMap.end()) {
        assert(false);
        return __WASI_EINVAL;
    }
    pthread_rwlock_destroy(it->second.get());
    pThis->m_pthreadRWLockMap.erase(it);
    return 0;
}

uint32_t PthreadExt::PthreadRWLockRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_rdlock(pRWLock.get()));
}

uint32_t PthreadExt::PthreadRWLockTryRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_tryrdlock(pRWLock.get()));
}

uint32_t PthreadExt::PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    struct timespec ts;
    GetTimeoutTimespec(ts, useconds);
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_timedrdlock(pRWLock.get(), &ts));
}

uint32_t PthreadExt::PthreadRWLockWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_wrlock(pRWLock.get()));
}

uint32_t PthreadExt::PthreadRWLockTryWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_trywrlock(pRWLock.get()));
}

uint32_t PthreadExt::PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, true);
    if (!pRWLock)
        return __WASI_EINVAL;
    struct timespec ts;
    GetTimeoutTimespec(ts, useconds);
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_timedwrlock(pRWLock.get(), &ts));
}

uint32_t PthreadExt::PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
    auto* pThis = (PthreadExt*)wasm_runtime_get_function_attachment(pExecEnv);
    auto pRWLock = pThis->GetRWLock(rwlock, false);
    if (!pRWLock)
        return __WASI_EINVAL;
    return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_unlock(pRWLock.get()));
}

}
