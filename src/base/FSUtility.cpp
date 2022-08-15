#include "FSUtility.h"

namespace WAMR_EXT_NS {
    std::filesystem::path FSUtility::GetTempDir() {
#ifndef _WIN32
        const char* tmpdir = "";
        for (const char* tempdirEnvVar : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
            tmpdir = getenv(tempdirEnvVar);
            if (tmpdir && tmpdir[0])
                break;
        }
        if (!tmpdir || !tmpdir[0]) {
#ifdef __ANDROID__
            tmpdir = "/data/local/tmp";
#else
            tmpdir = "/tmp";
#endif
        }
        return tmpdir;
#else
        TCHAR buf[MAX_PATH + 2];
        DWORD len = GetTempPath(sizeof(buf) / sizeof(TCHAR) - 1, buf);
        if (len > 0) {
            buf[len] = 0;
            return buf;
        }
        return "";
#endif
    }
}
