#ifndef SOCKETCLIENT_MISC_H
#define SOCKETCLIENT_MISC_H

#include <string>
#include <windows.h>

namespace Misc {
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, std::basic_string<TCHAR> &valueFromRegistry);
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, DWORD &valueFromRegistry);
}

#endif //SOCKETCLIENT_MISC_H
