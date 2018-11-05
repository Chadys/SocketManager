#ifndef SOCKETMANAGER_SOCKETHELPERCLASSES_H
#define SOCKETMANAGER_SOCKETHELPERCLASSES_H

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <conio.h>
#include <list>
#include <unordered_map>
#include "Misc.h"
#include "SocketManager.h"

class SocketManager;

/*********** CriticalContainers *********/                        // Practical class to gather up a container and its critical section
class CriticalContainerWrapper{
public:
    CRITICAL_SECTION            critSec{};
    CriticalContainerWrapper    ()  { InitializeCriticalSection(&critSec); }
    ~CriticalContainerWrapper   ()  { DeleteCriticalSection(&critSec); }
};

template<typename T>
class CriticalList : public CriticalContainerWrapper {
public:
    std::list<T>                list;

};
template<typename T, typename U>
class CriticalMap : public CriticalContainerWrapper {

public:
    std::unordered_map<T,U>     map;

    U           Get     (T key) {        //convenient way to access an element
        U value = U();
        EnterCriticalSection(&critSec);
        {
            auto it = map.find(key);
            if (it != map.end())
                value = it->second;
        }
        LeaveCriticalSection(&critSec);
        return value;
    }
};
//////////// CriticalContainers //////////

/************* ListElt ***********/
template<typename T>
class ListElt {                             // Object that manage itself inside its own container

protected:
    explicit        ListElt     (CriticalList<T> &l)                    : critList(l) {}

    CriticalList<T>                 &critList;          // Reference to container of this element
    typename std::list<T>::iterator it;                 // Iterator to itself to manipulate object in queue
public:
    template<typename... Args>
    static T*       Create      (CriticalList<T> &l, Args&&... args) {      // Factory function to return pointer to newly created element inside critList
        EnterCriticalSection(&l.critSec);
        //{
            l.list.emplace_back(l, args...);
            T &newElt = l.list.back();
            newElt.it = --l.list.end();
        //}
        LeaveCriticalSection(&l.critSec);
        return &newElt;
    }

    static void     Delete      (T *obj) {                                  // Delete the object from its container, pointer becomes invalid

        auto &critList = obj->critList;
        EnterCriticalSection(&critList.critSec);
        {
            obj->critList.list.erase(obj->it);
        }
        LeaveCriticalSection(&critList.critSec);
    }

    static void     ClearList   (CriticalList<T> &critList) {               // Clear given list by securely calling Delete on each element
        while (true) {
            EnterCriticalSection(&critList.critSec);
            //{
            if (critList.list.empty()) {
                LeaveCriticalSection(&critList.critSec);
                break;
            }
            T &elt = critList.list.front();
            //}
            LeaveCriticalSection(&critList.critSec);
            T::Delete(&elt);
        }
    }

};
////////////// ListElt ////////////


/************* Socket ***********/
class Socket : public ListElt<Socket> {     // Contains all needed information about one socket
    friend class SocketManager;
    friend class ListElt;

public:
    Socket(CriticalList<Socket> &l, SocketManager *c, SOCKET s_, int af_) : ListElt(l), id(), address(""), port(0),
                                                                            s(s_), af(af_), state(SocketState::INIT),
                                                                            OutstandingRecv(0), OutstandingSend(0),
                                                                            SockCritSec{}, client(c), timeWaitStartTime(0) {
        InitializeCriticalSection(&SockCritSec);
    }
    ~Socket(){
        DeleteCriticalSection(&SockCritSec);
    }

private:
    enum SocketState {
        INIT,
        ASSOCIATED,
        BOUND,
        DISCONNECTED,
        LISTENING,
        ACCEPTING,
        RETRY_CONNECTION,
        CONNECT_FAILURE,
        CONNECTED,
        CLOSING,
        FAILURE,
        DISCONNECTING,
        CLOSED
    };

    UUID                        id;                     // Socket unique identifier (used to store it in a map
    SOCKET                      s;                      // Socket handle
    const char *                address;                // IP address of connection
    u_short                     port;                   // Port of connection
    SocketState                 state;                  // State the socket is in
    int                         af;                     // Address family of socket
    volatile LONG               OutstandingRecv,        // Number of outstanding overlapped ops on
                                OutstandingSend;
    CRITICAL_SECTION            SockCritSec;            // Protect access to this structure
    SocketManager*              client;                 // Pointer to containing class
    DWORD                       timeWaitStartTime;      // Counter to test if socket has gotten out of TIME_WAIT state after a disconnect

    static void     Delete                  (Socket *obj);                                          // Close socket before deleting it
    static void     DeleteOrDisconnect      (Socket *obj, CriticalMap<UUID, Socket*> &critMap);     // Close socket before deleting it
    void            Disconnect              (CriticalMap<UUID, Socket*> &critMap);                  // Disconnect socket so it can be used again
    void            Close                   (bool forceClose);                                                     // Permanently close connexion

};
////////////// Socket ////////////


/************* Buffer ***********/
class Buffer : public ListElt<Buffer> {     // Used as a read or write buffer for overlapped operations
    friend class SocketManager;
    friend class Socket;

private:
    enum Operation {
        Read,
        Write,
        Connect,
        Disconnect,
        Accept,
        End
    };

public:
    explicit Buffer(CriticalList<Buffer> &l)                              : Buffer(l, Operation::Read) {}
    Buffer(CriticalList<Buffer> &l, Operation op)                         : ListElt(l),
                                                                            ol{}, buf(), bufLen(DEFAULT_BUFFER_SIZE),
                                                                            operation(op) {}

private:

    static const u_long         DEFAULT_BUFFER_SIZE     = 4096;

    WSAOVERLAPPED               ol;
    char                        buf[DEFAULT_BUFFER_SIZE]; // Buffer for recv/send
    u_long                      bufLen;
    Operation                   operation;              // Type of operation issued

};
////////////// Buffer ////////////


/************* Explicit Template Declaration ***********/

template class ListElt<Buffer>;
template class ListElt<Socket>;

////////////// Explicite Template Declaration ////////////

#endif //SOCKETMANAGER_SOCKETHELPERCLASSES_H
