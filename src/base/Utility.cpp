#include "Utility.h"
#ifdef __linux__
#include <syscall.h>
#include <sys/prctl.h>
#elif defined(__CYGWIN__)
#include <windows.h>
#endif
#ifndef _WIN32
#include <pthread.h>
#endif

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

    void Utility::SetCurrentThreadName(const char *name) {
#if defined(__APPLE__)
        pthread_setname_np(name);
#elif defined(__CYGWIN__) || defined(__FreeBSD__) || defined(_WIN32)
        pthread_setname_np(pthread_self(), name);
#elif defined(__linux__)
        prctl(PR_SET_NAME, name);
#endif
    }

    std::string Utility::GetCurrentThreadName() {
        char tempThreadName[64] = {0};
#ifdef __linux__
        if (prctl(PR_GET_NAME, tempThreadName) == -1)
            return "";
#elif defined(_WIN32) || defined(__APPLE__) || defined(__CYGWIN__) || defined(__FreeBSD__)
        int err = pthread_getname_np(pthread_self(), tempThreadName, sizeof(tempThreadName));
        if (err != 0)
            return "";
#else
        return "";
#endif
        return tempThreadName;
    }

    uvwasi_errno_t Utility::GetHostFDByAppFD(wasm_module_inst_t pWasmModuleInst, int32_t appFD, uv_os_fd_t &outHostFD,
                                             const std::function<void(const uvwasi_fd_wrap_t*)> &cb) {
        uvwasi_t *pUVWasi = &wasm_runtime_get_wasi_ctx(pWasmModuleInst)->uvwasi;
        uvwasi_fd_wrap_t* pFDWrap = nullptr;
        uvwasi_errno_t err = uvwasi_fd_table_get(pUVWasi->fds, appFD, &pFDWrap, 0, 0);
        if (err != 0)
            return err;
        if (cb)
            cb(pFDWrap);
        outHostFD = uv_get_osfhandle(pFDWrap->fd);
        uv_mutex_unlock(&pFDWrap->mutex);
        return 0;
    }

    uvwasi_errno_t Utility::ConvertErrnoToWasiErrno(int error) {
        if (error == 0)
            return 0;
        // Ref: wasm-micro-runtime/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/posix.c
        static const std::unordered_map<int, uvwasi_errno_t> errors {
#define X(v) {v, UVWASI_##v}
                X(E2BIG),
                X(EACCES),
                X(EADDRINUSE),
                X(EADDRNOTAVAIL),
                X(EAFNOSUPPORT),
                X(EAGAIN),
                X(EALREADY),
                X(EBADF),
                X(EBADMSG),
                X(EBUSY),
                X(ECANCELED),
                X(ECHILD),
                X(ECONNABORTED),
                X(ECONNREFUSED),
                X(ECONNRESET),
                X(EDEADLK),
                X(EDESTADDRREQ),
                X(EDOM),
                X(EDQUOT),
                X(EEXIST),
                X(EFAULT),
                X(EFBIG),
                X(EHOSTUNREACH),
                X(EIDRM),
                X(EILSEQ),
                X(EINPROGRESS),
                X(EINTR),
                X(EINVAL),
                X(EIO),
                X(EISCONN),
                X(EISDIR),
                X(ELOOP),
                X(EMFILE),
                X(EMLINK),
                X(EMSGSIZE),
                X(EMULTIHOP),
                X(ENAMETOOLONG),
                X(ENETDOWN),
                X(ENETRESET),
                X(ENETUNREACH),
                X(ENFILE),
                X(ENOBUFS),
                X(ENODEV),
                X(ENOENT),
                X(ENOEXEC),
                X(ENOLCK),
                X(ENOLINK),
                X(ENOMEM),
                X(ENOMSG),
                X(ENOPROTOOPT),
                X(ENOSPC),
                X(ENOSYS),
#ifdef ENOTCAPABLE
                X(ENOTCAPABLE),
#endif
                X(ENOTCONN),
                X(ENOTDIR),
                X(ENOTEMPTY),
                X(ENOTRECOVERABLE),
                X(ENOTSOCK),
                X(ENOTSUP),
                X(ENOTTY),
                X(ENXIO),
                X(EOVERFLOW),
                X(EOWNERDEAD),
                X(EPERM),
                X(EPIPE),
                X(EPROTO),
                X(EPROTONOSUPPORT),
                X(EPROTOTYPE),
                X(ERANGE),
                X(EROFS),
                X(ESPIPE),
                X(ESRCH),
                X(ESTALE),
                X(ETIMEDOUT),
                X(ETXTBSY),
                X(EXDEV),
#undef X
#if EOPNOTSUPP != ENOTSUP
                {EOPNOTSUPP, UVWASI_ENOTSUP},
#endif
#if EWOULDBLOCK != EAGAIN
                {EWOULDBLOCK, UVWASI_EAGAIN},
#endif
        };
        if (error < 0)
            return UVWASI_ENOSYS;
        const auto it = errors.find(error);
        if (it != errors.end())
            return it->second;
        return UVWASI_ENOSYS;
    }
}
