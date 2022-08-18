#pragma once
#include "BaseDef.h"

struct WamrExtInstanceConfig {
    uint8_t maxThreadNum{4};
    std::map<std::string, std::string> preOpenDirs;     // mapped dir -> host dir
    std::map<std::string, std::string> envVars;

    WamrExtInstanceConfig();
};

struct WamrExtModule {
    std::shared_ptr<uint8_t> pModuleBuf;
    wasm_module_t module{nullptr};
    WamrExtInstanceConfig instDefaultConf;

    explicit WamrExtModule(const std::shared_ptr<uint8_t>& pBuf) : pModuleBuf(pBuf) {}
    WamrExtModule(const WamrExtModule&) = delete;
    WamrExtModule& operator=(const WamrExtModule&) = delete;
};

struct WamrExtInstance {
    struct InstRuntimeData {
        std::vector<const char*> preOpenHostDirs;
        std::vector<const char*> preOpenMapDirs;
        std::list<std::string> envVarsStringList;
        std::vector<const char*> envVars;

        InstRuntimeData(const WamrExtInstanceConfig& config);
        InstRuntimeData(const InstRuntimeData&) = delete;
        InstRuntimeData& operator=(const InstRuntimeData&) = delete;
    };

    WamrExtModule* pModule;
    WamrExtInstanceConfig instConfig;
    wasm_module_inst_t instance{nullptr};
    std::shared_ptr<InstRuntimeData> pRuntimeData;

    explicit WamrExtInstance(WamrExtModule* _pModule) : pModule(_pModule),
        instConfig(_pModule->instDefaultConf) {}
    WamrExtInstance(const WamrExtInstance&) = delete;
    WamrExtInstance& operator=(const WamrExtInstance&) = delete;
};

namespace WAMR_EXT_NS {
    extern thread_local char gLastErrorStr[200];
    namespace wasi {
        struct wamr_wasi_struct_base {
            uint16_t struct_ver;
            uint16_t struct_size;
        };

#define wamr_wasi_struct_assert(T) static_assert(std::is_base_of<WAMR_EXT_NS::wasi::wamr_wasi_struct_base, T>::value && std::is_trivial<T>::value)
    }
};
