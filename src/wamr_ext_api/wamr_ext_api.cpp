#include <wamr_ext_api.h>
#include "../base/WamrExtInternalDef.h"
#include "../wamr_ext_lib/PthreadExt.h"

namespace WAMR_EXT_NS {
    int WamrExtSetInstanceOpt(WamrExtInstanceConfig& config, WamrExtInstanceOpt opt, const void* value) {
        int ret = 0;
        switch (opt) {
            case WAMR_INST_OPT_MAX_THREAD_NUM: {
                uint32_t maxThreadNum = *((uint32_t*)value);
                if (maxThreadNum > 30)
                    ret = EINVAL;
                else
                    config.maxThreadNum = maxThreadNum;
                break;
            }
            default:
                ret = EINVAL;
                break;
        }
        return ret;
    }

    int WamrExtModuleLoad(wamr_ext_module_t* module, const std::shared_ptr<uint8_t>& pModuleBuf, uint32_t len) {
        auto* wasmModule = wasm_runtime_load(pModuleBuf.get(), len, gLastErrorStr, sizeof(gLastErrorStr));
        if (!wasmModule)
            return -1;
        *module = new WamrExtModule(pModuleBuf);
        (*module)->module = wasmModule;
        wasm_runtime_set_wasi_args_ex(wasmModule, nullptr, 0, nullptr, 0,
                                      nullptr, 0, nullptr, 0,
                                      fileno(stdin), fileno(stdout), fileno(stderr));
        return 0;
    }

    std::mutex gInstInitializationLock;
};

int wamr_ext_init() {
    if (!wasm_runtime_init())
        return -1;
    WAMR_EXT_NS::PthreadExt::Init();
    return 0;
}

#define WASM_MAX_MODULE_FILE_SIZE (512 * 1024 * 1024)   // 512MB

int wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* file_path) {
    auto* f = fopen(file_path, "rb");
    if (!f)
        return errno;
    int ret = 0;
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

int wamr_ext_module_load_by_buffer(wamr_ext_module_t* module, const uint8_t* buf, uint32_t len) {
    if (!buf || len <= 0 || !module)
        return EINVAL;
    std::shared_ptr<uint8_t> pNewBuf(new uint8_t[len], std::default_delete<uint8_t[]>());
    memcpy(pNewBuf.get(), buf, len);
    return WAMR_EXT_NS::WamrExtModuleLoad(module, pNewBuf, len);
}

int wamr_ext_module_set_inst_default_opt(wamr_ext_module_t* module, enum WamrExtInstanceOpt opt, const void* value) {
    if (!module || !(*module))
        return EINVAL;
    return WAMR_EXT_NS::WamrExtSetInstanceOpt((*module)->instDefaultConf, opt, value);
}

int wamr_ext_instance_create(wamr_ext_module_t* module, wamr_ext_instance_t* inst) {
    if (!module || !(*module))
        return EINVAL;
    *inst = new WamrExtInstance(*module);
    return 0;
}

int wamr_ext_instance_set_opt(wamr_ext_instance_t* inst, enum WamrExtInstanceOpt opt, const void* value) {
    if (!inst || !(*inst))
        return EINVAL;
    return WAMR_EXT_NS::WamrExtSetInstanceOpt((*inst)->instConfig, opt, value);
}

int wamr_ext_instance_init(wamr_ext_instance_t* inst) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    std::lock_guard<std::mutex> _al(WAMR_EXT_NS::gInstInitializationLock);
    wasm_runtime_set_max_thread_num(pInst->instConfig.maxThreadNum);
    // No app heap size for each module
    pInst->instance = wasm_runtime_instantiate(pInst->pModule->module, 512 * 1024, 0,
                                               WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr));
    if (!pInst->instance)
        return -1;
    return 0;
}

WAMR_EXT_API int wamr_ext_instance_run_main(wamr_ext_instance_t* inst, int32_t argc, char** argv) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    int ret = 0;
    if (!wasm_application_execute_main(pInst->instance, argc, argv)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->instance));
        ret = -1;
    }
    return ret;
}

const char* wamr_ext_strerror(int err) {
    if (err >= 0)
        return strerror(err);
    else
        return WAMR_EXT_NS::gLastErrorStr;
}
