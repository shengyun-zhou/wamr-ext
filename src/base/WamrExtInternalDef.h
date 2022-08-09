#pragma once
#include "BaseDef.h"

struct WamrExtInstanceConfig {
    uint8_t maxThreadNum{4};
};

struct WamrExtModule {
    std::shared_ptr<uint8_t> pModuleBuf;
    wasm_module_t module{nullptr};
    WamrExtInstanceConfig instDefaultConf;

    explicit WamrExtModule(const std::shared_ptr<uint8_t>& pBuf) : pModuleBuf(pBuf) {}
};

struct WamrExtInstance {
    WamrExtModule* pModule;
    WamrExtInstanceConfig instConfig;
    wasm_module_inst_t instance{nullptr};
    explicit WamrExtInstance(WamrExtModule* _pModule) : pModule(_pModule),
        instConfig(_pModule->instDefaultConf) {}
};

namespace WAMR_EXT_NS {
    extern thread_local char gLastErrorStr[200];
};
