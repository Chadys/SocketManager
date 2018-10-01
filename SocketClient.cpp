#include "SocketClient.h"

#ifndef DEBUG
#define _cprintf(...) if(false);
#endif

//TODO reusable socket with another list and Disconnectex
//TODO manage incomplete WSASend
// Post another WSASend() request.
// Since WSASend() is not guaranteed to send all of the bytes requested,
// continue posting WSASend() calls until all received bytes are sent.
//How you proceed from this point depends on the error (temporary resource limit or hard network fault) and how many other WSASends you have pending on that socket (zero or non-zero). You can only try and send the rest of the data if you have a temporary resource error and no other outstanding WSASend calls for this socket
//if you DO have other WSASend calls pending then you should probably abort the connection as you may have garbled your data stream by sending part of the buffer from this WSASend call and then all (or part) of a subsequent WSASend call.
//TODO max send to avoid consuming all resources
//TODO use SIO_IDEAL_SEND_BACKLOG_QUERY and SIO_IDEAL_SEND_BACKLOG_CHANGE
//TODO manage socket server as well

template<typename T>
template<typename... Args>
T *SocketClient::ListElt<T>::Create(CriticalList<T> &l, Args &&... args) {
    EnterCriticalSection(&l.critSec);
    //{
        l.list.emplace_back(l, args...);
        T &newElt = l.list.back();
        newElt.it = --l.list.end();
    //}
    LeaveCriticalSection(&l.critSec);
    return &newElt;
}

template<typename T>
void SocketClient::ListElt<T>::Delete(T *obj) {
    auto &critList = obj->critList;
    EnterCriticalSection(&critList.critSec);
    {
        obj->critList.list.erase(obj->it);
    }
    LeaveCriticalSection(&critList.critSec);
}

template<typename T>
void SocketClient::ListElt<T>::ClearList(SocketClient::CriticalList<T> &critList) {
    while (true){
        EnterCriticalSection(&critList.critSec);
        //{
        if (critList.list.empty()){
            LeaveCriticalSection(&critList.critSec);
            break;
        }
        T &elt = critList.list.front();
        //}
        LeaveCriticalSection(&critList.critSec);
        T::Delete(&elt);
    }
}

void SocketClient::Socket::Delete(Socket *obj) {
    EnterCriticalSection(&obj->SockCritSec);
    // Close the socket if it hasn't already been closed
    if (obj->s != INVALID_SOCKET) {
        _cprintf("Socket::Delete: closing socket\n");
        if (shutdown(obj->s, SD_BOTH) != NO_ERROR) {
            int err = WSAGetLastError();
            _cprintf("Socket::Delete: closesocket failed / error %d\n", err);
            if (err == WSAEINPROGRESS)
                return;
        }
        if (closesocket(obj->s) != NO_ERROR) {
            int err = WSAGetLastError();
            _cprintf("Socket::Delete: closesocket failed / error %d\n", err);
            if (err == WSAEINPROGRESS)
                return;
        }
        obj->s = INVALID_SOCKET;
    }
    LeaveCriticalSection(&obj->SockCritSec);

    ListElt<Socket>::Delete(obj);
}

int SocketClient::ReceiveData(const char *data, size_t len, Socket *socket) {
    _cprintf("receive bytes : %.*s\n", len, data);
    if(len == 5 && strncmp(data, "ping\n", 5) == 0)
        SendData("pong", 4, socket);
    return 1;
}

void SocketClient::SendData(const char *data, size_t len, Socket *socket) {
    _cprintf("send %s\n", data);

    while(len > 0){
        Buffer *sendobj = Buffer::Create(inUseBufferList, Operation::Write);

        size_t currentLen = len > DEFAULT_BUFFER_SIZE ? DEFAULT_BUFFER_SIZE : len;
        strncpy(sendobj->buf, data, currentLen);
        sendobj->buflen = currentLen;

        if(PostSend(socket, sendobj) == SOCKET_ERROR){
            socket->closing = true;
            Buffer::Delete(sendobj);
            break;
        }
        len -= currentLen;
    }
}

int SocketClient::PostRecv(Socket *sock, Buffer *recvobj) {
    WSABUF  wbuf;
    int     rc, err;
    DWORD   flags = 0;

    recvobj->operation = Operation::Read;
    wbuf.buf = recvobj->buf;
    wbuf.len = recvobj->buflen;
    EnterCriticalSection(&(sock->SockCritSec));
    {
        rc = WSARecv(sock->s,           //s : A descriptor identifying a connected socket.
                     &wbuf,             //lpBuffers : A pointer to an array of WSABUF structures. Each WSABUF structure contains a pointer to a buffer and the length, in bytes, of the buffer.
                     1,                 //dwBufferCount : The number of WSABUF structures in the lpBuffers array.
                     nullptr,           //lpNumberOfBytesRecvd : A pointer to the number, in bytes, of data received by this call if the receive operation completes immediately. Use NULL for this parameter if the lpOverlapped parameter is not NULL to avoid potentiall
                     &flags,            //lpFlags : A pointer to flags used to modify the behavior of the WSARecv function call.
                     &(recvobj->ol),    //lpOverlapped : A pointer to a WSAOVERLAPPED structure (ignored for nonoverlapped sockets).
                     nullptr);          //lpCompletionRoutine : A pointer to the completion routine called when the receive operation has been completed (ignored for nonoverlapped sockets).

        if (rc == SOCKET_ERROR) {
            rc = NO_ERROR;
            if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
                _cprintf("PostRecv: WSARecv* failed: %d\n", err);
                rc = SOCKET_ERROR;
            }
        }
        if (rc == NO_ERROR) {
            // Increment outstanding overlapped operations
            sock->OutstandingRecv++;
        }
    }
    LeaveCriticalSection(&(sock->SockCritSec));
    return rc;
}

int SocketClient::PostSend(Socket *sock, Buffer *sendobj) {
    WSABUF  wbuf;
    int     rc, err;

    sendobj->operation = Operation::Write;
    wbuf.buf = sendobj->buf;
    wbuf.len = sendobj->buflen;
    EnterCriticalSection(&(sock->SockCritSec));
    {
        rc = WSASend(sock->s,           //s : A descriptor identifying a connected socket.
                     &wbuf,             //lpBuffers : A pointer to an array of WSABUF structures. Each WSABUF structure contains a pointer to a buffer and the length, in bytes, of the buffer.
                     1,                 //dwBufferCount : The number of WSABUF structures in the lpBuffers array.
                     nullptr,           //lpNumberOfBytesSent : A pointer to the number, in bytes, sent by this call if the I/O operation completes immediately. Use NULL for this parameter if the lpOverlapped parameter is not NULL to avoid potentially erroneous results
                     0,                 //dwFlags : The flags used to modify the behavior of the WSASend function call.
                     &(sendobj->ol),    //lpOverlapped : A pointer to a WSAOVERLAPPED structure (ignored for nonoverlapped sockets).
                     nullptr);          //lpCompletionRoutine : A pointer to the completion routine called when the receive operation has been completed (ignored for nonoverlapped sockets).

        if (rc == SOCKET_ERROR) {
            rc = NO_ERROR;
            if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
                _cprintf("PostSend: WSASend* failed: %d [internal = %d]\n", err, sendobj->ol.Internal);
                rc = SOCKET_ERROR;
            }
        }
        if (rc == NO_ERROR) {
            // Increment the outstanding operation count
            sock->OutstandingSend++;
        }
    }
    LeaveCriticalSection(&(sock->SockCritSec));
    return rc;
}
void SocketClient::HandleError(Socket *sockobj, Buffer *buf, DWORD error) {
    bool    cleanupSocket   = false;

    _cprintf("OP = %d; Error = %d\n", buf->operation, error);

    EnterCriticalSection(&sockobj->SockCritSec);
    {
        if (buf->operation == Operation::Read) {
            sockobj->OutstandingRecv--;
        }
        else if (buf->operation == Operation::Write) {
            sockobj->OutstandingSend--;
        }
        if (sockobj->OutstandingRecv == 0 && sockobj->OutstandingSend == 0) {
            _cprintf("Freeing socket obj in GetOverlappedResult\n");
            cleanupSocket = true;
        }
    }
    LeaveCriticalSection(&sockobj->SockCritSec);
    if(cleanupSocket)
        Socket::Delete(sockobj);
    Buffer::Delete(buf);

}

void SocketClient::HandleIo(Socket *sockobj, Buffer *buf, DWORD BytesTransfered) {
    bool    cleanupSocket   = false;

    if (buf->operation == Operation::Read) {
        EnterCriticalSection(&sockobj->SockCritSec);
        {
            sockobj->OutstandingRecv--;
        }
        LeaveCriticalSection(&sockobj->SockCritSec);
        _cprintf("read\n");

        // Receive completed successfully
        if (BytesTransfered > 0) {
//            InterlockedExchangeAdd(&bytesRead, BytesTransfered);
            buf->buflen = BytesTransfered;
            ReceiveData(buf->buf, buf->buflen, sockobj);
            buf->buflen = DEFAULT_BUFFER_SIZE;
            if(PostRecv(sockobj, buf) == SOCKET_ERROR) {
                _cprintf("HandleIo: PostRecv failed!\n");
                sockobj->closing = true;
                Buffer::Delete(buf);
            }
        }
        else {
            _cprintf("Received 0 byte\n");
            // Graceful close - the receive returned 0 bytes read
            sockobj->closing = true;
            // Free the receive buffer
            Buffer::Delete(buf);
        }
    }
    else if (buf->operation == Operation::Write) {
        _cprintf("write\n");

        // Update the counters
        EnterCriticalSection(&sockobj->SockCritSec);
        {
            sockobj->OutstandingSend--;
        }
        LeaveCriticalSection(&sockobj->SockCritSec);
//        InterlockedExchangeAdd(&bytesSent, BytesTransfered);

        Buffer::Delete(buf);
    }
    else if (buf->operation == Operation::Connect || buf->operation == Operation::Accept ) {
        _cprintf("connected\n");
        // ----------------------------- trigger first recv
        buf->operation = Operation::Read;
        if(PostRecv(sockobj, buf) == SOCKET_ERROR){
            _cprintf("HandleIo: PostRecv failed!\n");
            sockobj->closing = true;
            Buffer::Delete(buf);
        }
    }
    else {
        _cprintf("op ?");
    }

    // If this was the last outstanding operation on closing socket, clean it up
    EnterCriticalSection(&sockobj->SockCritSec);
    {
        if ((sockobj->OutstandingSend == 0) && (sockobj->OutstandingRecv == 0) && (sockobj->closing)) {
            cleanupSocket = true;
        }
    }
    LeaveCriticalSection(&sockobj->SockCritSec);

    if (cleanupSocket) {
        Socket::Delete(sockobj);
    }
}

DWORD WINAPI SocketClient::IOCPWorkerThread(LPVOID lpParam) {
    Socket         *socket;
    Buffer         *buffer;
    LPOVERLAPPED   *lpOverlapped        = nullptr;
    auto            CompletionPort      = (HANDLE)lpParam;
    DWORD           BytesTransfered;
    DWORD           Flags;
    int             rc;
    DWORD           error;

    while (true) {
        error = NO_ERROR;
        rc = GetQueuedCompletionStatus(CompletionPort,                        //CompletionPort[in] : A handle to the completion port. To create a completion port, use the CreateIoCompletionPort function.
                                       &BytesTransfered,                      //lpNumberOfBytes[out] : A pointer to a variable that receives the number of bytes transferred during an I/O operation that has completed.
                                       (PULONG_PTR)&socket,                   //lpCompletionKey[out] : A pointer to a variable that receives the completion key value associated with the file handle whose I/O operation has completed. A completion key is a per-file key that is specified in a call to CreateIoCompletionPort.
                                       (LPOVERLAPPED *)&lpOverlapped,         //lpOverlapped[out] : A pointer to a variable that receives the address of the OVERLAPPED structure that was specified when the completed I/O operation was started.
                                       INFINITE);                             //dwMilliseconds[in] : The number of milliseconds that the caller is willing to wait for a completion packet to appear at the completion port.
        buffer = CONTAINING_RECORD(lpOverlapped, Buffer, ol);
        if (rc == FALSE) {
            error = GetLastError();
            _cprintf("CompletionThread: GetQueuedCompletionStatus failed for operation %d : %d\n", buffer->operation, error);

            if(socket != nullptr) {
                rc = WSAGetOverlappedResult(socket->s, &buffer->ol, &BytesTransfered, FALSE, &Flags);
                if (rc == FALSE) {
                    error = static_cast<DWORD>(WSAGetLastError());
                }
            }
        }
        if (buffer->operation == Operation::End)
            break;
        if (error != NO_ERROR)
            socket->client->HandleError(socket, buffer, error);
        else
            socket->client->HandleIo(socket, buffer, BytesTransfered);
    }

    _cprintf("exit thread");
    return NO_ERROR;
}

SocketClient::SocketClient() : state(State::NOT_INITIALIZED), iocpHandle(INVALID_HANDLE_VALUE) {
    int         res;
    
    // ----------------------------- start WSA
    
    WSADATA     wsaData; // gets populated w/ info explaining this sockets implementation

    // load Winsock 2.2 DLL. initiates use of the Winsock DLL by a process
    if ((res = WSAStartup(MAKEWORD(2, 2), &wsaData)) != NO_ERROR) {
        //WSASYSNOTREADY, WSAVERNOTSUPPORTED, WSAEINPROGRESS, WSAEPROCLIM, WSAEFAULT
        _cprintf("WSAStartup failed / error %d\n", res);
        return;
    }
    _cprintf("WSAStartup ok\n");
    state = State::WSA_INITIALIZED;

    // ----------------------------- set up IOCP

    if ((iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE,   //FileHandle[in] : If INVALID_HANDLE_VALUE is specified, the function creates an I/O completion port without associating it with a file handle. In this case, the ExistingCompletionPort parameter must be NULL and the CompletionKey parameter is ignored.
                                             nullptr,                //ExistingCompletionPort[in, optional] : If this parameter is NULL, the function creates a new I/O completion port.
                                             0,                      //CompletionKey[in] : The per-handle user-defined completion key that is included in every I/O completion packet for the specified file handle. (ignored)
                                             0                       //NumberOfConcurrentThreads[in] : The maximum number of threads that the operating system can allow to concurrently process I/O completion packets for the I/O completion port. If this parameter is zero, the system allows as many concurrently running threads as there are processors in the system.
                                            )) == nullptr) {
        _cprintf("CreateIoCompletionPort failed / error %d\n", GetLastError());
        return;
    }
    _cprintf("CreateIoCompletionPort ok\n");
    state = State::IOCP_INITIALIZED;

    // count processors

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);

    // ----------------------------- init critical sections

    InitializeCriticalSection(&inUseBufferList.critSec);
    InitializeCriticalSection(&inUseSocketList.critSec);
    state = State::CRIT_SEC_INITIALIZED;

    // ----------------------------- create worker threads

    HANDLE      ThreadHandle;
    DWORD       ThreadID;
    for (int i = 0; i < (int)SystemInfo.dwNumberOfProcessors * THREADS_PER_PROC; i++) {
        // create worker thread and pass the completion port to the thread
        if ((ThreadHandle = CreateThread(nullptr,                 // default security attributes
                                         0,                       // use default stack size
                                         IOCPWorkerThread,        // thread function
                                         iocpHandle,              // argument to thread function
                                         0,                       // use default creation flags
                                         &ThreadID                // thread identifier
                                        )) == nullptr) {
            _cprintf("CreateThread failed / error %d\n", GetLastError());
            ClearThreads();
            return;
        }
        _cprintf("CreateThread %d ok\n", ThreadID);
        threadHandles.push_back(ThreadHandle);
    }
    state = State::THREADS_INITIALIZED;
    if (!InitAsyncSocketFuncs())
        return;
    state = State::MSWSOCK_FUNC_INITIALIZED;
    state = State::READY;
}

SocketClient::~SocketClient() {
    if(state >= State::THREADS_INITIALIZED){
        ClearThreads();
    }
    ListElt<Socket>::ClearList(inUseSocketList);
    ListElt<Buffer>::ClearList(inUseBufferList);
    if(state >= State::CRIT_SEC_INITIALIZED){
        DeleteCriticalSection(&inUseBufferList.critSec);
        DeleteCriticalSection(&inUseSocketList.critSec);
    }
    if(state >= State::IOCP_INITIALIZED){
        CloseHandle(iocpHandle);
    }
    if(state >= State::WSA_INITIALIZED){
        if(WSACleanup() == SOCKET_ERROR){
            //WSANOTINITIALISED, WSAENETDOWN, WSAEINPROGRESS
            _cprintf("WSACleanup failed / error %d\n", WSAGetLastError());
        }
    }
}

void SocketClient::ClearThreads() {
    if(!threadHandles.empty()){
        for (int i = 0 ; i < threadHandles.size() ; i++){
            Buffer *endobj = Buffer::Create(inUseBufferList, Operation::End);

            // Unblock threads from GetQueuedCompletionStatus call and signal application close
            PostQueuedCompletionStatus(iocpHandle,              //CompletionPort : A handle to an I/O completion port to which the I/O completion packet is to be posted.
                                       0,                       //dwNumberOfBytesTransferred : The value to be returned through the lpNumberOfBytesTransferred parameter of the GetQueuedCompletionStatus function.
                                       (ULONG_PTR)nullptr,      //dwNumberOfBytesTransferred : The value to be returned through the lpCompletionKey parameter of the GetQueuedCompletionStatus function.
                                       &(endobj->ol));          //lpOverlapped : The value to be returned through the lpOverlapped parameter of the GetQueuedCompletionStatus function.
        }
        WaitForMultipleObjects(threadHandles.size(),            //nCount : The number of object handles in the array pointed to by lpHandles
                               threadHandles.data(),            //lpHandles : An array of object handles.
                               TRUE,                            //bWaitAll : If this parameter is TRUE, the function returns when the state of all objects in the lpHandles array is signaled
                               INFINITE);                       //dwMilliseconds : The time-out interval, in milliseconds. If dwMilliseconds is INFINITE, the function will return only when the specified objects are signaled.
        for (HANDLE &threadHandle : threadHandles){
            CloseHandle(threadHandle);
        }
        threadHandles.clear();
    }
}

SocketClient::Socket* SocketClient::ListenToNewSocket(const char *address, u_short port) {
    int err;
    if (state < State::READY)
        return nullptr;

    // ----------------------------- create socket

    SOCKET sock;
    if ((sock = WSASocket(FAMILY,                      //af : The address family specification
                          SOCK_STREAM,                 //type : SOCK_STREAM -> A socket type that provides sequenced, reliable, two-way, connection-based byte streams with an OOB data transmission mechanism. This socket type uses the Transmission Control Protocol (TCP) for the Internet address family (AF_INET or AF_INET6).
                          IPPROTO_TCP,                 //protocol : IPPROTO_TCP -> The Transmission Control Protocol (TCP). This is a possible value when the af parameter is AF_INET or AF_INET6 and the type parameter is SOCK_STREAM.
                          nullptr,                     //lpProtocolInfo : A pointer to a WSAPROTOCOL_INFO structure that defines the characteristics of the socket to be created.
                          0,                           //g : An existing socket group ID or an appropriate action to take when creating a new socket and a new socket group. 0 -> No group operation is performed.
                          WSA_FLAG_OVERLAPPED          //dwFlags : A set of flags used to specify additional socket attributes. WSA_FLAG_OVERLAPPED -> Create a socket that supports overlapped I/O operations.
                         )) == INVALID_SOCKET) {
        _cprintf("WSASocket failed / error %d\n", WSAGetLastError());
        return nullptr;
    }
    else
        _cprintf("WSASocket ok\n");

    const int fam = FAMILY;
    Socket *sockObj = Socket::Create(inUseSocketList, this, sock, fam); //can't use FAMILY directly else undefined reference to `SocketClient::FAMILY' STRANGEST ERROR EVER, compiler bug ?

    // ----------------------------- associate socket to IOCP
    _cprintf("GetSocketObj ok\n");
    HANDLE hrc = CreateIoCompletionPort((HANDLE)sockObj->s,          //FileHandle[in] : An open file handle. The handle must be to an object that supports overlapped I/O.
                                        iocpHandle,                  //ExistingCompletionPort[in, optional] : A handle to an existing I/O completion, the function associates it with the handle specified by the FileHandle parameter.
                                        (ULONG_PTR)sockObj,          //CompletionKey[in] : The per-handle user-defined completion key that is included in every I/O completion packet for the specified file handle.
                                        0);                          //NumberOfConcurrentThreads[in] : This parameter is ignored if the ExistingCompletionPort parameter is not NULL.
    if (hrc == nullptr) {
        _cprintf("CreateIoCompletionPort failed / error %d\n", GetLastError());
        Socket::Delete(sockObj);
        return nullptr;
    }
    _cprintf("CreateIoCompletionPort ok\n");

    // ----------------------------- bind socket
    SOCKADDR_IN SockAddr;
    ZeroMemory(&SockAddr, sizeof(SOCKADDR_IN));
    SockAddr.sin_family = FAMILY;
    SockAddr.sin_addr.s_addr = INADDR_ANY;
    SockAddr.sin_port = 0;
    if (bind(sock,                        //s : A descriptor identifying an unconnected socket.
             (SOCKADDR*)(&SockAddr),      //name : A pointer to a sockaddr structure that specifies the address to which to connect. For IPv4, the sockaddr contains AF_INET for the address family, the destination IPv4 address, and the destination port.
             sizeof(SockAddr)             //namelen : The length, in bytes, of the sockaddr structure pointed to by the name parameter.
            ) == SOCKET_ERROR){
        _cprintf("bind failed / error %d\n", WSAGetLastError());
        Socket::Delete(sockObj);
        return nullptr;
    }

    // ----------------------------- connect socket
    SockAddr.sin_addr.s_addr = inet_addr(address);
    if(WSAHtons(sockObj->s, port, &SockAddr.sin_port) == SOCKET_ERROR) { // host-to-network-short: big-endian conversion of a 16 byte value
        //WSANOTINITIALISED, WSAENETDOWN, WSAENOTSOCK, WSAEFAULT
        _cprintf("WSAHtonl failed / error %d\n", GetLastError());
        Socket::Delete(sockObj);
        return nullptr;
    }

    Buffer *connectobj = Buffer::Create(inUseBufferList, Operation::Connect);
    if (!ConnectEx(sock,                        //s : A descriptor identifying an unconnected socket.
                   (SOCKADDR*)(&SockAddr),      //name : A pointer to a sockaddr structure that specifies the address to which to connect. For IPv4, the sockaddr contains AF_INET for the address family, the destination IPv4 address, and the destination port.
                   sizeof(SockAddr),            //namelen : The length, in bytes, of the sockaddr structure pointed to by the name parameter.
                   nullptr,                     //lpSendBuffer : A pointer to the buffer to be transferred after a connection is established. This parameter is optional.
                   0,                           //dwSendDataLength : The length, in bytes, of data pointed to by the lpSendBuffer parameter. This parameter is ignored when the lpSendBuffer parameter is NULL.
                   nullptr,                     //lpdwBytesSent : On successful return, this parameter points to a DWORD value that indicates the number of bytes that were sent after the connection was established. This parameter is ignored when the lpSendBuffer parameter is NULL.
                   &(connectobj->ol)            //lpOverlapped : An OVERLAPPED structure used to process the request. The lpOverlapped parameter must be specified, and cannot be NULL.
                  )) {
        if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
            _cprintf("ListenToNewSocket: ConnectEx failed: %d\n", err);
            Socket::Delete(sockObj);
            return nullptr; // connect error
        }
    }
    _cprintf("ConnectEx ok\n");
    return sockObj;
}

bool SocketClient::InitAsyncSocketFuncs() {
    //dummy socket to pass to WSAIoctl call
    SOCKET sock = WSASocket(FAMILY,                      //af : The address family specification
                            SOCK_STREAM,                 //type : SOCK_STREAM -> A socket type that provides sequenced, reliable, two-way, connection-based byte streams with an OOB data transmission mechanism. This socket type uses the Transmission Control Protocol (TCP) for the Internet address family (AF_INET or AF_INET6).
                            IPPROTO_TCP,                 //protocol : IPPROTO_TCP -> The Transmission Control Protocol (TCP). This is a possible value when the af parameter is AF_INET or AF_INET6 and the type parameter is SOCK_STREAM.
                            nullptr,                     //lpProtocolInfo : A pointer to a WSAPROTOCOL_INFO structure that defines the characteristics of the socket to be created.
                            0,                           //g : An existing socket group ID or an appropriate action to take when creating a new socket and a new socket group. 0 -> No group operation is performed.
                            0);                          //dwFlags : A set of flags used to specify additional socket attributes.
    if (sock == INVALID_SOCKET) {
        _cprintf("WSASocket failed / error %d\n", WSAGetLastError());
        return false;
    }
    return InitAsyncSocketFunc(sock, WSAID_CONNECTEX, &ConnectEx, sizeof(ConnectEx)) &&
           InitAsyncSocketFunc(sock, WSAID_DISCONNECTEX, &DisconnectEx, sizeof(DisconnectEx));/* &&
           InitAsyncSocketFunc(sock, WSAID_ACCEPTEX, &AcceptEx, sizeof(AcceptEx)); */
} //TODO remove either Accept or Connect depending on client/server

bool SocketClient::InitAsyncSocketFunc(SOCKET sock, GUID guid, LPVOID func, DWORD size) {
    DWORD   dwBytes;
    if (WSAIoctl(sock,                                   //s : A descriptor identifying a socket.
                 SIO_GET_EXTENSION_FUNCTION_POINTER,     //dwIoControlCode : The control code of operation to perform.
                 &guid,                                  //lpvInBuffer : A pointer to the input buffer.
                 sizeof(guid),                           //cbInBuffer : The size, in bytes, of the input buffer.
                 func,                                   //lpvOutBuffer : A pointer to the output buffer.
                 size,                                   //cbOutBuffer : The size, in bytes, of the output buffer.
                 &dwBytes,                               //lpcbBytesReturned : A pointer to actual number of bytes of output.
                 nullptr,                                //lpOverlapped : A pointer to a WSAOVERLAPPED structure (ignored for non-overlapped sockets).
                 nullptr                                 //lpCompletionRoutine : A pointer to the completion routine called when the operation has been completed (ignored for non-overlapped sockets).
                ) != 0) {
        _cprintf("WSAIoctl failed / error %d\n", WSAGetLastError());
        return false;
    }
    return true;
}
