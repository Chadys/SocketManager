#ifndef SOCKETMANAGER_SOCKETHELPERCLASSES_H
#define SOCKETMANAGER_SOCKETHELPERCLASSES_H

#include <list>
#include <queue>
#include <unordered_map>
#include "socket_headers.h"
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
class CriticalRecyclableList : public CriticalContainerWrapper {    //prevent std::list malloc overhead by keeping a second list to store deleted element for later reuse
private:
    size_t                      max_recycled_size;
    std::list<T>                recycle_list;
public:
    std::list<T>                list;

    explicit                            CriticalRecyclableList<T>   (size_t s = 250) : CriticalContainerWrapper(), max_recycled_size(s) {}

    void                                erase                       (typename std::list<T>::iterator it){
        EnterCriticalSection(&critSec);
        {
            if (recycle_list.size() >= max_recycled_size) {
                list.erase(it);
            } else {
                recycle_list.splice(recycle_list.end(), list, it);
            }
        }
        LeaveCriticalSection(&critSec);
    }

    template<typename... Args>
    typename std::list<T>::iterator     create                      (Args&&... args ){
        EnterCriticalSection(&critSec);
        //{
        if (recycle_list.empty()) {
            list.emplace_back(args...);
        } else {
            list.splice(list.end(), recycle_list, recycle_list.begin());
            list.back() = T(args...);
        }
        auto newEltIt = --list.end();
        //}
        LeaveCriticalSection(&critSec);
        return newEltIt;
    }
};

template<typename T>
class CriticalQueue : public CriticalContainerWrapper {
public:
    std::queue<T>                queue;
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
    explicit        ListElt     (CriticalRecyclableList<T> &l)                    : critList(l) {}

    CriticalRecyclableList<T>       &critList;          // Reference to container of this element
    typename std::list<T>::iterator it;                 // Iterator to itself to manipulate object in queue
public:
    template<typename... Args>
    static T*       Create      (CriticalRecyclableList<T> &l, Args&&... args) {      // Factory function to return pointer to newly created element inside critList
        auto newEltIt = l.create(l, args...);
        T& newElt = *newEltIt;
        newElt.it = newEltIt;
        return &newElt;
    }

    static void     Delete      (T *obj) {                                  // Delete the object from its container, pointer becomes invalid
        obj->critList.erase(obj->it);
    }

    static void     ClearList   (CriticalRecyclableList<T> &critList) {     // Clear given list by securely calling Delete on each element
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
    Socket(CriticalRecyclableList<Socket> &l, SocketManager *c, SOCKET s_, int af_) : ListElt(l), id(), address(""), port(0),
                                                                            s(s_), af(af_), state(SocketState::INIT),
                                                                            OutstandingRecv(0), OutstandingSend(0), pendingByteSent(0),
                                                                            SockCritSec{}, client(c), timeWaitStartTime(0) {
        InitializeCriticalSection(&SockCritSec);
    }
    ~Socket(){
        DeleteCriticalSection(&SockCritSec);
    }

    Socket& operator=(const Socket& sock){
        id = sock.id;
        s = sock.s;
        address = sock.address;
        port = sock.port;
        state = sock.state;
        af = sock.af;
        OutstandingRecv = sock.OutstandingRecv;
        OutstandingSend = sock.OutstandingSend;
        pendingByteSent = sock.pendingByteSent;
        SockCritSec = sock.SockCritSec;
        client = sock.client;
        timeWaitStartTime = sock.timeWaitStartTime;
        critList = sock.critList;
        it = sock.it;
        return *this;
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
    volatile LONG64             pendingByteSent;        // keep track of pending byte sent
    CRITICAL_SECTION            SockCritSec;            // Protect access to this structure
    SocketManager*              client;                 // Pointer to containing class
    DWORD                       timeWaitStartTime;      // Counter to test if socket has gotten out of TIME_WAIT state after a disconnect
    ULONG                       maxPendingByteSent;     // Max pending byte sent calculated using ISB, used as threshold to prevent more send if memory becomes limited

    static void     Delete                  (Socket *obj);                                          // Close socket before deleting it
    static void     DeleteOrDisconnect      (Socket *obj, CriticalMap<UUID, Socket*> &critMap);     // Try to disconnect socket for reuse or close and delete it if is is not possible
    void            Disconnect              (CriticalMap<UUID, Socket*> &critMap);                  // Disconnect socket so it can be used again
    void            Close                   (bool forceClose);                                      // Permanently close connexion

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
        ISBChange,
        End
    };

public:
    explicit Buffer(CriticalRecyclableList<Buffer> &l)                              : Buffer(l, Operation::Read) {}
    Buffer(CriticalRecyclableList<Buffer> &l, Operation op)                         : ListElt(l),
                                                                                      ol{}, buf(), bufLen(DEFAULT_BUFFER_SIZE),
                                                                                      operation(op) {}

    Buffer& operator=(const Buffer& buff){
        ol = buff.ol;
        operation = buff.operation;
        critList = buff.critList;
        it = buff.it;
        return *this;
    }

private:

    static const u_long         DEFAULT_BUFFER_SIZE     = 4096;

    WSAOVERLAPPED               ol;
    char                        buf[DEFAULT_BUFFER_SIZE];   // Buffer for recv/send
    u_long                      bufLen;
    Operation                   operation;                  // Type of operation issued

};
////////////// Buffer ////////////


/************* Explicit Template Declaration ***********/

template class ListElt<Buffer>;
template class ListElt<Socket>;

////////////// Explicite Template Declaration ////////////

#endif //SOCKETMANAGER_SOCKETHELPERCLASSES_H