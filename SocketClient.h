#ifndef SOCKETCLIENT_SOCKETCLIENT_H
#define SOCKETCLIENT_SOCKETCLIENT_H

#include "SocketHelperClasses.h"
#include <vector>

#define DEBUG
#ifndef DEBUG
#define _cprintf(...) if(false);
#endif

class SocketClient {                            // Manage a client connected to an arbitrary number of socket server
    friend class Socket;

private:
    /************************ Attributes **************************/

    static const int        FAMILY                  = AF_INET;      // IPv4 address family.
    static const int        THREADS_PER_PROC        = 1;
    static const int        MAX_UNUSED_SOCKET       = 1; //TODO

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

    //////////////////////// End Internal def ///////////////////////

    /************************ Attributes **************************/

    CriticalList<Buffer>        inUseBufferList;            // All buffers currently used in an overlapped operation
    CriticalList<Socket>        inUseSocketList;            // All sockets this instance is currently connected to
    CriticalList<Socket*>       reusableSocketList;         // All sockets previously disconnected that can be recycled

//    volatile LONG               bytesRead               = 0,
//                                bytesSent               = 0;
    State                       state;                      // Current state of this instance, used for cleanup and to test readiness
    std::vector<HANDLE>         threadHandles;              // Handles to all threads receiving IOCP events
    HANDLE                      iocpHandle;                 // Handle to IO completion port

    static LPFN_CONNECTEX       ConnectEx;
    static LPFN_DISCONNECTEX    DisconnectEx;
    static LPFN_ACCEPTEX        AcceptEx;

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
    Socket*             CreateSocket        (SOCKET sock);                                          // Try to recycle a disconnected socket, or create a new one
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
