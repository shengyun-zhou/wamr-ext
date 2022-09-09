#include "WasiPthreadExt.h"
#include "WamrExtInternalDef.h"

namespace WAMR_EXT_NS {
    void WasiPthreadExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_INIT, std::make_shared<ExtSyscall_P_U32>((void*)PthreadMutexInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_TIMEDLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadMutexTimedLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_UNLOCK, std::make_shared<ExtSyscall_P>((void*)PthreadMutexUnlock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadMutexDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_INIT, std::make_shared<ExtSyscall_P>((void*)PthreadCondInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_TIMEDWAIT, std::make_shared<ExtSyscall_P_P_U64>((void*)PthreadCondTimedWait));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_BROADCAST, std::make_shared<ExtSyscall_P>((void*)PthreadCondBroadcast));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_SIGNAL, std::make_shared<ExtSyscall_P>((void*)PthreadCondSignal));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadCondDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_INIT, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDRDLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadRWLockTimedRdLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDWRLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadRWLockTimedWrLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_UNLOCK, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockUnlock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_HOST_SETNAME, std::make_shared<ExtSyscall_S>((void*)PthreadSetName));
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

    int32_t WasiPthreadExt::PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t mutexType) {
        auto *pManager = GetInstPthreadManager(pExecEnv);
        bool bRecursive = mutexType == 1;
        *mutex = PTHREAD_EXT_MUTEX_UNINIT_ID;
        pManager->GetMutex(mutex, bRecursive ? 2 : 1);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 0);
        if (!pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_unlock(pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pMutex)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_mutex_lock(pMutex.get());
        } else if (useconds == 0) {
            err = pthread_mutex_trylock(pMutex.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_mutex_timedlock(pMutex.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
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

    int32_t WasiPthreadExt::PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond) {
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

    int32_t WasiPthreadExt::PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pCond || !pMutex)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_cond_wait(pCond.get(), pMutex.get());
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_cond_timedwait(pCond.get(), pMutex.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
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

    int32_t WasiPthreadExt::PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
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

    int32_t WasiPthreadExt::PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_rwlock_rdlock(pRWLock.get());
        } else if (useconds == 0) {
            err = pthread_rwlock_tryrdlock(pRWLock.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_rwlock_timedrdlock(pRWLock.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    int32_t WasiPthreadExt::PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_rwlock_wrlock(pRWLock.get());
        } else if (useconds == 0) {
            err = pthread_rwlock_trywrlock(pRWLock.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_rwlock_timedwrlock(pRWLock.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
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
}
