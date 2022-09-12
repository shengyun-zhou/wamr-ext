#pragma once
#include "../base/BaseDef.h"
#include "WasiPthreadExt.h"
#include "WasiProcessExt.h"
#include "wamr_ext_api.h"

struct WamrExtInstanceConfig {
    uint8_t maxThreadNum{4};
    std::map<std::string, std::string> preOpenDirs;     // mapped dir -> host dir
    std::map<std::string, std::string> envVars;
    std::map<std::string, std::string> hostCmdWhitelist;
    std::vector<std::string> args;
    WamrExtInstanceExceptionCB exceptionCB{.func = nullptr};

    WamrExtInstanceConfig();
};

struct WamrExtModule {
    std::shared_ptr<uint8_t> pModuleBuf;
    wasm_module_t wasmModule{nullptr};
    std::string moduleName;
    WamrExtInstanceConfig instDefaultConf;

    explicit WamrExtModule(const std::shared_ptr<uint8_t>& pBuf, wasm_module_t _wasmModule, const char* name) :
        pModuleBuf(pBuf), wasmModule(_wasmModule), moduleName(name) {}
    WamrExtModule(const WamrExtModule&) = delete;
    WamrExtModule& operator=(const WamrExtModule&) = delete;
};

struct WamrExtInstance {
    wamr_ext_instance_t* pUserCallbackPointer;
    std::mutex instanceLock;
    enum {
        STATE_NEW,
        STATE_STARTED,
        STATE_ENDED,
        STATE_DESTROYING,
        STATE_DESTROYED,
    } state{STATE_NEW};
    WamrExtModule* pMainModule;
    WamrExtInstanceConfig config;
    wasm_module_inst_t wasmMainInstance{nullptr};
    std::mutex execFuncLock;
    std::unordered_map<std::string, wasm_exec_env_t> wasmExecEnvMap;
    WAMR_EXT_NS::WasiPthreadExt::InstancePthreadManager wasiPthreadManager;
    WAMR_EXT_NS::WasiProcessExt::ProcManager wasiProcessManager;

    explicit WamrExtInstance(WamrExtModule* _pModule, wamr_ext_instance_t* _pUserCallbackPointer) :
        pMainModule(_pModule), config(_pModule->instDefaultConf), pUserCallbackPointer(_pUserCallbackPointer) {}
    WamrExtInstance(const WamrExtInstance&) = delete;
    WamrExtInstance& operator=(const WamrExtInstance&) = delete;
};

namespace WAMR_EXT_NS {
    extern thread_local char gLastErrorStr[200];

    namespace wasi {
        union wamr_ext_syscall_arg {
            uint8_t u8;
            uint16_t u16;
            uint32_t u32;
            uint64_t u64;
            float f32;
            double f64;
            uint32_t app_pointer;
            void* native_pointer;
            uint8_t __padding[16];
        };

        enum wamr_ext_syscall_id {
            __EXT_SYSCALL_WAMR_EXT_SYSCTL = 1,

            // Pthread ext
            __EXT_SYSCALL_PTHREAD_MUTEX_INIT = 100,
            __EXT_SYSCALL_PTHREAD_MUTEX_TIMEDLOCK = 101,
            __EXT_SYSCALL_PTHREAD_MUTEX_UNLOCK = 102,
            __EXT_SYSCALL_PTHREAD_MUTEX_DESTROY = 103,

            __EXT_SYSCALL_PTHREAD_COND_INIT = 110,
            __EXT_SYSCALL_PTHREAD_COND_TIMEDWAIT = 111,
            __EXT_SYSCALL_PTHREAD_COND_BROADCAST = 112,
            __EXT_SYSCALL_PTHREAD_COND_SIGNAL = 113,
            __EXT_SYSCALL_PTHREAD_COND_DESTROY = 114,

            __EXT_SYSCALL_PTHREAD_RWLOCK_INIT = 120,
            __EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDRDLOCK = 121,
            __EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDWRLOCK = 122,
            __EXT_SYSCALL_PTHREAD_RWLOCK_UNLOCK = 123,
            __EXT_SYSCALL_PTHREAD_RWLOCK_DESTROY = 124,

            __EXT_SYSCALL_PTHREAD_HOST_SETNAME = 130,

            // Filesystem ext
            __EXT_SYSCALL_FD_STATVFS = 200,
            __EXT_SYSCALL_FD_EXT_FCNTL = 201,

            // Socket ext
            __EXT_SYSCALL_SOCK_OPEN = 300,
            __EXT_SYSCALL_SOCK_BIND = 301,
            __EXT_SYSCALL_SOCK_CONNECT = 302,
            __EXT_SYSCALL_SOCK_LISTEN = 303,
            __EXT_SYSCALL_SOCK_ACCEPT = 304,
            __EXT_SYSCALL_SOCK_GETSOCKNAME = 305,
            __EXT_SYSCALL_SOCK_GETPEERNAME = 306,
            __EXT_SYSCALL_SOCK_SHUTDOWN = 307,
            __EXT_SYSCALL_SOCK_GETSOCKOPT = 308,
            __EXT_SYSCALL_SOCK_SETSOCKOPT = 309,
            __EXT_SYSCALL_SOCK_RECVMSG = 310,
            __EXT_SYSCALL_SOCK_SENDMSG = 311,
            __EXT_SYSCALL_SOCK_GETIFADDRS = 312,

            // Process ext
            __EXT_SYSCALL_PROC_SPAWN = 400,
            __EXT_SYSCALL_PROC_WAIT_PID = 401,
        };

        typedef long long wasi_time_t;
        typedef long long wasi_suseconds_t;

        struct wasi_timeval_t {
            wasi_time_t tv_sec;
            wasi_suseconds_t tv_usec;
        };

        struct wasi_iovec_t {
            uint32_t app_buf_offset;
            uint32_t buf_len;
        };
    }

    struct ExtSyscallBase {
    public:
        ExtSyscallBase(const char* argSig, void* pFunc) : m_argSig(argSig), m_pFunc(pFunc) {}
        int32_t Invoke(wasm_exec_env_t pExecEnv, uint32_t argc, wasi::wamr_ext_syscall_arg* appArgv);
    protected:
        virtual int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg* appArgv) = 0;

        std::string m_argSig;
        void* m_pFunc;
    };

    struct ExtSyscall_P : public ExtSyscallBase {
    public:
        ExtSyscall_P(void* pFunc) : ExtSyscallBase("*", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_S : public ExtSyscallBase {
    public:
        ExtSyscall_S(void* pFunc) : ExtSyscallBase("$", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_P_U32 : public ExtSyscallBase {
    public:
        ExtSyscall_P_U32(void* pFunc) : ExtSyscallBase("*i", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_P_U64 : public ExtSyscallBase {
    public:
        ExtSyscall_P_U64(void* pFunc) : ExtSyscallBase("*I", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_P_P(void* pFunc) : ExtSyscallBase("**", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_P : public ExtSyscallBase {
    public:
        ExtSyscall_U32_P(void* pFunc) : ExtSyscallBase("i*", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_U32 : public ExtSyscallBase {
    public:
        ExtSyscall_U32_U32(void* pFunc) : ExtSyscallBase("ii", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_P_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_P_P_P(void* pFunc) : ExtSyscallBase("***", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_S_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_S_P_P(void* pFunc) : ExtSyscallBase("$**", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_P_P_U64 : public ExtSyscallBase {
    public:
        ExtSyscall_P_P_U64(void* pFunc) : ExtSyscallBase("**I", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_U32_P_P(void* pFunc) : ExtSyscallBase("i**", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_U32_U32_P : public ExtSyscallBase {
    public:
        ExtSyscall_U32_U32_U32_P(void* pFunc) : ExtSyscallBase("iii*", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_P_P_U32 : public ExtSyscallBase {
    public:
        ExtSyscall_U32_P_P_U32(void* pFunc) : ExtSyscallBase("i**i", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_U32_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_U32_U32_P_P(void* pFunc) : ExtSyscallBase("ii**", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_U32_U32_P_P : public ExtSyscallBase {
    public:
        ExtSyscall_U32_U32_U32_P_P(void* pFunc) : ExtSyscallBase("iii**", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    struct ExtSyscall_U32_U32_U32_P_U32 : public ExtSyscallBase {
    public:
        ExtSyscall_U32_U32_U32_P_U32(void* pFunc) : ExtSyscallBase("iii*i", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    void RegisterExtSyscall(wasi::wamr_ext_syscall_id syscallID, const std::shared_ptr<ExtSyscallBase>& pSyscallImpl);
};
