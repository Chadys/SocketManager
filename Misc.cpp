#include "Misc.h"

int Misc::GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, std::basic_string<TCHAR> &valueFromRegistry) {
    int     err;
    DWORD   cbData;
    DWORD   bufferSize = 512; // If too small, will be resized down below.

    do {
        valueFromRegistry.resize(bufferSize);
        cbData = bufferSize;
        err = RegGetValue(HKEY_LOCAL_MACHINE,                              //hkey : A handle to an open registry key. HKEY_LOCAL_MACHINE -> is predefined.
                          regSubKey,                                       //lpSubKey : The name of the registry key. This key must be a subkey of the key specified by the hkey parameter. Key names are not case sensitive.
                          regValue,                                        //lpValue : The name of the registry value. If this parameter is NULL or an empty string, "", the function retrieves the type and data for the key's unnamed or default value, if any.
                          RRF_RT_REG_SZ,                                   //dwFlags : The flags that restrict the data type of value to be queried. If the data type of the value does not meet this criteria, the function fails.
                          nullptr,                                         //pdwType : A pointer to a variable that receives a code indicating the type of data stored in the specified value. This parameter can be NULL if the type is not required.
                          static_cast<void*>(valueFromRegistry.data()),    //pvData : A pointer to a buffer that receives the value's data. If the data is a string, the function checks for a terminating null character. If one is not found, the string is stored with a null terminator if the buffer is large enough to accommodate the extra character. Otherwise, the function fails and returns ERROR_MORE_DATA.
                          &cbData);                                        //pcbData : A pointer to a variable that specifies the size of the buffer pointed to by the pvData parameter, in bytes. When the function returns, this variable contains the size of the data copied to pvData. If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, this size includes any terminating null character or characters.

        // Get a buffer that is big enough.
        cbData /= sizeof(TCHAR);
        if (cbData > bufferSize) {
            bufferSize = cbData;
        } else {
            bufferSize *= 2;
        }
    } while (err == ERROR_MORE_DATA);
    if (err != ERROR_SUCCESS) {
        cbData = 0;
    }
    valueFromRegistry.resize(cbData);
    return err;
}

int Misc::GetRegistryValue(const TCHAR *regSubKey, const TCHAR *regValue, DWORD &valueFromRegistry) {
    DWORD   cbData = sizeof(DWORD);

    return RegGetValue(HKEY_LOCAL_MACHINE,
                       regSubKey,
                       regValue,
                       RRF_RT_REG_DWORD,
                       nullptr,
                       static_cast<void*>(&valueFromRegistry),
                       &cbData);
}