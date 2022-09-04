#include <wamr_ext_api.h>
#include "../base/WamrExtInternalDef.h"
#include "../wamr_ext_lib/WasiPthreadExt.h"
#include "../wamr_ext_lib/WasiWamrExt.h"
#include "../wamr_ext_lib/WasiFSExt.h"
#include "../wamr_ext_lib/WasiSocketExt.h"

namespace WAMR_EXT_NS {
    int32_t WamrExtSetInstanceOpt(WamrExtInstanceConfig& config, WamrExtInstanceOpt opt, const void* value) {
        if (!value)
            return EINVAL;
        int32_t ret = 0;
        switch (opt) {
            case WAMR_EXT_INST_OPT_MAX_THREAD_NUM: {
                uint32_t maxThreadNum = *((uint32_t*)value);
                if (maxThreadNum > 30)
                    ret = EINVAL;
                else
                    config.maxThreadNum = maxThreadNum;
                break;
            }
            case WAMR_EXT_INST_OPT_ADD_HOST_DIR: {
                WamrExtKeyValueSS* kv = (WamrExtKeyValueSS*)value;
                // v: mapped dir, k: host dir
                config.preOpenDirs[kv->v] = kv->k;
                break;
            }
            case WAMR_EXT_INST_OPT_ADD_ENV_VAR: {
                WamrExtKeyValueSS* kv = (WamrExtKeyValueSS*)value;
                config.envVars[kv->k] = kv->v;
                break;
            }
            case WAMR_EXT_INST_OPT_ARG: {
                config.args.clear();
                for (int i = 0; ((char**)value)[i]; i++)
                    config.args.emplace_back(((char**)value)[i]);
                break;
            }
            default:
                ret = EINVAL;
                break;
        }
        return ret;
    }

    int32_t WamrExtModuleLoad(wamr_ext_module_t* module, const std::shared_ptr<uint8_t>& pModuleBuf, uint32_t len) {
        auto* wasmModule = wasm_runtime_load(pModuleBuf.get(), len, gLastErrorStr, sizeof(gLastErrorStr));
        if (!wasmModule)
            return -1;
        *module = new WamrExtModule(pModuleBuf);
        (*module)->module = wasmModule;
        return 0;
    }

    std::mutex gInstInitializationLock;
}

int32_t wamr_ext_init() {
    if (!wasm_runtime_init())
        return -1;
    WAMR_EXT_NS::WasiPthreadExt::Init();
    WAMR_EXT_NS::WasiWamrExt::Init();
    WAMR_EXT_NS::WasiFSExt::Init();
    WAMR_EXT_NS::WasiSocketExt::Init();
    return 0;
}

#define WASM_MAX_MODULE_FILE_SIZE (512 * 1024 * 1024)   // 512MB

int32_t wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* file_path) {
    auto* f = fopen(file_path, "rb");
    if (!f)
        return errno;
    int32_t ret = 0;
    do {
        fseek(f, 0, SEEK_END);
        uint64_t fileSize = ftell(f);
        if (fileSize > WASM_MAX_MODULE_FILE_SIZE) {
            ret = EFBIG;
            break;
        }
        std::shared_ptr<uint8_t> pFileBuf(new uint8_t[fileSize], std::default_delete<uint8_t[]>());
        fseek(f, 0, SEEK_SET);
        fread(pFileBuf.get(), 1, fileSize, f);
        fclose(f);
        f = nullptr;
        ret = WAMR_EXT_NS::WamrExtModuleLoad(module, pFileBuf, fileSize);
    } while (false);
    if (f)
        fclose(f);
    return ret;
}

int32_t wamr_ext_module_load_by_buffer(wamr_ext_module_t* module, const uint8_t* buf, uint32_t len) {
    if (!buf || len <= 0 || !module)
        return EINVAL;
    std::shared_ptr<uint8_t> pNewBuf(new uint8_t[len], std::default_delete<uint8_t[]>());
    memcpy(pNewBuf.get(), buf, len);
    return WAMR_EXT_NS::WamrExtModuleLoad(module, pNewBuf, len);
}

int32_t wamr_ext_module_set_inst_default_opt(wamr_ext_module_t* module, enum WamrExtInstanceOpt opt, const void* value) {
    if (!module || !(*module))
        return EINVAL;
    return WAMR_EXT_NS::WamrExtSetInstanceOpt((*module)->instDefaultConf, opt, value);
}

int32_t wamr_ext_instance_create(wamr_ext_module_t* module, wamr_ext_instance_t* inst) {
    if (!module || !(*module))
        return EINVAL;
    *inst = new WamrExtInstance(*module);
    return 0;
}

int32_t wamr_ext_instance_set_opt(wamr_ext_instance_t* inst, enum WamrExtInstanceOpt opt, const void* value) {
    if (!inst || !(*inst))
        return EINVAL;
    return WAMR_EXT_NS::WamrExtSetInstanceOpt((*inst)->instConfig, opt, value);
}

int32_t wamr_ext_instance_start(wamr_ext_instance_t* inst) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    pInst->pRuntimeData.reset(new WamrExtInstance::InstRuntimeData(pInst->instConfig));
    {
        std::lock_guard<std::mutex> _al(WAMR_EXT_NS::gInstInitializationLock);
        wasm_runtime_set_wasi_args_ex(pInst->pModule->module, pInst->pRuntimeData->preOpenHostDirs.data(), pInst->pRuntimeData->preOpenHostDirs.size(),
                                      pInst->pRuntimeData->preOpenMapDirs.data(), pInst->pRuntimeData->preOpenMapDirs.size(),
                                      pInst->pRuntimeData->envVars.data(), pInst->pRuntimeData->envVars.size(),
                                      const_cast<char**>(pInst->pRuntimeData->argv.data()), pInst->pRuntimeData->argv.size(),
                                      pInst->pRuntimeData->newStdinFD, pInst->pRuntimeData->newStdOutFD, pInst->pRuntimeData->newStdErrFD);
        wasm_runtime_set_max_thread_num(pInst->instConfig.maxThreadNum);
        // No app heap size for each module
        pInst->instance = wasm_runtime_instantiate(pInst->pModule->module, 64 * 1024, 0,
                                                   WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr));
        if (!pInst->instance)
            return -1;
        wasm_runtime_set_custom_data(pInst->instance, pInst);
    }
    if (!wasm_application_execute_main(pInst->instance, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->instance));
        return -1;
    }
    return 0;
}

const char* wamr_ext_strerror(int32_t err) {
    if (err >= 0)
        return strerror(err);
    else
        return WAMR_EXT_NS::gLastErrorStr;
}
