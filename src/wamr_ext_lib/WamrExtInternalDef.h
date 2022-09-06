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
};