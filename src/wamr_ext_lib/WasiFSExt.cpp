#include "WasiFSExt.h"
#include "WamrExtInternalDef.h"
extern "C" {
#include <uv_mapping.h>
}
#ifndef _WIN32
#include <fcntl.h>
#ifdef __linux__
typedef struct flock64 host_flock_t;
#define HOST_F_SETLK  F_SETLK64
#define HOST_F_SETLKW F_SETLKW64
#define HOST_F_GETLK  F_GETLK64
#else
typedef struct flock host_flock_t;
#define HOST_F_SETLK  F_SETLK
#define HOST_F_SETLKW F_SETLKW
#define HOST_F_GETLK  F_GETLK
#endif
#endif

namespace WAMR_EXT_NS {
    namespace wasi {
        struct wamr_statvfs {
            uint32_t f_bsize;
            uint64_t f_blocks;
            uint64_t f_bfree;
            uint64_t f_bavail;
        };
        static_assert(std::is_trivial<wamr_statvfs>::value);

        struct wamr_fcntl_generic {
            int32_t cmd;
            int32_t ret_value;
        };
        static_assert(std::is_trivial<wamr_fcntl_generic>::value);

        struct wamr_fcntl_flock : public wamr_fcntl_generic {
            int16_t l_type;
            int16_t l_whence;
            int64_t l_start;
            int64_t l_len;
        };

        static_assert(std::is_trivial<wamr_fcntl_flock>::value);
    }

    void WasiFSExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_FD_STATVFS, std::make_shared<ExtSyscall_U32_P>((void*)FDStatVFS));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_FD_EXT_FCNTL, std::make_shared<ExtSyscall_U32_P>((void*)FDFcntl));
    }

    int32_t WasiFSExt::FDStatVFS(wasm_exec_env_t pExecEnv, int32_t fd, void* _pAppRetStatInfo) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppRetStatInfo, sizeof(wasi::wamr_statvfs)))
            return UVWASI_EFAULT;
        std::string path;
        uv_os_fd_t _osfd;
        auto err = Utility::GetHostFDByAppFD(pWasmModuleInst, fd, _osfd, [&path](const uvwasi_fd_wrap_t* pFDWrap) {
            path = pFDWrap->real_path;
        });
        if (err != 0)
            return err;
        uv_fs_t req;
        int uvErr = uv_fs_statfs(nullptr, &req, path.c_str(), nullptr);
        if (uvErr != 0) {
            err = uvwasi__translate_uv_error(uvErr);
        } else {
            uv_statfs_t* pUVRet = (uv_statfs_t*)req.ptr;
            wasi::wamr_statvfs* pAppRetStatInfo = static_cast<wasi::wamr_statvfs*>(_pAppRetStatInfo);
            pAppRetStatInfo->f_bsize = pUVRet->f_bsize;
            pAppRetStatInfo->f_blocks = pUVRet->f_blocks;
            pAppRetStatInfo->f_bfree = pUVRet->f_bfree;
            pAppRetStatInfo->f_bavail = pUVRet->f_bavail;
        }
        uv_fs_req_cleanup(&req);
        return err;
    }

#define __WASI_F_GETLK ((1 << 8) | 1)
#define __WASI_F_SETLK ((1 << 8) | 2)
#define __WASI_F_SETLKW ((1 << 8) | 3)

#define __WASI_F_RDLCK 0
#define __WASI_F_WRLCK 1
#define __WASI_F_UNLCK 2

    int32_t WasiFSExt::FDFcntl(wasm_exec_env_t pExecEnv, int32_t fd, void* _pAppFcntlInfo) {
        wasm_module_inst_t pWasmModule = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, _pAppFcntlInfo, sizeof(wasi::wamr_fcntl_generic)))
            return UVWASI_EFAULT;
        uv_os_fd_t osfd;
        auto err = Utility::GetHostFDByAppFD(pWasmModule, fd, osfd);
        if (err != 0)
            return err;
        wasi::wamr_fcntl_generic* pAppFcntlGeneric = static_cast<wasi::wamr_fcntl_generic*>(_pAppFcntlInfo);
        pAppFcntlGeneric->ret_value = 0;
        switch (pAppFcntlGeneric->cmd) {
            case __WASI_F_GETLK:
            case __WASI_F_SETLK:
            case __WASI_F_SETLKW: {
                if (!wasm_runtime_validate_native_addr(pWasmModule, _pAppFcntlInfo, sizeof(wasi::wamr_fcntl_flock))) {
                    err = UVWASI_EFAULT;
                    break;
                }
                wasi::wamr_fcntl_flock* pAppFLockCtrl = static_cast<wasi::wamr_fcntl_flock*>(pAppFcntlGeneric);
#ifndef _WIN32
                host_flock_t hostFlock;
                memset(&hostFlock, 0, sizeof(hostFlock));
                hostFlock.l_start = pAppFLockCtrl->l_start;
                hostFlock.l_len = pAppFLockCtrl->l_len;
                if (pAppFLockCtrl->l_type == __WASI_F_RDLCK) {
                    hostFlock.l_type = F_RDLCK;
                } else if (pAppFLockCtrl->l_type == __WASI_F_WRLCK) {
                    hostFlock.l_type = F_WRLCK;
                } else if (pAppFLockCtrl->l_type == __WASI_F_UNLCK) {
                    hostFlock.l_type = F_UNLCK;
                } else {
                    err = UVWASI_EINVAL;
                    break;
                }
                if (pAppFLockCtrl->l_whence == UVWASI_WHENCE_SET) {
                    hostFlock.l_whence = SEEK_SET;
                } else if (pAppFLockCtrl->l_whence == UVWASI_WHENCE_CUR) {
                    hostFlock.l_whence = SEEK_CUR;
                } else if (pAppFLockCtrl->l_whence == UVWASI_WHENCE_END) {
                    hostFlock.l_whence = SEEK_END;
                } else {
                    err = UVWASI_EINVAL;
                    break;
                }
                int hostCmd;
                switch (pAppFLockCtrl->cmd) {
                    case __WASI_F_GETLK:
                        hostCmd = HOST_F_GETLK;
                        break;
                    case __WASI_F_SETLK:
                        hostCmd = HOST_F_SETLK;
                        break;
                    case __WASI_F_SETLKW:
                        hostCmd = HOST_F_SETLKW;
                        break;
                }
                if ((pAppFLockCtrl->ret_value = fcntl(osfd, hostCmd, &hostFlock)) == -1) {
                    err = Utility::ConvertErrnoToWasiErrno(errno);
                    break;
                }
                if (pAppFLockCtrl->cmd == __WASI_F_GETLK) {
                    pAppFLockCtrl->l_start = hostFlock.l_start;
                    pAppFLockCtrl->l_len = hostFlock.l_len;
                    switch (hostFlock.l_whence) {
                        case SEEK_SET:
                            pAppFLockCtrl->l_whence = UVWASI_WHENCE_SET;
                            break;
                        case SEEK_CUR:
                            pAppFLockCtrl->l_whence = UVWASI_WHENCE_CUR;
                            break;
                        case SEEK_END:
                            pAppFLockCtrl->l_whence = UVWASI_WHENCE_END;
                            break;
                        default:
                            pAppFLockCtrl->l_whence = hostFlock.l_whence;
                            break;
                    }
                    switch (hostFlock.l_type) {
                        case F_RDLCK:
                            pAppFLockCtrl->l_type = __WASI_F_RDLCK;
                            break;
                        case F_WRLCK:
                            pAppFLockCtrl->l_type = __WASI_F_WRLCK;
                            break;
                        case F_UNLCK:
                            pAppFLockCtrl->l_type = __WASI_F_UNLCK;
                            break;
                        default:
                            pAppFLockCtrl->l_type = hostFlock.l_type;
                            break;
                    }
                }
#else
#error "fcntl() for file locking is not implemented for win32"
#endif
                break;
            }
            default:
                err = UVWASI_EINVAL;
                break;
        }
        return err;
    }
}