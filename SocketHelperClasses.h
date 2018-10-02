#ifndef SOCKETCLIENT_SOCKETHELPERCLASSES_H
#define SOCKETCLIENT_SOCKETHELPERCLASSES_H

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <conio.h>
#include <list>

class SocketClient;

/*********** CriticalList *********/
template<typename T>
class CriticalList {                        // Practical class to gather up a list and its critical section

public:
    CRITICAL_SECTION            critSec;
    std::list<T>                list;

};
//////////// CriticalList //////////

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
    friend class SocketClient;

public:
    Socket(CriticalList<Socket> &l, SocketClient *c, SOCKET s_, int af_)  : ListElt(l),
                                                                            s(s_), af(af_), state(SocketState::INIT),
                                                                            OutstandingRecv(0), OutstandingSend(0),
                                                                            SockCritSec{}, client(c) {
        InitializeCriticalSection(&SockCritSec);
    }
    ~Socket(){
        DeleteCriticalSection(&SockCritSec);
    }
    static void     Delete      (Socket *obj);                            // Close socket before deleting it

private:
    enum SocketState {
        INIT,
        ASSOCIATED,
        BOUND,
        CONNECTED,
        CLOSING,
        FAILURE
    };

    SOCKET                      s;                      // Socket handle
    SocketState                 state;                  //state the socket is in
    int                         af;                     // Address family of socket
    volatile LONG               OutstandingRecv,        // Number of outstanding overlapped ops on
            OutstandingSend;
    CRITICAL_SECTION            SockCritSec;            // Protect access to this structure
    SocketClient*               client;                 // Pointer to containing class

    void            Disconnect  ();                                       // Disconnect socket so it can be used again

};
////////////// Socket ////////////


/************* Buffer ***********/
class Buffer : public ListElt<Buffer> {     // Used as a read or write buffer for overlapped operations
    friend class SocketClient;
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
                                                                            ol{}, buf(), buflen(DEFAULT_BUFFER_SIZE),
                                                                            operation(op) {}

private:

    static const int        DEFAULT_BUFFER_SIZE     = 4096;

    WSAOVERLAPPED               ol;
    char                        buf[DEFAULT_BUFFER_SIZE]; // Buffer for recv/send
    size_t                      buflen;
    Operation                   operation;              // Type of operation issued

};
////////////// Buffer ////////////

template class ListElt<Buffer>;
template class ListElt<Socket>;

#endif //SOCKETCLIENT_SOCKETHELPERCLASSES_H
