#include <wamr_ext_api.h>
#include "../wamr_ext_lib/PthreadExt.h"

WamrExtErrno wamr_ext_init() {
    wamr_ext::PthreadExt::Init();
    return WAMR_EXT_NO_ERROR;
}
