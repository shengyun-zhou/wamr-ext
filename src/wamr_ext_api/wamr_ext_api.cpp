#include <wamr_ext_api.h>
#include "../wamr_ext_lib/WamrExtInternalDef.h"
#include "../wamr_ext_lib/WasiPthreadExt.h"
#include "../wamr_ext_lib/WasiWamrExt.h"
#include "../wamr_ext_lib/WasiFSExt.h"
#include "../wamr_ext_lib/WasiSocketExt.h"
#include <wasm_runtime.h>
#include <aot/aot_runtime.h>
#include "../base/LoopThread.h"

namespace WAMR_EXT_NS {
    std::mutex gWasmLock;
    LoopThread gLoopThread("wamr_ext_loop");
    std::unordered_map<wasi::wamr_ext_syscall_id, std::shared_ptr<ExtSyscallBase>> gExtSyscallMap;

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
            *module = new WamrExtModule(pModuleBuf, wasmModule, moduleName);
            return 0;
        } while (false);
        wasm_runtime_unload(wasmModule);
        return -1;
    }

    void RegisterExtSyscall(wasi::wamr_ext_syscall_id syscallID, const std::shared_ptr<ExtSyscallBase>& pSyscallImpl) {
        gExtSyscallMap[syscallID] = pSyscallImpl;
    }

    int32_t WasiExtSyscall(wasm_exec_env_t pExecEnv, uint32_t syscallID, uint32_t argc, wasi::wamr_ext_syscall_arg* argv) {
        auto it = gExtSyscallMap.find((wasi::wamr_ext_syscall_id)syscallID);
        if (it == gExtSyscallMap.end()) {
            assert(false);
            return UVWASI_ENOSYS;
        }
        return it->second->Invoke(pExecEnv, argc, argv);
    }
}

int32_t wamr_ext_init() {
    if (!wasm_runtime_init())
        return -1;
    WAMR_EXT_NS::gExtSyscallMap.reserve(100);
    static NativeSymbol nativeSymbols[] = {
        {"syscall", (void*)WAMR_EXT_NS::WasiExtSyscall, "(ii*)i"},
    };
    wasm_runtime_register_natives("wamr_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    WAMR_EXT_NS::WasiPthreadExt::Init();
    WAMR_EXT_NS::WasiWamrExt::Init();
    WAMR_EXT_NS::WasiFSExt::Init();
    WAMR_EXT_NS::WasiSocketExt::Init();
    WAMR_EXT_NS::gLoopThread.Start();
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
        wasm_runtime_set_wasi_args_ex(pInst->pMainModule->wasmModule, tempPreOpenHostDirs.data(), tempPreOpenHostDirs.size(),
                                      tempPreOpenMapDirs.data(), tempPreOpenMapDirs.size(),
                                      tempEnvVars.data(), tempEnvVars.size(),
                                      const_cast<char**>(tempArgv.data()), tempArgv.size(),
                                      newStdinFD, newStdOutFD, newStdErrFD);
        wasm_runtime_set_max_thread_num(pInst->config.maxThreadNum);
        // No app heap size for each module
        const int STACK_SIZE = 64 * 1024;
        pInst->wasmMainInstance = wasm_runtime_instantiate(pInst->pMainModule->wasmModule, STACK_SIZE, 0,
                                                           WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr));
        if (!pInst->wasmMainInstance)
            return -1;
        auto wasmMainExecEnv = wasm_runtime_create_exec_env(pInst->wasmMainInstance, STACK_SIZE);
        if (!wasmMainExecEnv) {
            snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Failed to create exec env for main module %s", pInst->pMainModule->moduleName.c_str());
            return -1;
        }
        pInst->wasmExecEnvMap[pInst->pMainModule->moduleName] = wasmMainExecEnv;
        if (pInst->wasmMainInstance->module_type == Wasm_Module_Bytecode) {
            auto* pByteCodeInst = (WASMModuleInstance*)pInst->wasmMainInstance;
            for (auto* pSubModuleNode = (WASMSubModInstNode*)bh_list_first_elem(pByteCodeInst->sub_module_inst_list); pSubModuleNode;
                 pSubModuleNode = (WASMSubModInstNode*)bh_list_elem_next(pSubModuleNode)) {
                wasmMainExecEnv = wasm_runtime_create_exec_env((wasm_module_inst_t)pSubModuleNode->module_inst, STACK_SIZE);
                if (!wasmMainExecEnv) {
                    snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Failed to create exec env for sub module %s", pSubModuleNode->module_name);
                    return -1;
                }
                pInst->wasmExecEnvMap[pSubModuleNode->module_name] = wasmMainExecEnv;
            }
        }
        for (const auto& p : pInst->wasmExecEnvMap)
            wasm_runtime_set_custom_data(get_module_inst(p.second), pInst);
    }
    std::lock_guard<std::mutex> _al(pInst->execFuncLock);
    wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(pInst->wasmMainInstance, "_initialize", "()");
    if (!wasmFuncInst) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot find function _initialize() in main module %s\n", pInst->pMainModule->moduleName.c_str());
        return -1;
    }
    if (!wasm_runtime_call_wasm_a(pInst->wasmExecEnvMap[pInst->pMainModule->moduleName], wasmFuncInst, 0, nullptr, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->wasmMainInstance));
        wasm_runtime_clear_exception(pInst->wasmMainInstance);
        return -1;
    }
    return 0;
}

WAMR_EXT_API int32_t wamr_ext_instance_exec_main_func(wamr_ext_instance_t* inst, int32_t* ret_value) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    std::lock_guard<std::mutex> _al(pInst->execFuncLock);
    wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(pInst->wasmMainInstance, "__main_void", "()i");
    if (!wasmFuncInst) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot find function __main_void() in main module %s\n", pInst->pMainModule->moduleName.c_str());
        return -1;
    }
    wasm_val_t wasmRetVal;
    if (!wasm_runtime_call_wasm_a(pInst->wasmExecEnvMap[pInst->pMainModule->moduleName], wasmFuncInst, 1, &wasmRetVal, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->wasmMainInstance));
        wasm_runtime_clear_exception(pInst->wasmMainInstance);
        return -1;
    }
    if (ret_value)
        *ret_value = wasmRetVal.of.i32;
    return 0;
}

WAMR_EXT_API int32_t wamr_ext_instance_destroy(wamr_ext_instance_t* inst) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    {
        std::lock_guard<std::mutex> _al(pInst->execFuncLock);
        for (const auto &p: pInst->wasmExecEnvMap) {
            auto wasmInst = get_module_inst(p.second);
            wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(wasmInst, "__wasm_call_dtors", "()");
            if (wasmFuncInst) {
                wasm_runtime_call_wasm_a(p.second, wasmFuncInst, 0, nullptr, 0, nullptr);
                wasm_runtime_clear_exception(wasmInst);
            }
        }
    }
    delete pInst;
    *inst = nullptr;
    return 0;
}

const char* wamr_ext_strerror(int32_t err) {
    if (err >= 0)
        return strerror(err);
    else
        return WAMR_EXT_NS::gLastErrorStr;
}
