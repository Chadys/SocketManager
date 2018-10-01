#ifndef SOCKETCLIENT_SOCKETCLIENT_H
#define SOCKETCLIENT_SOCKETCLIENT_H
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <conio.h>
#include <lm.h>
#include <cstdio>
#include <string>
#include <list>
#include <vector>

#define DEBUG

class SocketClient {                            // Manage a client connected to an arbitrary number of socket server
private:
    /************************ Attributes **************************/

    static const int        DEFAULT_BUFFER_SIZE     = 4096;
    static const int        FAMILY                  = AF_INET;      // IPv4 address family.
    static const int        THREADS_PER_PROC        = 1;

    //////////////////////// End Attributes ///////////////////////

    /************************ Internal def **************************/

    enum State {
        NOT_INITIALIZED,
        WSA_INITIALIZED,
        IOCP_INITIALIZED,
        CRIT_SEC_INITIALIZED,
        THREADS_INITIALIZED,
        MSWSOCK_FUNC_INITIALIZED,
        READY
    };

    enum Operation {
        Read,
        Write,
        Connect,
        Disconnect,
        Accept,
        End
    };

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
    class ListElt {                             // Object that manage itself inside its container

    protected:
        explicit        ListElt     (CriticalList<T> &l)                    : critList(l) {}

        CriticalList<T>&                critList;           // Reference to container of this element
    private:
        typename std::list<T>::iterator it;                 // Iterator to itself to manipulate object in queue
    public:
        template<typename... Args>
        static T*       Create      (CriticalList<T> &l, Args&&... args);       // Factory function to return pointer to newly created element inside critList
        static void     Delete      (T *obj);                                   // Delete the object from its container, pointer becomes invalid
        static void     ClearList   (CriticalList<T> &critList);                       // Clear given list by securely calling Delete on each element

    };
    ////////////// ListElt ////////////

public:
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
        static void Delete(Socket *obj);        // Close socket before deleting it

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
        SocketClient*               client;

    };
    ////////////// Socket ////////////

private:
    /************* Buffer ***********/
    class Buffer : public ListElt<Buffer> {     // Used as a read or write buffer for overlapped operations
        friend class SocketClient;

    public:
        explicit Buffer(CriticalList<Buffer> &l)                              : Buffer(l, Operation::Read) {}
        Buffer(CriticalList<Buffer> &l, Operation op)                         : ListElt(l),
                                                                                ol{}, buf(), buflen(DEFAULT_BUFFER_SIZE),
                                                                                operation(op) {}

    private:
        WSAOVERLAPPED               ol;
        char                        buf[DEFAULT_BUFFER_SIZE]; // Buffer for recv/send
        size_t                      buflen;
        Operation                   operation;              // Type of operation issued

    };
    ////////////// Buffer ////////////

    //////////////////////// End Internal def ///////////////////////

    /************************ Attributes **************************/

    CriticalList<Buffer>    inUseBufferList;            // All buffers currently used in an overlapped operation
    CriticalList<Socket>    inUseSocketList;            // All sockets this instance is currently connected to

//    volatile LONG           bytesRead               = 0,
//                            bytesSent               = 0;
    State                   state;                      // Current state of this instance, used for cleanup and to test readiness
    std::vector<HANDLE>     threadHandles;              // Handles to all threads receiving IOCP events
    HANDLE                  iocpHandle;                 // Handle to IO completion port

    LPFN_CONNECTEX          ConnectEx               = nullptr;
    LPFN_DISCONNECTEX       DisconnectEx            = nullptr;
    LPFN_ACCEPTEX           AcceptEx                = nullptr;

    //////////////////////// End Attributes ///////////////////////

    /************************ Methods **************************/

    static DWORD WINAPI IOCPWorkerThread    (LPVOID lpParam);                                       // Per-thread function receiving IOCP events

    void                HandleError         (Socket *sockobj, Buffer *buf, DWORD error);            // Manage one IOCP error
    void                HandleIo            (Socket *sockobj, Buffer *buf, DWORD BytesTransfered);  // Manage one IOCP event, calling all needed functions
    int                 PostRecv            (Socket *sock, Buffer *recvobj);                        // Post an overlapped recv operation on the socket
    int                 PostSend            (Socket *sock, Buffer *sendobj);                        // Post an overlapped send operation on the socket
    void                ClearThreads        ();                                                     // Tells all working threads to shut down and free resources
    bool                InitAsyncSocketFuncs();                                                     // Initialize function pointer to needed mswsock functions
    bool                InitAsyncSocketFunc (SOCKET sock, GUID guid, LPVOID func, DWORD size);      // Initialize function pointer to one mswsock function

public:
                        SocketClient        ();
                        ~SocketClient       ();
    Socket*             ListenToNewSocket   (const char *address, u_short port);                    // Start listening to new read/write event on this socket
    inline bool         isReady             () const                                                { return state == State::READY; };
    void                SendData            (const char *data, size_t len, Socket *socket);         // Send a given buffer to the given socket
    virtual int         ReceiveData         (const char* data, size_t length, Socket *socket);      // Do what needs to be done when receiving content from a socket //TODO make pure virtual
    /* Implementation recommendation :
     * Since a single read can be received by different threads,
     * keep a map socket->buffer as attribute inside your implementing class
     * override ReceiveData to fill this buffer
     * (using a CRITICAL_SECTION for the whole map or a per-entry one depending on the number of socket you expect)
     * and send an event to another thread(s) managing this map if the end of the buffer was detected
     * (the detection depends on your communication protocol)
     * */

    //////////////////////// End Methods ///////////////////////
};

#endif //SOCKETCLIENT_SOCKETCLIENT_H
