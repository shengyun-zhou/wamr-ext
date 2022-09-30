#ifndef WAMR_EXT_WAMR_EXT_API_H
#define WAMR_EXT_WAMR_EXT_API_H

#include <stdint.h>

#ifndef WAMR_EXT_STATIC_LIB
#ifdef _WIN32
#define WAMR_EXT_API __declspec(dllexport)
#else
#define WAMR_EXT_API __attribute__ ((visibility ("default")))
#endif
#else
#define WAMR_EXT_API
#endif

struct WamrExtModule;
typedef struct WamrExtModule* wamr_ext_module_t;
struct WamrExtInstance;
typedef struct WamrExtInstance* wamr_ext_instance_t;

#ifdef __cplusplus
extern "C" {
#endif

enum WamrExtInstanceOpt {
    // Add host dir for WAsm app, value type: WamrExtKeyValueSS*
    WAMR_EXT_INST_OPT_ADD_HOST_DIR = 2,
    // Add environment variable for WAsm app, value type: WamrExtKeyValueSS*
    WAMR_EXT_INST_OPT_ADD_ENV_VAR = 3,
    // Set arguments that will be passed to main(), value type: char**(ends with NULL)
    WAMR_EXT_INST_OPT_ARG = 4,
    // Set callback when WAsm app raises exception during running(e.g. out of memory access), value type: WamrExtInstanceExceptionCB*
    WAMR_EXT_INST_OPT_EXCEPTION_CALLBACK = 5,
    // Add a host command to whitelist to allow WAsm app to execute it.
    // Value type: WamrExtKeyValueSS*, where key is the command name(NOT command path) used by WAsm app, value is the mapped host command name or path.
    // e.g. ping -> ping, uname -> /usr/bin/uname
    WAMR_EXT_INST_OPT_ADD_HOST_COMMAND = 6,
    // Set maximum memory size(bytes), value type: uint32_t*
    WAMR_EXT_INST_OPT_MAX_MEMORY = 7,
};

struct WamrExtKeyValueSS {
    const char* k;
    const char* v;
};

struct WamrExtExceptionInfo;
typedef struct WamrExtExceptionInfo wamr_ext_exception_info_t;

enum WamrExtExceptionInfoEnum {
    // Get error code of the exception, value type: int32_t*
    WAMR_EXT_EXCEPTION_INFO_ERROR_CODE = 1,
    // Get error string info of the exception, value type: char**,
    WAMR_EXT_EXCEPTION_INFO_ERROR_STRING = 2,
};

struct WamrExtInstanceExceptionCB {
    void(*func)(wamr_ext_instance_t* inst, wamr_ext_exception_info_t* exception_info, void* user_data);
    void* user_data;
};

WAMR_EXT_API void wamr_ext_version(const char** ver_str, uint32_t* ver_code);
WAMR_EXT_API int32_t wamr_ext_init();
WAMR_EXT_API int32_t wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* module_name, const char* file_path);
WAMR_EXT_API int32_t wamr_ext_module_load_by_buffer(wamr_ext_module_t* module, const char* module_name, const uint8_t* buf, uint32_t len);
WAMR_EXT_API int32_t wamr_ext_module_set_inst_default_opt(wamr_ext_module_t* module, enum WamrExtInstanceOpt opt, const void* value);
WAMR_EXT_API int32_t wamr_ext_instance_create(wamr_ext_module_t* module, wamr_ext_instance_t* inst);
WAMR_EXT_API int32_t wamr_ext_instance_set_opt(wamr_ext_instance_t* inst, enum WamrExtInstanceOpt opt, const void* value);
WAMR_EXT_API int32_t wamr_ext_instance_start(wamr_ext_instance_t* inst);
WAMR_EXT_API int32_t wamr_ext_instance_exec_main_func(wamr_ext_instance_t* inst, int32_t* ret_value);
WAMR_EXT_API int32_t wamr_ext_instance_destroy(wamr_ext_instance_t* inst);

WAMR_EXT_API const char* wamr_ext_strerror(int32_t err);
WAMR_EXT_API int32_t wamr_ext_exception_get_info(wamr_ext_exception_info_t* exception, enum WamrExtExceptionInfoEnum info, void* value);

#ifdef __cplusplus
}
#endif

#endif //WAMR_EXT_WAMR_EXT_API_H
