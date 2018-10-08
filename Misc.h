#ifndef SOCKETCLIENT_MISC_H
#define SOCKETCLIENT_MISC_H

#include <string>
#include <windows.h>

namespace Misc {
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, std::basic_string<TCHAR> &valueFromRegistry);
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, DWORD &valueFromRegistry);
}
/************* StdExtention ***********/
namespace std
{
    template<>
    struct hash<UUID>
    {
        size_t operator () (const UUID& uid) const
        {
            RPC_STATUS status;
            return UuidHash(const_cast<UUID*>(&uid), &status);
        }
    };

    template<>
    struct equal_to<UUID>
    {
        bool operator () (const UUID& uid1, const UUID& uid2) const
        {
            RPC_STATUS status;
            return UuidEqual (const_cast<UUID*>(&uid1), const_cast<UUID*>(&uid2), &status);
        }
    };

}
////////////// StdExtention ////////////

#endif //SOCKETCLIENT_MISC_H
