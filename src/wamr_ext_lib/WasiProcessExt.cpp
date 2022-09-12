#include "WasiProcessExt.h"
#include "WamrExtInternalDef.h"
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#endif

namespace WAMR_EXT_NS {
    namespace wasi {
        struct wamr_spawn_action_base {
            uint32_t app_next_action_pointer;
            uint32_t cmd;
        };

        static_assert(std::is_trivial<wamr_spawn_action_base>::value);

        struct wamr_spawn_action_fdup2 : public wamr_spawn_action_base {
            int32_t src_fd;
            int32_t dest_fd;
        };

        static_assert(std::is_trivial<wamr_spawn_action_fdup2>::value);

        struct wamr_spawn_req {
            uint32_t app_cmd_name_pointer;
            uint32_t app_argv_pointer;
            uint32_t argc;
            uint32_t app_fa_pointer;
            int32_t ret_pid;
        };
    }

    std::mutex WasiProcessExt::m_gProcessSpawnLock;

    void WasiProcessExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PROC_SPAWN, std::make_shared<ExtSyscall_P>((void*)ProcessSpawn));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PROC_WAIT_PID, std::make_shared<ExtSyscall_U32_U32_P_P>(
                (void *) ProcessWaitPID));
    }

#define __WAMR_WASI_SPAWN_ACTION_FDUP2 1

    int32_t WasiProcessExt::ProcessSpawn(wasm_exec_env_t pExecEnv, void *_pAppReq) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        auto* pWamrExtInst = (WamrExtInstance*)wasm_runtime_get_custom_data(pWasmModuleInst);
        auto* pProcManager = &pWamrExtInst->wasiProcessManager;
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppReq, sizeof(wasi::wamr_spawn_req)))
            return UVWASI_EFAULT;
        auto pAppReq = (wasi::wamr_spawn_req*)_pAppReq;
        if (!wasm_runtime_validate_app_str_addr(pWasmModuleInst, pAppReq->app_cmd_name_pointer))
            return UVWASI_EFAULT;
        const char* cmdName = static_cast<const char*>(wasm_runtime_addr_app_to_native(pWasmModuleInst, pAppReq->app_cmd_name_pointer));
        if (!wasm_runtime_validate_app_addr(pWasmModuleInst, pAppReq->app_argv_pointer, sizeof(uint32_t) * pAppReq->argc))
            return UVWASI_EFAULT;
        std::vector<const char*> hostArgv;
        hostArgv.reserve(pAppReq->argc + 2);
        hostArgv.push_back(cmdName);
        uint32_t* appArgvPointers = static_cast<uint32_t*>(wasm_runtime_addr_app_to_native(pWasmModuleInst, pAppReq->app_argv_pointer));
        for (uint32_t i = 0; i < pAppReq->argc; i++) {
            if (!wasm_runtime_validate_app_str_addr(pWasmModuleInst, appArgvPointers[i]))
                return UVWASI_EFAULT;
            hostArgv.push_back((char*)wasm_runtime_addr_app_to_native(pWasmModuleInst, appArgvPointers[i]));
        }
        std::vector<wasi::wamr_spawn_action_base*> appSpawnActions;
        for (auto appPointer = pAppReq->app_fa_pointer; appPointer;) {
            wasi::wamr_spawn_action_base* pHostPointer = static_cast<wasi::wamr_spawn_action_base*>(wasm_runtime_addr_app_to_native(
                    pWasmModuleInst, appPointer));
            if (!pHostPointer || !wasm_runtime_validate_native_addr(pWasmModuleInst, pHostPointer, sizeof(wasi::wamr_spawn_action_base)))
                return UVWASI_EFAULT;
            size_t validateSize = 0;
            switch (pHostPointer->cmd) {
                case __WAMR_WASI_SPAWN_ACTION_FDUP2:
                    validateSize = sizeof(wasi::wamr_spawn_action_fdup2);
                    break;
            }
            if (validateSize > 0 && !wasm_runtime_validate_native_addr(pWasmModuleInst, pHostPointer, validateSize))
                return UVWASI_EFAULT;
            appSpawnActions.push_back(pHostPointer);
            appPointer = pHostPointer->app_next_action_pointer;
        }
#ifndef _WIN32
        hostArgv.push_back(nullptr);
        // Allow redirection for stdin, stdout and stderr only
        uv_os_fd_t dup2FDArr[3];
        for (int32_t fd = 0; fd < sizeof(dup2FDArr) / sizeof(uv_os_fd_t); fd++) {
            uv_os_fd_t& hostFD = dup2FDArr[fd];
            hostFD = -1;
            Utility::GetHostFDByAppFD(pWasmModuleInst, fd, hostFD);
        }
        for (const auto& pAppAction : appSpawnActions) {
            switch (pAppAction->cmd) {
                case __WAMR_WASI_SPAWN_ACTION_FDUP2: {
                    wasi::wamr_spawn_action_fdup2* pAppFDup2Action = static_cast<wasi::wamr_spawn_action_fdup2*>(pAppAction);
                    if (pAppFDup2Action->dest_fd < 0 || pAppFDup2Action->dest_fd >= sizeof(dup2FDArr) / sizeof(uv_os_fd_t))
                        break;
                    uv_os_fd_t hostFD;
                    auto err = Utility::GetHostFDByAppFD(pWasmModuleInst, pAppFDup2Action->src_fd, hostFD);
                    if (err != 0)
                        return err;
                    dup2FDArr[pAppFDup2Action->dest_fd] = hostFD;
                    break;
                }
            }
        }
        posix_spawn_file_actions_t hostFA;
        int32_t err = posix_spawn_file_actions_init(&hostFA);
        if (err != 0)
            return Utility::ConvertErrnoToWasiErrno(err);
        pid_t childPID;
        do {
            for (int32_t fd = 0; fd < sizeof(dup2FDArr) / sizeof(uv_os_fd_t); fd++) {
                if (dup2FDArr[fd] != -1 && (err = posix_spawn_file_actions_adddup2(&hostFA, dup2FDArr[fd], fd)) != 0)
                    break;
            }
            if (err != 0)
                break;
            int pipeFD[2];
            {
                std::lock_guard<std::mutex> _spawnAL(m_gProcessSpawnLock);
                // Create pipes to detect if the child process is alive
                if (pipe(pipeFD) != 0) {
                    err = errno;
                    break;
                }
                posix_spawn_file_actions_addclose(&hostFA, pipeFD[0]);
                err = posix_spawnp(&childPID, cmdName, &hostFA, nullptr, (char **) hostArgv.data(), environ);
                close(pipeFD[1]);
                if (err != 0) {
                    close(pipeFD[0]);
                    break;
                } else {
                    int tempFlags = fcntl(pipeFD[0], F_GETFL, 0);
                    if (tempFlags != -1)
                        fcntl(pipeFD[0], F_SETFL, tempFlags | O_NONBLOCK);
                }
            }
            std::lock_guard<std::mutex> _procManagerAL(pProcManager->lock);
            auto itPair = pProcManager->childProcMap.insert({childPID, ProcManager::ChildProcInfo()});
            itPair.first->second.pollPipeFD = pipeFD[0];
            pProcManager->pollFDToProcMap[pipeFD[0]] = itPair.first;
        } while (false);
        posix_spawn_file_actions_destroy(&hostFA);
        if (err != 0)
            return Utility::ConvertErrnoToWasiErrno(err);
        pAppReq->ret_pid = childPID;
        return 0;
#else
#error "Spawning process doesn't support Win32 now"
#endif
    }

#define __WASI_WNOHANG 1

    int32_t WasiProcessExt::ProcessWaitPID(wasm_exec_env_t pExecEnv, int32_t pid, int32_t opt, int32_t *exitStatus, int32_t *retPID) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        auto* pWamrExtInst = (WamrExtInstance*)wasm_runtime_get_custom_data(pWasmModuleInst);
        auto* pProcManager = &pWamrExtInst->wasiProcessManager;
#ifndef _WIN32
        int32_t err = 0;
        while (true) {
            std::vector<pollfd> procPollFDs;
            {
                std::lock_guard<std::mutex> _al(pProcManager->lock);
                if (pid > 0) {
                    auto it = pProcManager->childProcMap.find(pid);
                    if (it == pProcManager->childProcMap.end())
                        return UVWASI_ECHILD;
                    procPollFDs.emplace_back();
                    procPollFDs.back().fd = it->second.pollPipeFD;
                } else {
                    if (pProcManager->childProcMap.empty())
                        return UVWASI_ECHILD;
                    for (const auto &it: pProcManager->childProcMap) {
                        procPollFDs.emplace_back();
                        procPollFDs.back().fd = it.second.pollPipeFD;
                    }
                }
            }
            for (auto& pollFD : procPollFDs) {
                pollFD.revents = 0;
                pollFD.events = POLLIN;
            }
            int timeout = INT_MAX;
            if (opt & __WASI_WNOHANG)
                timeout = 0;
            int pollret = poll(procPollFDs.data(), procPollFDs.size(), timeout);
            if (pollret == -1) {
                err = errno;
                break;
            } else if (pollret == 0) {
                *retPID = 0;
                break;
            } else if (pollret > 0) {
                *retPID = 0;
                for (auto& pollFD : procPollFDs) {
                    if ((pollFD.revents & POLLIN) || (pollFD.revents & POLLHUP)) {
                        uint8_t _d[128];
                        read(pollFD.fd, _d, sizeof(_d));
                        std::lock_guard<std::mutex> _al(pProcManager->lock);
                        auto it = pProcManager->pollFDToProcMap.find(pollFD.fd);
                        if (it != pProcManager->pollFDToProcMap.end()) {
                            int childProcStatus = 0;
                            pid_t waitret = waitpid(it->second->first, &childProcStatus, WNOHANG);
                            if (waitret == it->second->first || (waitret == -1 && errno == ECHILD)) {
                                if (waitret == it->second->first) {
                                    *exitStatus = WEXITSTATUS(childProcStatus);
                                    *retPID = waitret;
                                } else {
                                    assert(false);
                                    // The child process may exit and its status may be reported by other wait()/waitpid() calls.
                                    // Assume that it has been exited with code 0 here
                                    *exitStatus = 0;
                                    *retPID = it->second->first;
                                }
                                close(pollFD.fd);
                                pProcManager->childProcMap.erase(it->second);
                                pProcManager->pollFDToProcMap.erase(it);
                                assert(pProcManager->childProcMap.size() == pProcManager->pollFDToProcMap.size());
                            }
                        } else {
                            assert(false);
                            // The pipe FD may be closed
                        }
                    }
                    if (*retPID != 0)
                        break;
                }
                if (*retPID != 0) {
                    assert(pid <= 0 || pid == *retPID);
                    break;
                } else if (timeout == 0) {
                    break;
                }
            }
        }
        return Utility::ConvertErrnoToWasiErrno(err);
#else
#error "Waiting child processes doesn't support Win32 now"
#endif
    }
}
