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
    // Set maximum thread number, value type: uint32_t*
    WAMR_INST_OPT_MAX_THREAD_NUM = 1,
    // Add host dir for WAsm app, value type: WamrKeyValueSS*
    WAMR_INST_OPT_ADD_HOST_DIR = 2,
    // Add environment variable for WAsm app, value type: WamrKeyValueSS*
    WAMR_INST_OPT_ADD_ENV_VAR = 3,
};

struct WamrKeyValueSS {
    const char* k;
    const char* v;
};

WAMR_EXT_API int32_t wamr_ext_init();
WAMR_EXT_API int32_t wamr_ext_module_load_by_file(wamr_ext_module_t* module, const char* file_path);
WAMR_EXT_API int32_t wamr_ext_module_load_by_buffer(wamr_ext_module_t* module, const uint8_t* buf, uint32_t len);
WAMR_EXT_API int32_t wamr_ext_module_set_inst_default_opt(wamr_ext_module_t* module, enum WamrExtInstanceOpt opt, const void* value);
WAMR_EXT_API int32_t wamr_ext_instance_create(wamr_ext_module_t* module, wamr_ext_instance_t* inst);
WAMR_EXT_API int32_t wamr_ext_instance_set_opt(wamr_ext_instance_t* inst, enum WamrExtInstanceOpt opt, const void* value);
WAMR_EXT_API int32_t wamr_ext_instance_init(wamr_ext_instance_t* inst);
WAMR_EXT_API int32_t wamr_ext_instance_run_main(wamr_ext_instance_t* inst, int32_t argc, char** argv);

WAMR_EXT_API const char* wamr_ext_strerror(int32_t err);

#ifdef __cplusplus
}
#endif

#endif //WAMR_EXT_WAMR_EXT_API_H
