#include "WasiSocketExt.h"
#include <uv.h>

namespace WAMR_EXT_NS {
    namespace wasi {
        struct alignas(8) wamr_wasi_sockaddr_storage : public wamr_wasi_struct_base {
            uint16_t family;
            union {
                struct {
                    uint8_t addr[4];
                    uint16_t port;      // Network byte order
                } inet;
                struct {
                    uint8_t addr[16];
                    uint16_t port;      // Network byte order
                    uint32_t flowinfo;
                    uint32_t scope_id;
                } inet6;
                uint8_t __padding[56];
            } u_addr;
        };
        wamr_wasi_struct_assert(wamr_wasi_sockaddr_storage);
        static_assert(sizeof(struct wamr_wasi_sockaddr_storage) == 64);

        struct wasi_linger_t {
            int32_t l_onoff;
            int32_t l_linger;
        };

        struct wamr_wasi_msghdr : public wamr_wasi_struct_base {
            struct wamr_wasi_sockaddr_storage addr;
            uint32_t input_flags;
            uint32_t ret_flags;
            uint64_t ret_data_size;
        };
        wamr_wasi_struct_assert(wamr_wasi_msghdr);
    }

    void WasiSocketExt::Init() {
        static NativeSymbol nativeSymbols[] = {
            {"sock_open", (void*)SockOpen, "(iii*)i", nullptr},
            {"sock_bind", (void*)SockBind, "(i*)i", nullptr},
            {"sock_connect", (void*)SockConnect, "(i*)i", nullptr},
            {"sock_listen", (void*)SockListen, "(ii)i", nullptr},
            {"sock_accept", (void*)SockAccept, "(i**)i", nullptr},
            {"sock_getsockname", (void*)SockGetSockName, "(i*)i", nullptr},
            {"sock_getpeername", (void*)SockGetPeerName, "(i*)i", nullptr},
            {"sock_shutdown", (void*)SockShutdown, "(ii)i", nullptr},
            {"sock_getopt", (void*)SockGetOpt, "(iii**)i", nullptr},
            {"sock_setopt", (void*)SockSetOpt, "(iii*i)i", nullptr},
            {"sock_recvmsg", (void*)SockRecvMsg, "(i**i)i", nullptr},
            {"sock_sendmsg", (void*)SockSendMsg, "(i**i)i", nullptr},
        };
        wasm_runtime_register_natives("socket_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    }

    uvwasi_errno_t WasiSocketExt::GetSysLastSocketError() {
        int err;
#ifndef _WIN32
        err = errno;
#else
#error "Converting socket error code to uvwasi_errno_t is not implemented"
#endif
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    uvwasi_errno_t WasiSocketExt::GetHostSocketFD(wasm_module_inst_t pWasmModuleInst, int32_t appSockFD, uv_os_sock_t &outHostSockFD) {
        uvwasi_filetype_t _wasiSockType;
        return GetHostSocketFD(pWasmModuleInst, appSockFD, outHostSockFD, _wasiSockType);
    }

    uvwasi_errno_t WasiSocketExt::GetHostSocketFD(wasm_module_inst_t pWasmModuleInst, int32_t appSockFD, uv_os_sock_t &outHostSockFD,
                                                  uvwasi_filetype_t& outWasiSockType) {
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(pWasmModuleInst);
        uvwasi_fd_wrap_t* pFDWrap = nullptr;
        uvwasi_errno_t err = uvwasi_fd_table_get(pUVWasi->fds, appSockFD, &pFDWrap, 0, 0);
        if (err != 0)
            return err;
        outWasiSockType = pFDWrap->type;
        outHostSockFD = (uv_os_sock_t)uv_get_osfhandle(pFDWrap->fd);
        uv_mutex_unlock(&pFDWrap->mutex);
        return 0;
    }

    uvwasi_errno_t WasiSocketExt::InsertNewHostSocketFDToTable(wasm_module_inst_t pWasmModuleInst, uv_os_sock_t hostSockFD, uvwasi_filetype_t wasiSockType,
                                                               int32_t &outAppSockFD) {
        // FIXME: socket may not be closed correctly on Windows host
        uv_file uvFD = uv_open_osfhandle((uv_os_fd_t)hostSockFD);
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(pWasmModuleInst);
        uvwasi_fd_wrap_t* pFDWrap = nullptr;
        uvwasi_errno_t err = uvwasi_fd_table_insert(pUVWasi, pUVWasi->fds, uvFD, "", "", wasiSockType,
                                                    UVWASI__RIGHTS_SOCKET_BASE, UVWASI__RIGHTS_ALL, 0, &pFDWrap);
        if (err != 0) {
            uv_fs_t req;
            uv_fs_close(nullptr, &req, uvFD, nullptr);
            uv_fs_req_cleanup(&req);
            return err;
        }
        outAppSockFD = pFDWrap->id;
        uv_mutex_unlock(&pFDWrap->mutex);
        return err;
    }

#define __WASI_AF_UNSPEC 0
#define __WASI_AF_INET 1
#define __WASI_AF_INET6 2
#define __WASI_IPPROTO_TCP 6
#define __WASI_IPPROTO_UDP 17
#define __WASI_SOCK_NONBLOCK (0x00004000)

    uvwasi_errno_t WasiSocketExt::WasiAppSockAddrToHostSockAddr(const wasi::wamr_wasi_sockaddr_storage *pWasiAppSockAddr,
                                                                sockaddr_storage &hostSockAddr, socklen_t& outAddrLen) {
        memset(&hostSockAddr, 0, sizeof(hostSockAddr));
        if (pWasiAppSockAddr->family == __WASI_AF_INET) {
            hostSockAddr.ss_family = AF_INET;
            auto* pHostSockAddr4 = (struct sockaddr_in*)&hostSockAddr;
            memcpy(&pHostSockAddr4->sin_addr.s_addr, pWasiAppSockAddr->u_addr.inet.addr, 4);
            pHostSockAddr4->sin_port = pWasiAppSockAddr->u_addr.inet.port;
            outAddrLen = sizeof(struct sockaddr_in);
        } else if (pWasiAppSockAddr->family == __WASI_AF_INET6) {
            hostSockAddr.ss_family = AF_INET6;
            auto* pHostSockAddr6 = (struct sockaddr_in6*)&hostSockAddr;
            memcpy(&pHostSockAddr6->sin6_addr.s6_addr, pWasiAppSockAddr->u_addr.inet6.addr, 16);
            pHostSockAddr6->sin6_port = pWasiAppSockAddr->u_addr.inet6.port;
            pHostSockAddr6->sin6_flowinfo = pWasiAppSockAddr->u_addr.inet6.flowinfo;
            pHostSockAddr6->sin6_scope_id = pWasiAppSockAddr->u_addr.inet6.scope_id;
            outAddrLen = sizeof(struct sockaddr_in6);
        } else {
            return UVWASI_EAFNOSUPPORT;
        }
        return 0;
    }

    void WasiSocketExt::HostSockAddrToWasiAppSockAddr(const sockaddr_storage &hostSockAddr,
                                                      wasi::wamr_wasi_sockaddr_storage *pWasiAppSockAddr) {
        if (hostSockAddr.ss_family == AF_INET) {
            pWasiAppSockAddr->family = __WASI_AF_INET;
            const struct sockaddr_in* pHostSockAddr4 = (struct sockaddr_in*)&hostSockAddr;
            memcpy(pWasiAppSockAddr->u_addr.inet.addr, &pHostSockAddr4->sin_addr.s_addr, 4);
            pWasiAppSockAddr->u_addr.inet.port = pHostSockAddr4->sin_port;
        } else if (hostSockAddr.ss_family == AF_INET6) {
            pWasiAppSockAddr->family = __WASI_AF_INET6;
            const struct sockaddr_in6* pHostSockAddr6 = (struct sockaddr_in6*)&hostSockAddr;
            memcpy(pWasiAppSockAddr->u_addr.inet6.addr, pHostSockAddr6->sin6_addr.s6_addr, 16);
            pWasiAppSockAddr->u_addr.inet6.port = pHostSockAddr6->sin6_port;
            pWasiAppSockAddr->u_addr.inet6.flowinfo = pHostSockAddr6->sin6_flowinfo;
            pWasiAppSockAddr->u_addr.inet6.scope_id = pHostSockAddr6->sin6_scope_id;
        } else {
            assert(false);
            pWasiAppSockAddr->family = __WASI_AF_UNSPEC;
        }
    }

    int32_t WasiSocketExt::SockOpen(wasm_exec_env_t pExecEnv, int32_t domain, int32_t type, int32_t protocol, int32_t *outAppSockFD) {
        if (!outAppSockFD)
            return UVWASI_EINVAL;

        if (domain == __WASI_AF_INET)
            domain = AF_INET;
        else if (domain == __WASI_AF_INET6)
            domain = AF_INET6;
        else
            return UVWASI_EAFNOSUPPORT;

        int32_t hostSockType = 0;
        uvwasi_filetype_t wasiSockType = 0;
        if ((type & UVWASI_FILETYPE_SOCKET_DGRAM) == UVWASI_FILETYPE_SOCKET_DGRAM) {
            hostSockType = SOCK_DGRAM;
            wasiSockType = UVWASI_FILETYPE_SOCKET_DGRAM;
        }
        else if ((type & UVWASI_FILETYPE_SOCKET_STREAM) == UVWASI_FILETYPE_SOCKET_STREAM) {
            hostSockType = SOCK_STREAM;
            wasiSockType = UVWASI_FILETYPE_SOCKET_STREAM;
        } else {
            return UVWASI_EINVAL;
        }

        int32 sockFlags = 0;
        if ((type & __WASI_SOCK_NONBLOCK) == __WASI_SOCK_NONBLOCK)
            sockFlags |= __WASI_SOCK_NONBLOCK;

        if (hostSockType == SOCK_DGRAM) {
            if (protocol == 0)
                protocol = __WASI_IPPROTO_UDP;
            if (protocol == __WASI_IPPROTO_UDP)
                protocol = IPPROTO_UDP;
            else
                return UVWASI_EPROTONOSUPPORT;
        } else if (hostSockType == SOCK_STREAM) {
            if (protocol == 0)
                protocol = __WASI_IPPROTO_TCP;
            if (protocol == __WASI_IPPROTO_TCP)
                protocol = IPPROTO_TCP;
            else
                return UVWASI_EPROTONOSUPPORT;
        }
        uv_os_sock_t newHostSockFD = socket(domain, hostSockType, protocol);
        if (newHostSockFD == INVALID_SOCKET)
            return GetSysLastSocketError();
        if (sockFlags & __WASI_SOCK_NONBLOCK) {
#ifdef _WIN32
            u_long nbio = 1;
            ioctlsocket(newHostSockFD, FIONBIO, &nbio);
#else
            int tempFlags = fcntl(newHostSockFD, F_GETFL, 0);
            if (tempFlags != -1)
                fcntl(newHostSockFD, F_SETFL, tempFlags | O_NONBLOCK);
#endif
        }
        return InsertNewHostSocketFDToTable(get_module_inst(pExecEnv), newHostSockFD, wasiSockType, *outAppSockFD);
    }

    int32_t WasiSocketExt::SockBind(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base *_pAppBindAddr) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppBindAddr, _pAppBindAddr->struct_size))
            return UVWASI_EFAULT;
        sockaddr_storage hostBindAddr;
        socklen_t hostAddrLen = 0;
        wasi::wamr_wasi_sockaddr_storage* pAppSockAddr = static_cast<wasi::wamr_wasi_sockaddr_storage *>(_pAppBindAddr);
        uvwasi_errno_t err = WasiAppSockAddrToHostSockAddr(pAppSockAddr, hostBindAddr, hostAddrLen);
        if (err != 0)
            return err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        if (::bind(hostSockFD, (sockaddr*)&hostBindAddr, hostAddrLen) != 0)
            err = GetSysLastSocketError();
        return err;
    }

    int32_t WasiSocketExt::SockConnect(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base* _pAppConnectAddr) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppConnectAddr, _pAppConnectAddr->struct_size))
            return UVWASI_EFAULT;
        sockaddr_storage hostConnAddr;
        socklen_t hostAddrLen = 0;
        wasi::wamr_wasi_sockaddr_storage* pAppSockAddr = static_cast<wasi::wamr_wasi_sockaddr_storage*>(_pAppConnectAddr);
        uvwasi_errno_t err = WasiAppSockAddrToHostSockAddr(pAppSockAddr, hostConnAddr, hostAddrLen);
        if (err != 0)
            return err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        if (connect(hostSockFD, (sockaddr*)&hostConnAddr, hostAddrLen) != 0)
            err = GetSysLastSocketError();
        return err;
    }

    int32_t WasiSocketExt::SockListen(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t backlog) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        uvwasi_errno_t err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        if (listen(hostSockFD, backlog) != 0)
            err = GetSysLastSocketError();
        return err;
    }

    int32_t WasiSocketExt::SockAccept(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t *outNewAppSockFD,
                                      wasi::wamr_wasi_struct_base *_pAppSockAddr) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppSockAddr, _pAppSockAddr->struct_size))
            return UVWASI_EFAULT;
        uvwasi_errno_t err;
        uv_os_sock_t hostSockFD;
        uvwasi_filetype_t appWasiSockType;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD, appWasiSockType)) != 0)
            return err;
        sockaddr_storage hostSockAddr;
        socklen_t hostAddrLen = sizeof(hostSockAddr);
        uv_os_sock_t newHostSockFD = accept(hostSockFD, (sockaddr*)&hostSockAddr, &hostAddrLen);
        if (newHostSockFD == INVALID_SOCKET) {
            err = GetSysLastSocketError();
        } else {
            err = InsertNewHostSocketFDToTable(pWasmModuleInst, newHostSockFD, appWasiSockType, *outNewAppSockFD);
            if (err == 0)
                HostSockAddrToWasiAppSockAddr(hostSockAddr, static_cast<wasi::wamr_wasi_sockaddr_storage*>(_pAppSockAddr));
        }
        return err;
    }

    int32_t WasiSocketExt::SockGetSockName(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base *_pAppSockAddr) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppSockAddr, _pAppSockAddr->struct_size))
            return UVWASI_EFAULT;
        uvwasi_errno_t err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        sockaddr_storage hostSockAddr;
        socklen_t hostAddrLen = sizeof(hostSockAddr);
        if (getsockname(hostSockFD, (sockaddr*)&hostSockAddr, &hostAddrLen) != 0)
            err = GetSysLastSocketError();
        else
            HostSockAddrToWasiAppSockAddr(hostSockAddr, static_cast<wasi::wamr_wasi_sockaddr_storage*>(_pAppSockAddr));
        return err;
    }

    int32_t WasiSocketExt::SockGetPeerName(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base *_pAppSockAddr) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppSockAddr, _pAppSockAddr->struct_size))
            return UVWASI_EFAULT;
        uvwasi_errno_t err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        sockaddr_storage hostSockAddr;
        socklen_t hostAddrLen = sizeof(hostSockAddr);
        if (getpeername(hostSockFD, (sockaddr*)&hostSockAddr, &hostAddrLen) != 0)
            err = GetSysLastSocketError();
        else
            HostSockAddrToWasiAppSockAddr(hostSockAddr, static_cast<wasi::wamr_wasi_sockaddr_storage*>(_pAppSockAddr));
        return err;
    }

#ifndef _WIN32
#define HOST_SHUTDOWN_RD SHUT_RD
#define HOST_SHUTDOWN_WR SHUT_WR
#define HOST_SHUTDOWN_RDWR SHUT_RDWR
#else
#define HOST_SHUTDOWN_RD SD_RECEIVE
#define HOST_SHUTDOWN_WR SD_SEND
#define HOST_SHUTDOWN_RDWR SD_BOTH
#endif

    int32_t WasiSocketExt::SockShutdown(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appHow) {
        int hostShutdownHow = 0;
        if (appHow == UVWASI_SHUT_RD)
            hostShutdownHow = HOST_SHUTDOWN_RD;
        else if (appHow == UVWASI_SHUT_WR)
            hostShutdownHow = HOST_SHUTDOWN_WR;
        else if (appHow == (UVWASI_SHUT_RD | UVWASI_SHUT_WR))
            hostShutdownHow = HOST_SHUTDOWN_RDWR;
        else
            return UVWASI_EINVAL;
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        uvwasi_errno_t err;
        uv_os_sock_t hostSockFD;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
            return err;
        if (shutdown(hostSockFD, hostShutdownHow) != 0)
            err = GetSysLastSocketError();
        return err;
    }

#define __WASI_SOL_SOCKET 0x7fffffff
#define __WASI_SOL_IP 0
#define __WASI_SOL_IPV6 41

#define __WASI_SO_REUSEADDR    2
#define __WASI_SO_TYPE         3
#define __WASI_SO_ERROR        4
#define __WASI_SO_DONTROUTE    5
#define __WASI_SO_BROADCAST    6
#define __WASI_SO_SNDBUF       7
#define __WASI_SO_RCVBUF       8
#define __WASI_SO_KEEPALIVE    9
#define __WASI_SO_LINGER       13
#define __WASI_SO_RCVTIMEO     20
#define __WASI_SO_SNDTIMEO     21

#define __WASI_TCP_NODELAY     1

    uvwasi_errno_t WasiSocketExt::MapWasiSockOpt(int32_t appLevel, int32_t appOptName, int &hostOptLevel, int &hostOptName,
                                                 HostSockOptValType& hostOptValType) {
        hostOptValType = UINT32;
        if (appLevel == __WASI_SOL_SOCKET) {
            hostOptLevel = SOL_SOCKET;
            switch (appOptName) {
                case __WASI_SO_REUSEADDR:
                    hostOptName = SO_REUSEADDR;
                    break;
                case __WASI_SO_TYPE:
                    hostOptName = SO_TYPE;
                    break;
                case __WASI_SO_ERROR:
                    hostOptName = SO_ERROR;
                    break;
                case __WASI_SO_DONTROUTE:
                    hostOptName = SO_DONTROUTE;
                    break;
                case __WASI_SO_BROADCAST:
                    hostOptName = SO_BROADCAST;
                    break;
                case __WASI_SO_SNDBUF:
                    hostOptName = SO_SNDBUF;
                    break;
                case __WASI_SO_RCVBUF:
                    hostOptName = SO_RCVBUF;
                    break;
                case __WASI_SO_KEEPALIVE:
                    hostOptName = SO_KEEPALIVE;
                    break;
                case __WASI_SO_LINGER:
                    hostOptName = SO_LINGER;
                    hostOptValType = LINGER;
                    break;
                case __WASI_SO_RCVTIMEO:
                    hostOptName = SO_RCVTIMEO;
                    hostOptValType = TIMEVAL;
                    break;
                case __WASI_SO_SNDTIMEO:
                    hostOptName = SO_SNDTIMEO;
                    hostOptValType = TIMEVAL;
                    break;
                default:
                    return UVWASI_EINVAL;
            }
        } else if (appLevel == __WASI_IPPROTO_TCP) {
            hostOptLevel = SOL_TCP;
            switch (appOptName) {
                case __WASI_TCP_NODELAY:
                    hostOptName = TCP_NODELAY;
                    break;
                default:
                    return UVWASI_EINVAL;
            }
        } else {
            return UVWASI_ENOPROTOOPT;
        }
        return 0;
    }

    int32_t WasiSocketExt::SockGetOpt(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appLevel, int32_t appOptName,
                                      void *appOptBuf, uint32_t *appOptBufLen) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, appOptBuf, *appOptBufLen))
            return UVWASI_EFAULT;

        int32_t hostOptLevel = 0;
        int32_t hostOptName = 0;
        HostSockOptValType hostOptType;
        uvwasi_errno_t err = MapWasiSockOpt(appLevel, appOptName, hostOptLevel, hostOptName, hostOptType);
        if (err != 0)
            return err;
        union {
            uint32_t intVal;
            timeval timeval;
            linger lingerVal;
        } hostSockOptVal;
        socklen_t hostOptLen = sizeof(hostSockOptVal);
        uv_os_sock_t hostSockFD;
        uvwasi_filetype_t wasiSockType;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD, wasiSockType)) != 0)
            return err;

        if (hostOptLevel == SOL_SOCKET && hostOptName == SO_TYPE) {
            hostSockOptVal.intVal = wasiSockType;
        } else {
            if (getsockopt(hostSockFD, hostOptLevel, hostOptName, &hostSockOptVal, &hostOptLen) == -1)
                err = GetSysLastSocketError();
        }
        if (err == 0) {
            if (hostOptType == TIMEVAL) {
                wasi::wasi_timeval_t tempAppTimeval;
                tempAppTimeval.tv_sec = hostSockOptVal.timeval.tv_sec;
                tempAppTimeval.tv_usec = hostSockOptVal.timeval.tv_usec;
                if (*appOptBufLen > sizeof(tempAppTimeval))
                    *appOptBufLen = sizeof(tempAppTimeval);
                memcpy(appOptBuf, &tempAppTimeval, *appOptBufLen);
            } else if (hostOptType == LINGER) {
                wasi::wasi_linger_t tempAppLinger;
                tempAppLinger.l_linger = hostSockOptVal.lingerVal.l_linger;
                tempAppLinger.l_onoff = hostSockOptVal.lingerVal.l_onoff;
                if (*appOptBufLen > sizeof(tempAppLinger))
                    *appOptBufLen = sizeof(tempAppLinger);
                memcpy(appOptBuf, &tempAppLinger, *appOptBufLen);
            } else {
                if (*appOptBufLen > sizeof(hostSockOptVal.intVal))
                    *appOptBufLen = sizeof(hostSockOptVal.intVal);
                memcpy(appOptBuf, &hostSockOptVal.intVal, *appOptBufLen);
            }
        }
        return err;
    }

    int32_t WasiSocketExt::SockSetOpt(wasm_exec_env_t pExecEnv, int32_t appSockFD, int32_t appLevel, int32_t appOptName,
                                      const void *appOptBuf, uint32_t appOptBufLen) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, (void*)appOptBuf, appOptBufLen))
            return UVWASI_EFAULT;
        int32_t hostOptLevel = 0;
        int32_t hostOptName = 0;
        HostSockOptValType hostOptType;
        uvwasi_errno_t err = MapWasiSockOpt(appLevel, appOptName, hostOptLevel, hostOptName, hostOptType);
        if (err != 0)
            return err;
        union {
            uint32_t intVal;
            timeval timeval;
            linger lingerVal;
        } hostSockOptVal;
        socklen_t hostSockOptLen = sizeof(hostSockOptVal);
        do {
            if (hostOptType == UINT32) {
                if (appOptBufLen < sizeof(uint32_t)) {
                    err = UVWASI_EINVAL;
                    break;
                }
                hostSockOptLen = sizeof(hostSockOptVal.intVal);
                memcpy(&hostSockOptVal.intVal, appOptBuf, sizeof(hostSockOptVal.intVal));
            } else if (hostOptType == TIMEVAL) {
                if (appOptBufLen < sizeof(wasi::wasi_timeval_t)) {
                    err = UVWASI_EINVAL;
                    break;
                }
                hostSockOptLen = sizeof(hostSockOptVal.timeval);
                wasi::wasi_timeval_t* pAppTimeval = (wasi::wasi_timeval_t*)appOptBuf;
                hostSockOptVal.timeval.tv_sec = pAppTimeval->tv_sec;
                hostSockOptVal.timeval.tv_usec = pAppTimeval->tv_usec;
            } else if (hostOptType == LINGER) {
                if (appOptBufLen < sizeof(wasi::wasi_linger_t)) {
                    err = UVWASI_EINVAL;
                    break;
                }
                hostSockOptLen = sizeof(hostSockOptVal.lingerVal);
                wasi::wasi_linger_t* pAppLinger = (wasi::wasi_linger_t*)appOptBuf;
                hostSockOptVal.lingerVal.l_linger = pAppLinger->l_linger;
                hostSockOptVal.lingerVal.l_onoff = pAppLinger->l_onoff;
            }
            uv_os_sock_t hostSockFD;
            if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD)) != 0)
                break;
            if (setsockopt(hostSockFD, hostOptLevel, hostOptName, &hostSockOptVal, hostSockOptLen) == -1) {
                err = GetSysLastSocketError();
                break;
            }
        } while (false);
        return err;
    }

    uint32_t WasiSocketExt::MapWasiSockMsgFlags(uint32_t appSockMsgFlags) {
        uint32_t hostSockFlags = 0;
        if (appSockMsgFlags & UVWASI_SOCK_RECV_PEEK)
            hostSockFlags |= MSG_PEEK;
        if (appSockMsgFlags & UVWASI_SOCK_RECV_WAITALL)
            hostSockFlags |= MSG_WAITALL;
        return hostSockFlags;
    }

    int32_t WasiSocketExt::SockRecvMsg(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base *_pAppMsgHdr,
                                       wasi::wasi_iovec_t* pAppIOVec, uint32_t appIOVecCount) {
        if (appIOVecCount <= 0)
            return UVWASI_EINVAL;
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppMsgHdr, _pAppMsgHdr->struct_size) ||
            !wasm_runtime_validate_native_addr(pWasmModuleInst, pAppIOVec, sizeof(*pAppIOVec) * appIOVecCount)) {
            return UVWASI_EFAULT;
        }
        for (uint32_t i = 0; i < appIOVecCount; i++) {
            if (!wasm_runtime_validate_app_addr(pWasmModuleInst, pAppIOVec[i].app_buf_offset, pAppIOVec[i].buf_len))
                return UVWASI_EFAULT;
        }
        wasi::wamr_wasi_msghdr* pAppMsgHdr = static_cast<wasi::wamr_wasi_msghdr*>(_pAppMsgHdr);

        const uint32_t hostSockMsgFlags = MapWasiSockMsgFlags(pAppMsgHdr->input_flags);
        uv_os_sock_t hostSockFD;
        uvwasi_filetype_t wasiSockType;
        uvwasi_errno_t err;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD, wasiSockType)) != 0)
            return err;
        sockaddr_storage hostSockAddr;
        hostSockAddr.ss_family = AF_UNSPEC;
#ifndef _WIN32
        std::shared_ptr<iovec> pHostIOVec(new iovec[appIOVecCount], std::default_delete<iovec[]>());
        for (uint32_t i = 0; i < appIOVecCount; i++) {
            pHostIOVec.get()[i].iov_base = wasm_runtime_addr_app_to_native(pWasmModuleInst, pAppIOVec[i].app_buf_offset);
            pHostIOVec.get()[i].iov_len = pAppIOVec[i].buf_len;
        }

        msghdr hostMsgHdr;
        memset(&hostMsgHdr, 0, sizeof(hostMsgHdr));
        hostMsgHdr.msg_name = &hostSockAddr;
        hostMsgHdr.msg_namelen = sizeof(hostSockAddr);
        hostMsgHdr.msg_iov = pHostIOVec.get();
        hostMsgHdr.msg_iovlen = appIOVecCount;
        ssize_t ret = recvmsg(hostSockFD, &hostMsgHdr, hostSockMsgFlags);
        if (ret == -1) {
            err = GetSysLastSocketError();
        } else {
            pAppMsgHdr->ret_data_size = ret;
            pAppMsgHdr->ret_flags = 0;
            // TODO: Map host flags to WASI flags after the missing macro __WASI_RIFLAGS_RECV_DATA_TRUNCATED is fixed in wasi-libc
            if (hostSockAddr.ss_family != AF_UNSPEC)
                HostSockAddrToWasiAppSockAddr(hostSockAddr, &pAppMsgHdr->addr);
        }
#else
#error "Receiving data is not implemented for Win32"
#endif
        return err;
    }

    int32_t WasiSocketExt::SockSendMsg(wasm_exec_env_t pExecEnv, int32_t appSockFD, wasi::wamr_wasi_struct_base *_pAppMsgHdr,
                                       wasi::wasi_iovec_t *pAppIOVec, uint32_t appIOVecCount) {
        if (appIOVecCount <= 0)
            return UVWASI_EINVAL;
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppMsgHdr, _pAppMsgHdr->struct_size) ||
            !wasm_runtime_validate_native_addr(pWasmModuleInst, pAppIOVec, sizeof(*pAppIOVec) * appIOVecCount)) {
            return UVWASI_EFAULT;
        }
        for (uint32_t i = 0; i < appIOVecCount; i++) {
            if (!wasm_runtime_validate_app_addr(pWasmModuleInst, pAppIOVec[i].app_buf_offset, pAppIOVec[i].buf_len))
                return UVWASI_EFAULT;
        }
        wasi::wamr_wasi_msghdr* pAppMsgHdr = static_cast<wasi::wamr_wasi_msghdr*>(_pAppMsgHdr);

        const uint32_t hostSockMsgFlags = MapWasiSockMsgFlags(pAppMsgHdr->input_flags);
        uv_os_sock_t hostSockFD;
        uvwasi_filetype_t wasiSockType;
        uvwasi_errno_t err;
        if ((err = GetHostSocketFD(pWasmModuleInst, appSockFD, hostSockFD, wasiSockType)) != 0)
            return err;
        sockaddr_storage hostSockAddr;
        hostSockAddr.ss_family = AF_UNSPEC;
        socklen_t hostSockAddrLen = 0;
        if (pAppMsgHdr->addr.family != __WASI_AF_UNSPEC) {
            if ((err = WasiAppSockAddrToHostSockAddr(&pAppMsgHdr->addr, hostSockAddr, hostSockAddrLen)) != 0)
                return err;
        }
#ifndef _WIN32
        std::shared_ptr<iovec> pHostIOVec(new iovec[appIOVecCount], std::default_delete<iovec[]>());
        for (uint32_t i = 0; i < appIOVecCount; i++) {
            pHostIOVec.get()[i].iov_base = wasm_runtime_addr_app_to_native(pWasmModuleInst, pAppIOVec[i].app_buf_offset);
            pHostIOVec.get()[i].iov_len = pAppIOVec[i].buf_len;
        }

        msghdr hostMsgHdr;
        memset(&hostMsgHdr, 0, sizeof(hostMsgHdr));
        if (pAppMsgHdr->addr.family != __WASI_AF_UNSPEC) {
            hostMsgHdr.msg_name = &hostSockAddr;
            hostMsgHdr.msg_namelen = hostSockAddrLen;
        }
        hostMsgHdr.msg_iov = pHostIOVec.get();
        hostMsgHdr.msg_iovlen = appIOVecCount;
        ssize_t ret = sendmsg(hostSockFD, &hostMsgHdr, hostSockMsgFlags);
        if (ret == -1) {
            err = GetSysLastSocketError();
        } else {
            pAppMsgHdr->ret_data_size = ret;
        }
#else
#error "Sending data is not implemented for Win32"
#endif
        return err;
    }
}
