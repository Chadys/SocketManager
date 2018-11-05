#ifndef SOCKETMANAGER_MISC_H
#define SOCKETMANAGER_MISC_H

#include <string>
#include <windows.h>

#define LOG(text...) setbuf(stdout, 0); printf("%s : ", __func__); printf(text);

namespace Misc {
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, std::basic_string<TCHAR> &valueFromRegistry);
    int GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, DWORD &valueFromRegistry);
    inline UUID CreateNilUUID() { UUID nullId; UuidCreateNil(&nullId); return nullId; }
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
            return static_cast<bool>(UuidEqual(const_cast<UUID*>(&uid1), const_cast<UUID*>(&uid2), &status));
        }
    };

}
////////////// StdExtention ////////////

#endif //SOCKETMANAGER_MISC_H
