#pragma once
#include "../base/BaseDef.h"
#include "WasiPthreadExt.h"

struct WamrExtInstanceConfig {
    uint8_t maxThreadNum{4};
    std::map<std::string, std::string> preOpenDirs;     // mapped dir -> host dir
    std::map<std::string, std::string> envVars;
    std::vector<std::string> args;

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
    std::mutex instanceLock;
    WamrExtModule* pMainModule;
    WamrExtInstanceConfig config;
    wasm_module_inst_t wasmMainInstance{nullptr};
    std::mutex execFuncLock;
    std::unordered_map<std::string, wasm_exec_env_t> wasmExecEnvMap;
    WAMR_EXT_NS::WasiPthreadExt::InstancePthreadManager wasiPthreadManager;

    explicit WamrExtInstance(WamrExtModule* _pModule) : pMainModule(_pModule),
                                                        config(_pModule->instDefaultConf) {}
    ~WamrExtInstance();
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

        struct wamr_wasi_struct_base {
            uint16_t struct_ver;
            uint16_t struct_size;
        };

#define wamr_wasi_struct_assert(T) static_assert(std::is_base_of<WAMR_EXT_NS::wasi::wamr_wasi_struct_base, T>::value && std::is_trivial<T>::value)
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

    struct ExtSyscall_P_P_U64 : public ExtSyscallBase {
    public:
        ExtSyscall_P_P_U64(void* pFunc) : ExtSyscallBase("**I", pFunc) {}
    protected:
        int32_t DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) override;
    };

    void RegisterExtSyscall(wasi::wamr_ext_syscall_id syscallID, const std::shared_ptr<ExtSyscallBase>& pSyscallImpl);
};
