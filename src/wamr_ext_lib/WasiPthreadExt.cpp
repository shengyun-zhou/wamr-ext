#include "WasiPthreadExt.h"
#include "WamrExtInternalDef.h"

namespace WAMR_EXT_NS {
    void WasiPthreadExt::Init() {
        static NativeSymbol nativeSymbols[] = {
            {"pthread_mutex_init", (void*)PthreadMutexInit, "(**)i"},
            {"pthread_mutex_lock", (void*)PthreadMutexLock, "(*)i"},
            {"pthread_mutex_unlock", (void*)PthreadMutexUnlock, "(*)i"},
            {"pthread_mutex_trylock", (void*)PthreadMutexTryLock, "(*)i"},
            {"pthread_mutex_timedlock", (void*)PthreadMutexTimedLock, "(*I)i"},
            {"pthread_mutex_destroy", (void*)PthreadMutexDestroy, "(*)i"},

            {"pthread_cond_init", (void*)PthreadCondInit, "(**)i"},
            {"pthread_cond_destroy", (void*)PthreadCondDestroy, "(*)i"},
            {"pthread_cond_wait", (void*)PthreadCondWait, "(**)i"},
            {"pthread_cond_timedwait", (void*)PthreadCondTimedWait, "(**I)i"},
            {"pthread_cond_broadcast", (void*)PthreadCondBroadcast, "(*)i"},
            {"pthread_cond_signal", (void*)PthreadCondSignal, "(*)i"},

            {"pthread_rwlock_init", (void*)PthreadRWLockInit, "(**)i"},
            {"pthread_rwlock_destroy", (void*)PthreadRWLockDestroy, "(*)i"},
            {"pthread_rwlock_rdlock", (void*)PthreadRWLockRdLock, "(*)i"},
            {"pthread_rwlock_tryrdlock", (void*)PthreadRWLockTryRdLock, "(*)i"},
            {"pthread_rwlock_timedrdlock", (void*)PthreadRWLockTimedRdLock, "(*I)i"},
            {"pthread_rwlock_wrlock", (void*)PthreadRWLockWrLock, "(*)i"},
            {"pthread_rwlock_trywrlock", (void*)PthreadRWLockTryWrLock, "(*)i"},
            {"pthread_rwlock_timedwrlock", (void*)PthreadRWLockTimedWrLock, "(*I)i"},
            {"pthread_rwlock_unlock", (void*)PthreadRWLockUnlock, "(*)i"},
            {"pthread_setname_np", (void*)PthreadSetName, "(*)i"},
            {"pthread_getname_np", (void*)PthreadGetName, "(*i)i"},

            {"sem_init", (void*)SemaphoreInit, "(*i)i"},
            {"sem_wait", (void*)SemaphoreWait, "(*)i"},
            {"sem_timedwait", (void*)SemaphoreTimedWait, "(*I)i"},
            {"sem_trywait", (void*)SemaphoreTryWait, "(*)i"},
            {"sem_post", (void*)SemaphorePost, "(*)i"},
            {"sem_getvalue", (void*)SemaphoreGetValue, "(**)i"},
            {"sem_destroy", (void*)SemaphoreDestroy, "(*)i"},
        };
        wasm_runtime_register_natives("pthread_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    }

    WasiPthreadExt::InstancePthreadManager::InstancePthreadManager() {
        m_pthreadMutexMap.reserve(30);
        m_pthreadCondMap.reserve(15);
        m_pthreadRWLockMap.reserve(10);
    }

#define PTHREAD_EXT_MUTEX_UNINIT_ID 0
#define PTHREAD_EXT_COND_UNINIT_ID 0
#define PTHREAD_EXT_RWLOCK_UNINIT_ID 0

    std::shared_ptr<pthread_mutex_t> WasiPthreadExt::InstancePthreadManager::GetMutex(uint32_t *pHandleID, uint8_t autoCreatedMutexType) {
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
                    pMutex.reset(new pthread_mutex_t, [](pthread_mutex_t* p) {
                        pthread_mutex_destroy(p);
                        delete p;
                    });
                    pRetMutex = pMutex;
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

    std::shared_ptr<pthread_cond_t> WasiPthreadExt::InstancePthreadManager::GetCond(uint32_t *pHandleID, bool bAutoCreated) {
        std::shared_ptr<pthread_cond_t> pRetCond;
        std::lock_guard<std::mutex> _al(m_pthreadCondLock);
        if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID) {
            if (!bAutoCreated)
                return nullptr;
            while (true) {
                *pHandleID = m_curHandleId++;
                if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID)
                    continue;
                auto &pCond = m_pthreadCondMap[*pHandleID];
                if (!pCond) {
                    pCond.reset(new pthread_cond_t, [](pthread_cond_t* p) {
                        pthread_cond_destroy(p);
                        delete p;
                    });
                    pRetCond = pCond;
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

    std::shared_ptr<pthread_rwlock_t> WasiPthreadExt::InstancePthreadManager::GetRWLock(uint32_t *pHandleID, bool bAutoCreated) {
        std::shared_ptr<pthread_rwlock_t> pRetRWLock;
        std::lock_guard<std::mutex> _al(m_pthreadRWLockMapLock);
        if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID) {
            if (!bAutoCreated)
                return nullptr;
            while (true) {
                *pHandleID = m_curHandleId++;
                if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID)
                    continue;
                auto &pRWLock = m_pthreadRWLockMap[*pHandleID];
                if (!pRWLock) {
                    pRWLock.reset(new pthread_rwlock_t, [](pthread_rwlock_t* p) {
                        pthread_rwlock_destroy(p);
                        delete p;
                    });
                    pRetRWLock = pRWLock;
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

    std::shared_ptr<sem_t> WasiPthreadExt::InstancePthreadManager::CreateSemaphore(uint32_t *pHandleID, uint32_t initVal) {
        sem_t* pNewSemaphore = new sem_t;
        if (sem_init(pNewSemaphore, 0, initVal) != 0) {
            delete pNewSemaphore;
            return nullptr;
        }
        std::lock_guard<std::mutex> _al(m_semaphoreMapLock);
        while (true) {
            *pHandleID = m_curHandleId++;
            auto &pSema = m_semaphoreMap[*pHandleID];
            if (!pSema) {
                pSema.reset(pNewSemaphore, [](sem_t *p) {
                    sem_destroy(p);
                    delete p;
                });
                return pSema;
            }
        }
        delete pNewSemaphore;
        return nullptr;
    }

    std::shared_ptr<sem_t> WasiPthreadExt::InstancePthreadManager::GetSemaphore(uint32_t handleID) {
        std::lock_guard<std::mutex> _al(m_semaphoreMapLock);
        auto it = m_semaphoreMap.find(handleID);
        if (it != m_semaphoreMap.end())
            return it->second;
        return nullptr;
    }

    WasiPthreadExt::InstancePthreadManager* WasiPthreadExt::GetInstPthreadManager(wasm_exec_env_t pExecEnv) {
        auto* pWamrExtInst = (WamrExtInstance*)wasm_runtime_get_custom_data(get_module_inst(pExecEnv));
        return &pWamrExtInst->wasiPthreadManager;
    }

    void WasiPthreadExt::GetTimeoutTimespec(struct timespec &ts, uint64_t useconds) {
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
        while (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ++ts.tv_sec;
        }
    }

    int32_t WasiPthreadExt::PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t *attr) {
        auto *pManager = GetInstPthreadManager(pExecEnv);
        bool bRecursive = false;
        if (attr)
            bRecursive = (*(attr) & 1);
        *mutex = PTHREAD_EXT_MUTEX_UNINIT_ID;
        pManager->GetMutex(mutex, bRecursive ? 2 : 1);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadMutexLock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        auto *pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_lock(pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 0);
        if (!pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_unlock(pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadMutexTryLock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_trylock(pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pMutex)
            return UVWASI_EINVAL;
        struct timespec ts;
        GetTimeoutTimespec(ts, useconds);
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_timedlock(pMutex.get(), &ts));
    }

    int32_t WasiPthreadExt::PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        if (*mutex == PTHREAD_EXT_MUTEX_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadMutexMapLock);
        auto it = pManager->m_pthreadMutexMap.find(*mutex);
        if (it == pManager->m_pthreadMutexMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadMutexMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond, void *) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        *cond = PTHREAD_EXT_COND_UNINIT_ID;
        pManager->GetCond(cond, true);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        if (*cond == PTHREAD_EXT_COND_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadCondLock);
        auto it = pManager->m_pthreadCondMap.find(*cond);
        if (it == pManager->m_pthreadCondMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadCondMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pCond || !pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_wait(pCond.get(), pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pCond || !pMutex)
            return UVWASI_EINVAL;
        struct timespec ts;
        GetTimeoutTimespec(ts, useconds);
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_timedwait(pCond.get(), pMutex.get(), &ts));
    }

    int32_t WasiPthreadExt::PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        if (!pCond)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_signal(pCond.get()));
    }

    int32_t WasiPthreadExt::PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        if (!pCond)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_broadcast(pCond.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock, void *) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        *rwlock = PTHREAD_EXT_RWLOCK_UNINIT_ID;
        pManager->GetRWLock(rwlock, true);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        if (*rwlock == PTHREAD_EXT_RWLOCK_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadRWLockMapLock);
        auto it = pManager->m_pthreadRWLockMap.find(*rwlock);
        if (it == pManager->m_pthreadRWLockMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadRWLockMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadRWLockRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_rdlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockTryRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_tryrdlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        struct timespec ts;
        GetTimeoutTimespec(ts, useconds);
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_timedrdlock(pRWLock.get(), &ts));
    }

    int32_t WasiPthreadExt::PthreadRWLockWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_wrlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockTryWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_trywrlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        struct timespec ts;
        GetTimeoutTimespec(ts, useconds);
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_timedwrlock(pRWLock.get(), &ts));
    }

    int32_t WasiPthreadExt::PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, false);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_unlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadSetName(wasm_exec_env_t pExecEnv, char *name) {
        Utility::SetCurrentThreadName(name);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadGetName(wasm_exec_env_t pExecEnv, char *nameBuf, uint32_t bufLen) {
        snprintf(nameBuf, bufLen, "%s", Utility::GetCurrentThreadName().c_str());
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreInit(wasm_exec_env_t pExecEnv, uint32_t *sem, uint32_t initVal) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        if (!pManager->CreateSemaphore(sem, initVal))
            return Utility::ConvertErrnoToWasiErrno(errno);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreWait(wasm_exec_env_t pExecEnv, uint32_t *sem) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pSema = pManager->GetSemaphore(*sem);
        if (!pSema)
            return UVWASI_EINVAL;
        if (sem_wait(pSema.get()) != 0)
            return Utility::ConvertErrnoToWasiErrno(errno);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreTimedWait(wasm_exec_env_t pExecEnv, uint32_t *sem, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pSema = pManager->GetSemaphore(*sem);
        if (!pSema)
            return UVWASI_EINVAL;
        struct timespec ts;
        GetTimeoutTimespec(ts, useconds);
        if (sem_timedwait(pSema.get(), &ts) != 0)
            return Utility::ConvertErrnoToWasiErrno(errno);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreTryWait(wasm_exec_env_t pExecEnv, uint32_t *sem) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pSema = pManager->GetSemaphore(*sem);
        if (!pSema)
            return UVWASI_EINVAL;
        if (sem_trywait(pSema.get()) != 0)
            return Utility::ConvertErrnoToWasiErrno(errno);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphorePost(wasm_exec_env_t pExecEnv, uint32_t *sem) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pSema = pManager->GetSemaphore(*sem);
        if (!pSema)
            return UVWASI_EINVAL;
        if (sem_post(pSema.get()) != 0)
            return Utility::ConvertErrnoToWasiErrno(errno);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreGetValue(wasm_exec_env_t pExecEnv, uint32_t *sem, int32_t *pAppOutVal) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pSema = pManager->GetSemaphore(*sem);
        if (!pSema)
            return UVWASI_EINVAL;
        int hostVal = 0;
        if (sem_getvalue(pSema.get(), &hostVal) != 0)
            return Utility::ConvertErrnoToWasiErrno(errno);
        // Report 0 instead of negative number if one or more threads are blocked waiting to lock the semaphore
        *pAppOutVal = std::max(hostVal, 0);
        return 0;
    }

    int32_t WasiPthreadExt::SemaphoreDestroy(wasm_exec_env_t pExecEnv, uint32_t *sem) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_semaphoreMapLock);
        auto it = pManager->m_semaphoreMap.find(*sem);
        if (it == pManager->m_semaphoreMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_semaphoreMap.erase(it);
        return 0;
    }
}
