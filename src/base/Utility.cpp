#include "Utility.h"
#ifdef __linux__
#include <syscall.h>
#elif defined(__CYGWIN__)
#include <windows.h>
#endif
#ifndef _WIN32
#include <pthread.h>
#endif
extern "C" {
#include <uv_mapping.h>
}

namespace WAMR_EXT_NS {
    uint32_t Utility::GetProcessID() {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return getpid();
#endif
    }

    uint32_t Utility::GetCurrentThreadID() {
#if defined(_WIN32) || defined(__CYGWIN__)
        return GetCurrentThreadId();
#elif defined(__linux__)
        return syscall(__NR_gettid);
#elif defined(__APPLE__)
        return pthread_mach_thread_np(pthread_self());
#elif defined(__FreeBSD__)
        return pthread_getthreadid_np();
#else
        return uintptr_t(pthread_self());
#endif
    }

    uvwasi_errno_t Utility::ConvertErrnoToWasiErrno(int error) {
        if (error == 0)
            return 0;
        return uvwasi__translate_uv_error(uv_translate_sys_error(error));
    }
}
