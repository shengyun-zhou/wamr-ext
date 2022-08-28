#pragma once
#include "../base/Utility.h"
#include "../base/WamrExtInternalDef.h"
extern "C" {
#include <fd_table.h>
#include <uv_mapping.h>
#include <wasi_rights.h>
}
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace WAMR_EXT_NS {
    namespace wasi {
        struct wamr_wasi_sockaddr_storage;
    }

    class WasiSocketExt {
    public:
        static void Init();
    private:
        static uvwasi_errno_t GetSysLastSocketError();
        static uvwasi_errno_t WasiAppSockAddrToHostSockAddr(const wasi::wamr_wasi_sockaddr_storage* pWasiAppSockAddr,
                                                            sockaddr_storage& hostSockAddr, socklen_t& outAddrLen);
        static void HostSockAddrToWasiAppSockAddr(const sockaddr* pHostSockAddr, wasi::wamr_wasi_sockaddr_storage* pWasiAppSockAddr);
        static void HostSockAddrToWasiAppSockAddr(const sockaddr_storage& hostSockAddr, wasi::wamr_wasi_sockaddr_storage* pWasiAppSockAddr) {
            HostSockAddrToWasiAppSockAddr((const sockaddr*)&hostSockAddr, pWasiAppSockAddr);
        }
        static uvwasi_errno_t GetHostSocketFD(wasm_module_inst_t pWasmModuleInst, int32_t appSockFD, uv_os_sock_t& outHostSockFD);
        static uvwasi_errno_t GetHostSocketFD(wasm_module_inst_t pWasmModuleInst, int32_t appSockFD, uv_os_sock_t& outHostSockFD, uvwasi_filetype_t& outWasiSockType);
        static uvwasi_errno_t InsertNewHostSocketFDToTable(wasm_module_inst_t pWasmModuleInst, uv_os_sock_t hostSockFD, uvwasi_filetype_t wasiSockType, int32_t& outAppSockFD);

        static int32_t SockOpen(wasm_exec_env_t pExecEnv, int32_t domain, int32_t type, int32_t protocol, int32_t* outAppSockFD);
        static int32_t SockBind(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppBindAddr);
        static int32_t SockConnect(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppConnectAddr);
        static int32_t SockListen(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t backlog);
        static int32_t SockAccept(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t* outNewAppSockFD, wasi::wamr_wasi_struct_base* _pAppSockAddr);
        static int32_t SockGetSockName(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppSockAddr);
        static int32_t SockGetPeerName(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppSockAddr);
        static int32_t SockShutdown(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appHow);
        static int32_t WasiPollOneOff(wasm_exec_env_t pExecEnv, const uvwasi_subscription_t* pAppSub,
                                      uvwasi_event_t *pAppOutEvent, uint32_t appSubCount, uint32_t *pAppNEvents);

        enum HostSockOptValType {
            UINT32,
            TIMEVAL,
            LINGER,
        };
        static uvwasi_errno_t MapWasiSockOpt(int32_t appLevel, int32_t appOptName, int& hostOptLevel, int& hostOptName, HostSockOptValType& hostOptValTypeName);
        static int32_t SockGetOpt(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appLevel, int32_t appOptName, void* appOptBuf, uint32_t* appOptBufLen);
        static int32_t SockSetOpt(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appLevel, int32_t appOptName, const void* appOptBuf, uint32_t appOptBufLen);

        static uint32_t MapWasiSockMsgFlags(uint32_t appSockMsgFlags);
        static int32_t SockRecvMsg(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppMsgHdr, wasi::wasi_iovec_t* pAppIOVec, uint32_t appIOVecCount);
        static int32_t SockSendMsg(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppMsgHdr, wasi::wasi_iovec_t* pAppIOVec, uint32_t appIOVecCount);
        static int32_t SockGetIfAddrs(wasm_exec_env_t pExecEnv, wasi::wamr_wasi_struct_base* _pAppIfAddrsReq);
    };
}
