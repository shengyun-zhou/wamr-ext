#include "Utility.h"

namespace WAMR_EXT_NS {
    wasi_errno_t Utility::ConvertErrnoToWasiErrno(int error) {
        if (error == 0)
            return 0;
        // Ref: wasm-micro-runtime/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/posix.c
        static const std::unordered_map<int, wasi_errno_t> errors {
#define X(v) {v, __WASI_##v}
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
                {EOPNOTSUPP, __WASI_ENOTSUP},
#endif
#if EWOULDBLOCK != EAGAIN
                {EWOULDBLOCK, __WASI_EAGAIN},
#endif
        };
        if (error < 0)
            return __WASI_ENOSYS;
        const auto it = errors.find(error);
        if (it != errors.end())
            return it->second;
        return __WASI_ENOSYS;
    }
}
