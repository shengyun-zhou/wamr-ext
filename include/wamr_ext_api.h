#ifndef WAMR_EXT_WAMR_EXT_API_H
#define WAMR_EXT_WAMR_EXT_API_H

#ifndef WAMR_EXT_STATIC_LIB
#ifdef _WIN32
#define WAMR_EXT_API __declspec(dllexport)
#else
#define WAMR_EXT_API __attribute__ ((visibility ("default")))
#endif
#else
#define WAMR_EXT_API
#endif

#define WAMR_INVALID_MODULE (-1)
typedef int WAMR_EXT_MODULE_ID;

#ifdef __cplusplus
extern "C" {
#endif

enum WamrExtErrno {
    WAMR_EXT_NO_ERROR = 0,
};

WAMR_EXT_API enum WamrExtErrno wamr_ext_init();

#ifdef __cplusplus
}
#endif

#endif //WAMR_EXT_WAMR_EXT_API_H
