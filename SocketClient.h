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

    static const int            FAMILY                      = AF_INET;      // IPv4 address family.
    static const int            THREADS_PER_PROC            = 1;
    static const int            MAX_UNUSED_SOCKET           = 1; //TODO
    static const int            DEFAULT_TIME_WAIT_VALUE     = 120000;       // Either 120 or 240sec depending on doc page, but tests confirmed 120 (https://docs.microsoft.com/en-us/biztalk/technical-guides/settings-that-can-be-modified-to-improve-network-performance | https://docs.microsoft.com/en-us/previous-versions/windows/it-pro/windows-2000-server/cc938217(v=technet.10))
    static const int            MIN_TIME_WAIT_VALUE         = 30000;        // Range goes from 30 to 300sec according to microsoft doc
    static const int            MAX_TIME_WAIT_VALUE         = 300000;       // Range goes from 30 to 300sec according to microsoft doc
    static const TCHAR *        TIME_WAIT_REG_KEY;
    static const TCHAR *        TIME_WAIT_REG_VALUE;
    static DWORD                TimeWaitValue;
    static LPFN_CONNECTEX       ConnectEx;
    static LPFN_DISCONNECTEX    DisconnectEx;
    static LPFN_ACCEPTEX        AcceptEx;

    //////////////////////// End Attributes ///////////////////////

    /************************ Internal def **************************/

    enum State {
        NOT_INITIALIZED,
        WSA_INITIALIZED,
        IOCP_INITIALIZED,
        THREADS_INITIALIZED,
        MSWSOCK_FUNC_INITIALIZED,
        TIME_WAIT_VALUE_SELECTED,
        READY
    };

    //////////////////////// End Internal def ///////////////////////

    /************************ Attributes **************************/

    CriticalList<Buffer>        inUseBufferList;            // All buffers currently used in an overlapped operation
    CriticalList<Socket>        inUseSocketList;            // All sockets this instance is currently connected to
    CriticalList<Socket*>       reusableSocketList;         // All sockets previously disconnected that can be recycled //TODO use a queue instead
    CriticalMap<UUID, Socket*>  socketAccessMap;            // Only way to access a socket pointer from outside of this class, to prevent invalid memory access

    State                       state;                      // Current state of this instance, used for cleanup and to test readiness
    std::vector<HANDLE>         threadHandles;              // Handles to all threads receiving IOCP events
    HANDLE                      iocpHandle;                 // Handle to IO completion port

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
    void                InitTimeWaitValue   ();                                                     // Initialize TIME_WAIT detected value
    void                SendData            (const char *data, size_t len, Socket *socket);         // Send a given buffer to the given socket

public:
                        SocketClient        ();
                        ~SocketClient       ();
    Socket*             ReuseSocket         ();                                                     // Try to recycle a disconnected socket, or create a new one
    UUID                ListenToNewSocket   (const char *address, u_short port);                    // Start listening to new read/write event on this socket
    inline bool         isReady             () const                                                { return state == State::READY; };
    inline bool         isSocketReady       (UUID socketId)                                         { Socket *sockObj = socketAccessMap.Get(socketId); return sockObj != nullptr && sockObj->state == Socket::SocketState::CONNECTED; };
    inline void         SendData            (const char *data, size_t len, UUID socketId)           { return SendData(data, len, socketAccessMap.Get(socketId)); }
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
