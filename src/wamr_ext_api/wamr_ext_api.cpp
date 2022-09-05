#include <wamr_ext_api.h>
#include "../wamr_ext_lib/WamrExtInternalDef.h"
#include "../wamr_ext_lib/WasiPthreadExt.h"
#include "../wamr_ext_lib/WasiWamrExt.h"
#include "../wamr_ext_lib/WasiFSExt.h"
#include "../wamr_ext_lib/WasiSocketExt.h"

namespace WAMR_EXT_NS {
    std::mutex gWasmLock;
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

    int32_t WamrExtModuleLoad(wamr_ext_module_t* module, const char* moduleName, const std::shared_ptr<uint8_t>& pModuleBuf, uint32_t len) {
        std::lock_guard<std::mutex> _al(gWasmLock);
        auto* wasmModule = wasm_runtime_load(pModuleBuf.get(), len, gLastErrorStr, sizeof(gLastErrorStr));
        if (!wasmModule)
            return -1;
        do {
            if (!wasm_runtime_register_module(moduleName, wasmModule, gLastErrorStr, sizeof(gLastErrorStr)))
                break;
            *module = new WamrExtModule(pModuleBuf);
            (*module)->module = wasmModule;
            return 0;
        } while (false);
        wasm_runtime_unload(wasmModule);
        return -1;
    }
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

int32_t wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* module_name, const char* file_path) {
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
        ret = WAMR_EXT_NS::WamrExtModuleLoad(module, module_name, pFileBuf, fileSize);
    } while (false);
    if (f)
        fclose(f);
    return ret;
}

int32_t wamr_ext_module_load_by_buffer(wamr_ext_module_t* module, const char* module_name, const uint8_t* buf, uint32_t len) {
    if (!buf || len <= 0 || !module)
        return EINVAL;
    std::shared_ptr<uint8_t> pNewBuf(new uint8_t[len], std::default_delete<uint8_t[]>());
    memcpy(pNewBuf.get(), buf, len);
    return WAMR_EXT_NS::WamrExtModuleLoad(module, module_name, pNewBuf, len);
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
    std::lock_guard<std::mutex> _al((*inst)->instanceLock);
    return WAMR_EXT_NS::WamrExtSetInstanceOpt((*inst)->config, opt, value);
}

int32_t wamr_ext_instance_start(wamr_ext_instance_t* inst) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    {
        std::vector<const char*> tempPreOpenHostDirs;
        std::vector<const char*> tempPreOpenMapDirs;
        std::list<std::string> tempEnvVarsStringList;
        std::vector<const char*> tempEnvVars;
        std::vector<const char*> tempArgv;
        for (const auto& p : pInst->config.preOpenDirs) {
            tempPreOpenHostDirs.push_back(p.second.c_str());
            tempPreOpenMapDirs.push_back(p.first.c_str());
        }
        for (const auto& p : pInst->config.envVars) {
            tempEnvVarsStringList.emplace_back(std::move(p.first + '=' + p.second));
            tempEnvVars.push_back(tempEnvVarsStringList.back().c_str());
        }
        for (const auto& p : pInst->config.args)
            tempArgv.push_back(p.c_str());
        int newStdinFD = -1;
        int newStdOutFD = -1;
        int newStdErrFD = -1;
#ifndef _WIN32
        for (const auto& p : {
                std::make_pair(&newStdinFD, fileno(stdin)),
                std::make_pair(&newStdOutFD, fileno(stdout)),
                std::make_pair(&newStdErrFD, fileno(stderr)),
        }) {
            *p.first = -1;
            if (p.second != -1 && (*p.first = dup(p.second)) != -1)
                *p.first = uv_open_osfhandle(*p.first);
        }
#else
#error "Duplicating file handler of stdin, stdout and stderr is not supported for Win32"
#endif

        std::lock_guard<std::mutex> _al(WAMR_EXT_NS::gWasmLock);
        wasm_runtime_set_wasi_args_ex(pInst->pModule->module, tempPreOpenHostDirs.data(), tempPreOpenHostDirs.size(),
                                      tempPreOpenMapDirs.data(), tempPreOpenMapDirs.size(),
                                      tempEnvVars.data(), tempEnvVars.size(),
                                      const_cast<char**>(tempArgv.data()), tempArgv.size(),
                                      newStdinFD, newStdOutFD, newStdErrFD);
        wasm_runtime_set_max_thread_num(pInst->config.maxThreadNum);
        // No app heap size for each module
        pInst->wasmInstance = wasm_runtime_instantiate(pInst->pModule->module, 64 * 1024, 0,
                                                       WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr));
        if (!pInst->wasmInstance)
            return -1;
        // Create main executing environment
        wasm_runtime_get_exec_env_singleton(pInst->wasmInstance);
        wasm_runtime_set_custom_data(pInst->wasmInstance, pInst);
    }
    if (!wasm_application_execute_main(pInst->wasmInstance, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->wasmInstance));
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
