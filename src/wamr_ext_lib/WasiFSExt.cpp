#include "WasiFSExt.h"
extern "C" {
#include <fd_table.h>
#include <uv_mapping.h>
}

namespace WAMR_EXT_NS {
    namespace wasi {
        struct wamr_statvfs : public wamr_wasi_struct_base {
            uint32_t f_bsize;
            uint64_t f_blocks;
            uint64_t f_bfree;
            uint64_t f_bavail;
        };
        wamr_wasi_struct_assert(wamr_statvfs);
    }

    void WasiFSExt::Init() {
        static NativeSymbol nativeSymbols[] = {
            {"fd_statvfs", (void*)FDStatVFS, "(i*)i", nullptr},
        };
        wasm_runtime_register_natives("fs_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    }

    int32_t WasiFSExt::FDStatVFS(wasm_exec_env_t pExecEnv, int32_t fd, wasi::wamr_wasi_struct_base* _pRetStatInfo) {
        wasm_module_inst_t pWasmModule = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, _pRetStatInfo, _pRetStatInfo->struct_size))
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
            wasi::wamr_statvfs* pRetStatInfo = static_cast<wasi::wamr_statvfs*>(_pRetStatInfo);
            pRetStatInfo->f_bsize = pUVRet->f_bsize;
            pRetStatInfo->f_blocks = pUVRet->f_blocks;
            pRetStatInfo->f_bfree = pUVRet->f_bfree;
            pRetStatInfo->f_bavail = pUVRet->f_bavail;
        }
        uv_mutex_unlock(&pFDWrap->mutex);
        uv_fs_req_cleanup(&req);
        return err;
    }
}