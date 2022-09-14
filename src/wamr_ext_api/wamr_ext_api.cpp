#include <wamr_ext_api.h>
#include "../wamr_ext_lib/WamrExtInternalDef.h"
#include "../wamr_ext_lib/WasiPthreadExt.h"
#include "../wamr_ext_lib/WasiWamrExt.h"
#include "../wamr_ext_lib/WasiFSExt.h"
#include "../wamr_ext_lib/WasiSocketExt.h"
#include "../wamr_ext_lib/WasiProcessExt.h"
#include <wasm_runtime.h>
#include <aot/aot_runtime.h>
#include "../base/LoopThread.h"

namespace WAMR_EXT_NS {
    std::mutex gWasmLock;
    LoopThread gLoopThread("wamr_ext_loop");
    std::list<std::shared_ptr<WamrExtInstance>> gAllInstanceList;
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
            case WAMR_EXT_INST_OPT_EXCEPTION_CALLBACK: {
                config.exceptionCB = *(WamrExtInstanceExceptionCB*)value;
                break;
            }
            case WAMR_EXT_INST_OPT_ADD_HOST_COMMAND: {
                WamrExtKeyValueSS* kv = (WamrExtKeyValueSS*)value;
                if (strstr(kv->k, "/") || strstr(kv->k, "\\")) {
                    ret = EINVAL;
                    break;
                }
                config.hostCmdWhitelist[kv->k] = kv->v;
                break;
            }
            case WAMR_EXT_INST_OPT_MAX_MEMORY: {
                uint32_t maxMem = *((uint32_t*)value);
                if (maxMem == 0)
                    ret = EINVAL;
                else
                    config.maxMemory = maxMem;
                break;
            }
            default:
                ret = EINVAL;
                break;
        }
        return ret;
    }

    int32_t WamrExtCheckNewModuleName(const char* moduleName) {
        if (!moduleName || !moduleName[0] || wasm_runtime_is_built_in_module(moduleName) ||
            strcmp(moduleName, "wamr_ext") == 0) {
            return EINVAL;
        }
        if (wasm_runtime_is_module_registered(moduleName))
            return EEXIST;
        return 0;
    }

    int32_t WamrExtModuleLoad(wamr_ext_module_t* module, const char* moduleName, const std::shared_ptr<uint8_t>& pModuleBuf, uint32_t len) {
        std::lock_guard<std::mutex> _al(gWasmLock);
        int32_t err = WamrExtCheckNewModuleName(moduleName);
        if (err != 0)
            return err;
        auto* wasmModule = wasm_runtime_load(pModuleBuf.get(), len, gLastErrorStr, sizeof(gLastErrorStr));
        if (!wasmModule)
            return -1;
        do {
            // Set small memory here for dynamic enlarging during instantiation later
            if (wasmModule->module_type == Wasm_Module_Bytecode) {
                auto* pWasmByteCodeModule = (WASMModule*)wasmModule;
                if (pWasmByteCodeModule->memory_count > 0) {
                    assert(pWasmByteCodeModule->memory_count == 1);
                    pWasmByteCodeModule->memories[0].max_page_count = pWasmByteCodeModule->memories[0].init_page_count + 1;
                }
            } else if (wasmModule->module_type == Wasm_Module_AoT) {
                auto* pAOTModule = (AOTModule*)wasmModule;
                if (pAOTModule->memory_count > 0) {
                    assert(pAOTModule->memory_count == 1);
                    pAOTModule->memories[0].mem_max_page_count = pAOTModule->memories[0].mem_init_page_count + 1;
                }
            }
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
        if (argc > 0) {
            if (!argv || !wasm_runtime_validate_native_addr(get_module_inst(pExecEnv), argv, sizeof(wasi::wamr_ext_syscall_arg) * argc)) {
                assert(false);
                return EFAULT;
            }
        }
        auto it = gExtSyscallMap.find((wasi::wamr_ext_syscall_id)syscallID);
        if (it == gExtSyscallMap.end()) {
            assert(false);
            return UVWASI_ENOSYS;
        }
        return it->second->Invoke(pExecEnv, argc, argv);
    }

    void LoopCheckInstanceRoutine() {
        std::unique_lock<std::mutex> globalWasmAL(gWasmLock);
        if (gAllInstanceList.empty())
            return;
        auto instanceListIt = gAllInstanceList.begin();
        globalWasmAL.unlock();
        std::shared_ptr<WamrExtInstance> pInst;
        while (true) {
            pInst = *instanceListIt;
            bool bInstDestroyed = false;
            {
                std::unique_lock<std::mutex> instAL(pInst->instanceLock);
                switch (pInst->state) {
                    case WamrExtInstance::STATE_STARTED: {
                        const char* exceptionStr = wasm_runtime_get_exception(pInst->wasmMainInstance);
                        if (exceptionStr && exceptionStr[0]) {
                            WamrExtExceptionInfo exceptionInfo;
                            exceptionInfo.errorCode = -1;
                            exceptionInfo.errorStr = exceptionStr;
                            auto exceptionCB = pInst->config.exceptionCB;
                            pInst->state = WamrExtInstance::STATE_ENDED;
                            if (exceptionCB.func) {
                                instAL.unlock();
                                exceptionCB.func(pInst->pUserCallbackPointer, &exceptionInfo, exceptionCB.user_data);
                                instAL.lock();
                            }
                        }
                        break;
                    }
                    case WamrExtInstance::STATE_DESTROYED: {
                        bInstDestroyed = true;
                        break;
                    }
                }
            }
            globalWasmAL.lock();
            if (bInstDestroyed)
                instanceListIt = gAllInstanceList.erase(instanceListIt);
            else
                instanceListIt = std::next(instanceListIt);
            if (instanceListIt == gAllInstanceList.end()) {
                globalWasmAL.unlock();
                break;
            }
            globalWasmAL.unlock();
        }
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
    WAMR_EXT_NS::WasiProcessExt::Init();
    WAMR_EXT_NS::gLoopThread.Start();
    WAMR_EXT_NS::gLoopThread.PostTimerTask(WAMR_EXT_NS::LoopCheckInstanceRoutine, 0, 100);
    return 0;
}

#define WASM_MAX_MODULE_FILE_SIZE (512 * 1024 * 1024)   // 512MB

int32_t wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* module_name, const char* file_path) {
    int32_t err = WAMR_EXT_NS::WamrExtCheckNewModuleName(module_name);
    if (err != 0)
        return err;
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
    *inst = new WamrExtInstance(*module, inst);
    std::lock_guard<std::mutex> _al(WAMR_EXT_NS::gWasmLock);
    WAMR_EXT_NS::gAllInstanceList.emplace_back(*inst);
    return 0;
}

int32_t wamr_ext_instance_set_opt(wamr_ext_instance_t* inst, enum WamrExtInstanceOpt opt, const void* value) {
    if (!inst || !(*inst))
        return EINVAL;
    auto* pInst = *inst;
    std::lock_guard<std::mutex> _al(pInst->instanceLock);
    return WAMR_EXT_NS::WamrExtSetInstanceOpt(pInst->config, opt, value);
}

int32_t wamr_ext_instance_start(wamr_ext_instance_t* inst) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    std::lock_guard<std::mutex> _al(pInst->execFuncLock);
    {
        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
        if (pInst->state != WamrExtInstance::STATE_NEW) {
            snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Instance state error");
            return -1;
        }
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

        std::lock_guard<std::mutex> wasmAL(WAMR_EXT_NS::gWasmLock);
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
            auto* pMemory = pByteCodeInst->default_memory;
            assert(pMemory->heap_data == pMemory->heap_data_end && !pMemory->heap_handle);   // No app heap
            assert(pMemory->num_bytes_per_page == 64 * 1024);
            auto newMaxPageCount = std::max<uint32_t>(pInst->config.maxMemory / pMemory->num_bytes_per_page, 1);
            if (newMaxPageCount > pMemory->max_page_count) {
                pMemory->max_page_count = newMaxPageCount;
                if (pMemory->is_shared) {
                    // Allocate new memory but don't touch it to reduce process RSS
                    uint32_t heapOffset = pMemory->heap_data - pMemory->memory_data;
                    uint32_t totalMemSize = pMemory->num_bytes_per_page * newMaxPageCount;
                    uint8_t* pNewMem = (uint8_t*)wasm_runtime_realloc(pMemory->memory_data, pMemory->num_bytes_per_page * newMaxPageCount);
                    if (!pNewMem) {
                        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot allocate %uB memory for instance\n", totalMemSize);
                        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
                        pInst->state = WamrExtInstance::STATE_ENDED;
                        return -1;
                    }
                    pMemory->memory_data = pNewMem;
                    pMemory->memory_data_end = pMemory->memory_data + totalMemSize;
                    pMemory->heap_data = pMemory->heap_data_end = pMemory->memory_data + heapOffset;
                    pMemory->cur_page_count = pMemory->max_page_count;
                }
            }
        } else if (pInst->wasmMainInstance->module_type == Wasm_Module_AoT) {
            auto* pAOTInst = (AOTModuleInstance*)pInst->wasmMainInstance;
            auto* pMemory = ((AOTMemoryInstance**)pAOTInst->memories.ptr)[0];
            assert(pMemory->heap_data.ptr == pMemory->heap_data_end.ptr && !pMemory->heap_handle.ptr);   // No app heap
            assert(pMemory->num_bytes_per_page == 64 * 1024);
            auto newMaxPageCount = std::max<uint32_t>(pInst->config.maxMemory / pMemory->num_bytes_per_page, 1);
            if (newMaxPageCount > pMemory->max_page_count) {
                pMemory->max_page_count = newMaxPageCount;
                if (pMemory->is_shared) {
                    // Allocate new memory but don't touch it to reduce process RSS
                    uint32_t heapOffset = (uint8_t*)pMemory->heap_data.ptr - (uint8_t*)pMemory->memory_data.ptr;
                    uint32_t totalMemSize = pMemory->num_bytes_per_page * newMaxPageCount;
                    uint8_t* pNewMem = (uint8_t*)wasm_runtime_realloc(pMemory->memory_data.ptr, pMemory->num_bytes_per_page * newMaxPageCount);
                    if (!pNewMem) {
                        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot allocate %uB memory for instance\n", totalMemSize);
                        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
                        pInst->state = WamrExtInstance::STATE_ENDED;
                        return -1;
                    }
                    pMemory->memory_data.ptr = pNewMem;
                    pMemory->memory_data_size = totalMemSize;
                    pMemory->memory_data_end.ptr = (uint8_t*)pMemory->memory_data.ptr + pMemory->memory_data_size;
                    pMemory->heap_data.ptr = pMemory->heap_data_end.ptr = (uint8_t*)pMemory->memory_data.ptr + heapOffset;
                    pMemory->cur_page_count = pMemory->max_page_count;
                }
            }
        }
        for (const auto& p : pInst->wasmExecEnvMap)
            wasm_runtime_set_custom_data(get_module_inst(p.second), pInst);
    }
    wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(pInst->wasmMainInstance, "_initialize", "()");
    if (!wasmFuncInst) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot find function _initialize() in main module %s\n", pInst->pMainModule->moduleName.c_str());
        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
        pInst->state = WamrExtInstance::STATE_ENDED;
        return -1;
    }
    if (!wasm_runtime_call_wasm_a(pInst->wasmExecEnvMap[pInst->pMainModule->moduleName], wasmFuncInst, 0, nullptr, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->wasmMainInstance));
        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
        pInst->state = WamrExtInstance::STATE_ENDED;
        return -1;
    }
    std::lock_guard<std::mutex> instAL(pInst->instanceLock);
    pInst->state = WamrExtInstance::STATE_STARTED;
    return 0;
}

WAMR_EXT_API int32_t wamr_ext_instance_exec_main_func(wamr_ext_instance_t* inst, int32_t* ret_value) {
    if (!inst || !(*inst))
        return EINVAL;
    auto pInst = *inst;
    std::lock_guard<std::mutex> _al(pInst->execFuncLock);
    {
        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
        if (pInst->state != WamrExtInstance::STATE_STARTED) {
            snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Instance not started");
            return -1;
        }
    }
    wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(pInst->wasmMainInstance, "__main_void", "()i");
    if (!wasmFuncInst) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "Cannot find function __main_void() in main module %s\n", pInst->pMainModule->moduleName.c_str());
        return -1;
    }
    wasm_val_t wasmRetVal;
    if (!wasm_runtime_call_wasm_a(pInst->wasmExecEnvMap[pInst->pMainModule->moduleName], wasmFuncInst, 1, &wasmRetVal, 0, nullptr)) {
        snprintf(WAMR_EXT_NS::gLastErrorStr, sizeof(WAMR_EXT_NS::gLastErrorStr), "%s", wasm_runtime_get_exception(pInst->wasmMainInstance));
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
    bool bRunDtor = false;
    std::lock_guard<std::mutex> _al(pInst->execFuncLock);
    {
        std::lock_guard<std::mutex> instAL(pInst->instanceLock);
        if (pInst->state >= WamrExtInstance::STATE_DESTROYING) {
            *inst = nullptr;
            return 0;
        }
        bRunDtor = pInst->state == WamrExtInstance::STATE_STARTED;
        pInst->state = WamrExtInstance::STATE_DESTROYING;
    }
    if (bRunDtor) {
        for (const auto &p: pInst->wasmExecEnvMap) {
            auto wasmInst = get_module_inst(p.second);
            wasm_function_inst_t wasmFuncInst = wasm_runtime_lookup_function(wasmInst, "__wasm_call_dtors", "()");
            if (wasmFuncInst)
                wasm_runtime_call_wasm_a(p.second, wasmFuncInst, 0, nullptr, 0, nullptr);
        }
    }
    if (pInst->wasmMainInstance) {
        // Close all FDs opened by app
        std::vector<uint32_t> appFDs;
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(pInst->wasmMainInstance);
        uvwasi_fd_table_lock(pUVWasi->fds);
        appFDs.reserve(pUVWasi->fds->size);
        for (uint32_t i = 0; i < pUVWasi->fds->size; i++) {
            auto pEntry = pUVWasi->fds->fds[i];
            if (pEntry)
                appFDs.push_back(pEntry->id);
        }
        uvwasi_fd_table_unlock(pUVWasi->fds);
        for (auto appFD : appFDs)
            uvwasi_fd_close(pUVWasi, appFD);
    }
    for (const auto& p : pInst->wasmExecEnvMap)
        wasm_exec_env_destroy(p.second);
    pInst->wasmExecEnvMap.clear();
    if (pInst->wasmMainInstance)
        wasm_runtime_deinstantiate(pInst->wasmMainInstance);
    pInst->wasmMainInstance = nullptr;
    std::lock_guard<std::mutex> instAL(pInst->instanceLock);
    // Mark this instance destroyed first, then destroy finally in the loop thread
    pInst->state = WamrExtInstance::STATE_DESTROYED;
    *inst = nullptr;
    return 0;
}

const char* wamr_ext_strerror(int32_t err) {
    if (err >= 0)
        return strerror(err);
    else
        return WAMR_EXT_NS::gLastErrorStr;
}

int32_t wamr_ext_exception_get_info(wamr_ext_exception_info_t* exception, enum WamrExtExceptionInfoEnum info, void* value) {
    if (!exception || !value)
        return EINVAL;
    int32_t err = 0;
    switch (info) {
        case WAMR_EXT_EXCEPTION_INFO_ERROR_CODE:
            *((int32_t*)value) = exception->errorCode;
            break;
        case WAMR_EXT_EXCEPTION_INFO_ERROR_STRING:
            *((char**)value) = const_cast<char*>(exception->errorStr.c_str());
            break;
        default:
            err = EINVAL;
            break;
    }
    return err;
}
