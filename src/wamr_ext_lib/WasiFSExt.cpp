#include "WasiFSExt.h"
#include "../base/Utility.h"
extern "C" {
#include <fd_table.h>
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
        struct wamr_statvfs : public wamr_wasi_struct_base {
            uint32_t f_bsize;
            uint64_t f_blocks;
            uint64_t f_bfree;
            uint64_t f_bavail;
        };
        wamr_wasi_struct_assert(wamr_statvfs);

        struct wamr_fcntl_generic : public wamr_wasi_struct_base {
            int32_t cmd;
            int32_t ret_value;
        };
        wamr_wasi_struct_assert(wamr_fcntl_generic);

        struct wamr_fcntl_flock : public wamr_fcntl_generic {
            int16_t l_type;
            int16_t l_whence;
            int64_t l_start;
            int64_t l_len;
        };

        wamr_wasi_struct_assert(wamr_fcntl_flock);
    }

    void WasiFSExt::Init() {
        static NativeSymbol nativeSymbols[] = {
            {"fd_statvfs", (void*)FDStatVFS, "(i*)i", nullptr},
            {"fd_fcntl", (void*)FDFcntl, "(i*)i", nullptr},
        };
        wasm_runtime_register_natives("fs_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    }

    int32_t WasiFSExt::FDStatVFS(wasm_exec_env_t pExecEnv, int32_t fd, wasi::wamr_wasi_struct_base* _pAppRetStatInfo) {
        wasm_module_inst_t pWasmModule = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, _pAppRetStatInfo, _pAppRetStatInfo->struct_size))
            return UVWASI_EFAULT;
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(pWasmModule);
        uvwasi_fd_wrap_t* pFDWrap = nullptr;
        uvwasi_errno_t err = uvwasi_fd_table_get(pUVWasi->fds, fd, &pFDWrap, 0, 0);
        if (err != 0)
            return err;
        uv_fs_t req;
        int uvErr = uv_fs_statfs(nullptr, &req, pFDWrap->real_path, nullptr);
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
        uv_mutex_unlock(&pFDWrap->mutex);
        uv_fs_req_cleanup(&req);
        return err;
    }

#define __WASI_F_GETLK ((1 << 8) | 1)
#define __WASI_F_SETLK ((1 << 8) | 2)
#define __WASI_F_SETLKW ((1 << 8) | 3)

#define __WASI_F_RDLCK 0
#define __WASI_F_WRLCK 1
#define __WASI_F_UNLCK 2

    int32_t WasiFSExt::FDFcntl(wasm_exec_env_t pExecEnv, int32_t fd, wasi::wamr_wasi_struct_base *_pAppFcntlInfo) {
        wasm_module_inst_t pWasmModule = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, _pAppFcntlInfo, _pAppFcntlInfo->struct_size))
            return UVWASI_EFAULT;
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(pWasmModule);
        uvwasi_fd_wrap_t* pFDWrap = nullptr;
        uvwasi_errno_t err = uvwasi_fd_table_get(pUVWasi->fds, fd, &pFDWrap, 0, 0);
        if (err != 0)
            return err;
        uv_os_fd_t osfd = uv_get_osfhandle(pFDWrap->fd);
        wasi::wamr_fcntl_generic* pAppFcntlGeneric = static_cast<wasi::wamr_fcntl_generic*>(_pAppFcntlInfo);
        pAppFcntlGeneric->ret_value = 0;
        switch (pAppFcntlGeneric->cmd) {
            case __WASI_F_GETLK:
            case __WASI_F_SETLK:
            case __WASI_F_SETLKW: {
                if (pAppFcntlGeneric->struct_size < sizeof(wasi::wamr_fcntl_flock)) {
                    err = UVWASI_ERANGE;
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
        uv_mutex_unlock(&pFDWrap->mutex);
        return err;
    }
}