#include "SocketManager.h"
#include "SocketHelperClasses.h"

LPFN_CONNECTEX          SocketManager::ConnectEx             = nullptr;
LPFN_DISCONNECTEX       SocketManager::DisconnectEx          = nullptr;
LPFN_ACCEPTEX           SocketManager::AcceptEx              = nullptr;
const TCHAR *           SocketManager::TIME_WAIT_REG_KEY     = TEXT("SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters");
const TCHAR *           SocketManager::TIME_WAIT_REG_VALUE   = TEXT("TcpTimedWaitDelay");
DWORD                   SocketManager::TimeWaitValue         = 0;

void SocketManager::ChangeSocketState (Socket *sock, Socket::SocketState state){
    EnterCriticalSection(&sock->SockCritSec);
    {
        sock->state = state;
    }
    LeaveCriticalSection(&sock->SockCritSec);
}

bool SocketManager::SendData(const char *data, u_long length, Socket *socket) {
    if (socket == nullptr || socket->state != Socket::SocketState::CONNECTED) {
        return false;
    }
    if (length + socket->pendingByteSent > socket->maxPendingByteSent){
        LOG_ERROR("Socket %llu : Too mush pending send, retry after more data has been acknowledged by receiver\n", socket->s);
        return false;
    }
    LOG("send %lu : %s\n", length, data);

    while(length > 0){
        Buffer *sendObj = Buffer::Create(inUseBufferList, Buffer::Operation::Write);

        u_long currentLen = length > Buffer::DEFAULT_BUFFER_SIZE ? Buffer::DEFAULT_BUFFER_SIZE : length;
        strncpy(sendObj->buf, data, currentLen);
        sendObj->bufLen = currentLen;

        if(PostSend(socket, sendObj) == SOCKET_ERROR){
            EnterCriticalSection(&socket->SockCritSec);
            {
                socket->state = Socket::SocketState::FAILURE;
            }
            LeaveCriticalSection(&socket->SockCritSec);
            Buffer::Delete(sendObj);
            break;
        }
        length -= currentLen;
    }
    return true;
}

int SocketManager::PostRecv(Socket *sock, Buffer *recvObj) {
    WSABUF  wbuf;
    int     err;
    DWORD   flags = 0;

    wbuf.buf = recvObj->buf;
    wbuf.len = recvObj->bufLen;
    EnterCriticalSection(&(sock->SockCritSec));
    {
        err = WSARecv(sock->s,           //s : A descriptor identifying a connected socket.
                      &wbuf,             //lpBuffers : A pointer to an array of WSABUF structures. Each WSABUF structure contains a pointer to a buffer and the length, in bytes, of the buffer.
                      1,                 //dwBufferCount : The number of WSABUF structures in the lpBuffers array.
                      nullptr,           //lpNumberOfBytesRecvd : A pointer to the number, in bytes, of data received by this call if the receive operation completes immediately. Use NULL for this parameter if the lpOverlapped parameter is not NULL to avoid potentiall
                      &flags,            //lpFlags : A pointer to flags used to modify the behavior of the WSARecv function call.
                      &(recvObj->ol),    //lpOverlapped : A pointer to a WSAOVERLAPPED structure (ignored for nonoverlapped sockets).
                      nullptr);          //lpCompletionRoutine : A pointer to the completion routine called when the receive operation has been completed (ignored for nonoverlapped sockets).

        if (err == SOCKET_ERROR) {
            if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
                LOG_ERROR("WSARecv* failed: %d\n", err);
                err = SOCKET_ERROR;
            } else
                err = NO_ERROR;
        }
        if (err == NO_ERROR) {
            // Increment outstanding overlapped operations
            sock->OutstandingRecv++;
        }
    }
    LeaveCriticalSection(&(sock->SockCritSec));
    return err;
}

int SocketManager::PostSend(Socket *sock, Buffer *sendObj) {
    WSABUF  wbuf;
    int     err;

    wbuf.buf = sendObj->buf;
    wbuf.len = sendObj->bufLen;

    EnterCriticalSection(&(sock->SockCritSec));
    {
        err = WSASend(sock->s,           //s : A descriptor identifying a connected socket.
                      &wbuf,             //lpBuffers : A pointer to an array of WSABUF structures. Each WSABUF structure contains a pointer to a buffer and the length, in bytes, of the buffer.
                      1,                 //dwBufferCount : The number of WSABUF structures in the lpBuffers array.
                      nullptr,           //lpNumberOfBytesSent : A pointer to the number, in bytes, sent by this call if the I/O operation completes immediately. Use NULL for this parameter if the lpOverlapped parameter is not NULL to avoid potentially erroneous results
                      0,                 //dwFlags : The flags used to modify the behavior of the WSASend function call.
                      &(sendObj->ol),    //lpOverlapped : A pointer to a WSAOVERLAPPED structure (ignored for nonoverlapped sockets).
                      nullptr);          //lpCompletionRoutine : A pointer to the completion routine called when the receive operation has been completed (ignored for nonoverlapped sockets).

        if (err == SOCKET_ERROR) {
            if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
                LOG_ERROR("WSASend* failed: %d [internal = %llu]\n", err, sendObj->ol.Internal);
                err = SOCKET_ERROR;
            } else
                err = NO_ERROR;
        }
        if (err == NO_ERROR) {
            // Increment the outstanding operation count
            sock->OutstandingSend++;
            InterlockedExchangeAdd64(&sock->pendingByteSent, static_cast<LONG64>(sendObj->bufLen));
        }
    }
    LeaveCriticalSection(&(sock->SockCritSec));
    return err;
}

int SocketManager::PostISBNotify(Socket *sock, Buffer *isbObj) {
    int err;

    EnterCriticalSection(&(sock->SockCritSec));
    {
        err = idealsendbacklognotify(sock->s, &(isbObj->ol), nullptr);
        if (err == SOCKET_ERROR) {
            if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
                LOG_ERROR("idealsendbacklognotify failed: %d\n", err);
                err = SOCKET_ERROR;
            } else
                err = NO_ERROR;
        }
    }
    LeaveCriticalSection(&(sock->SockCritSec));

    return err;
}

void SocketManager::HandleError(Socket *sockObj, Buffer *buf, DWORD error) {
    bool    cleanupSocket   = false;

    LOG_ERROR("Handle error OP = %d; Error = %lu\n", buf->operation, error);

    EnterCriticalSection(&sockObj->SockCritSec);
    {
        switch (buf->operation){
            case Buffer::Operation::Connect :{
                if (error == WSAEADDRINUSE){ // TimeWaitValue must not have been big enough, update it and connect another socket instead
                    TimeWaitValue *= 2;
                    if (TimeWaitValue > MAX_TIME_WAIT_VALUE)
                        TimeWaitValue = MAX_TIME_WAIT_VALUE;
                    ConnectToNewSocket(sockObj->address, sockObj->port, sockObj->id);
                    sockObj->s = INVALID_SOCKET;
                    sockObj->state = Socket::SocketState::RETRY_CONNECTION;
                } else {
                    sockObj->state = Socket::SocketState::CONNECT_FAILURE;
                }
                break;
            }
            case Buffer::Operation::Read :{
                sockObj->state = Socket::SocketState::FAILURE;
                sockObj->OutstandingRecv--;
                break;
            }
            case Buffer::Operation::Write :{
                sockObj->state = Socket::SocketState::FAILURE;
                sockObj->OutstandingSend--;;
                InterlockedExchangeAdd64(&sockObj->pendingByteSent, -static_cast<LONG64>(buf->bufLen));
                break;
            }
            default :{
                sockObj->state = Socket::SocketState::FAILURE;
            }
        }
        if (sockObj->OutstandingRecv == 0 && sockObj->OutstandingSend == 0) {
            LOG("Freeing socket obj in HandleError\n");
            cleanupSocket = true;
        }
    }
    LeaveCriticalSection(&sockObj->SockCritSec);
    if(cleanupSocket)
        Socket::DeleteOrDisconnect(sockObj, socketAccessMap);
    Buffer::Delete(buf);
}

void SocketManager::HandleIo(Socket *sockObj, Buffer *buf, DWORD bytesTransfered) {
    bool    cleanupSocket   = false;

    switch(buf->operation) {
        case Buffer::Operation::Read :{
            HandleRead(sockObj, buf, bytesTransfered);
            break;
        }
        case Buffer::Operation::Write :{
            HandleWrite(sockObj, buf, bytesTransfered);
            break;
        }
        case Buffer::Operation::Connect :
            /** NOBREAK **/
        case Buffer::Operation::Accept :{
            HandleConnection(sockObj, buf);
            break;
        }
        case Buffer::Operation::Disconnect :{
            HandleDisconnect(sockObj, buf);
            break;
        }
        case Buffer::Operation::ISBChange :{
            UpdateISB(sockObj, buf);
            break;
        }
        default:
            LOG_ERROR("Unknown OP: %d\n", buf->operation);
    }

    // If this was the last outstanding operation on closing socket, clean it up
    EnterCriticalSection(&sockObj->SockCritSec);
    {
        if ((sockObj->OutstandingSend == 0) && (sockObj->OutstandingRecv == 0) && (sockObj->state > Socket::SocketState::CONNECTED)) {
            cleanupSocket = true;
        }
    }
    LeaveCriticalSection(&sockObj->SockCritSec);

    if (cleanupSocket) {
        Socket::DeleteOrDisconnect(sockObj, socketAccessMap);
    }
}

void SocketManager::HandleRead(Socket *sockObj, Buffer *buf, DWORD bytesTransfered) {
    EnterCriticalSection(&sockObj->SockCritSec);
    {
        sockObj->OutstandingRecv--;
    }
    LeaveCriticalSection(&sockObj->SockCritSec);
    LOG("read\n");

    // Receive completed successfully
    if (bytesTransfered > 0) {
        buf->bufLen = bytesTransfered;
        ReceiveData(buf->buf, buf->bufLen, sockObj);
        buf->bufLen = Buffer::DEFAULT_BUFFER_SIZE;
        if (sockObj->state != Socket::SocketState::CONNECTED)
            Buffer::Delete(buf);
        else if(PostRecv(sockObj, buf) == SOCKET_ERROR) {
            LOG_ERROR("PostRecv failed!\n");
            ChangeSocketState(sockObj, Socket::SocketState::FAILURE);
            LeaveCriticalSection(&sockObj->SockCritSec);
            Buffer::Delete(buf);
        }
    }
    else {
        LOG("Received 0 byte\n");
        // Graceful close - the receive returned 0 bytes read
        ChangeSocketState(sockObj, Socket::SocketState::CLOSING);
        // Free the receive buffer
        Buffer::Delete(buf);
    }
}

void SocketManager::HandleWrite(Socket *sockObj, Buffer *buf, DWORD bytesTransfered) {
    LOG("write\n");

    // Update the counters
    EnterCriticalSection(&sockObj->SockCritSec);
    {
        sockObj->OutstandingSend--;
        InterlockedExchangeAdd64(&sockObj->pendingByteSent, -static_cast<LONG64>(buf->bufLen));
    }
    LeaveCriticalSection(&sockObj->SockCritSec);
    if (bytesTransfered < buf->bufLen){ //incomplete send, very small chance of it ever happening, socket send stream most probably corrupted
        ChangeSocketState(sockObj, Socket::SocketState::FAILURE);
    }

    Buffer::Delete(buf);
}

void SocketManager::HandleConnection(Socket *sockObj, Buffer *buf) {
    LOG("connected\n");
    int err;
    int option, optSize;
    char *optPtr;
    if (buf->operation == Buffer::Operation::Connect){
        option = SO_UPDATE_CONNECT_CONTEXT;               //This option is used with the ConnectEx, WSAConnectByList, and WSAConnectByName functions. This option updates the properties of the socket after the connection is established. This option should be set if the getpeername, getsockname, getsockopt, setsockopt, or shutdown functions are to be used on the connected socket.
        optSize = 0;
        optPtr = nullptr;
    } else {
        Socket *listenSocketObj = sockObj;
        sockObj = currentAcceptSocket;                    //sockObj is the listen socket and not the new communication socket
        option = SO_UPDATE_ACCEPT_CONTEXT;                //This option is used with the AcceptEx function. This option updates the properties of the socket which are inherited from the listening socket. This option should be set if the getpeername, getsockname, getsockopt, or setsockopt functions are to be used on the accepted socket.
        optSize = sizeof(listenSocketObj->s);
        optPtr = (char*)&listenSocketObj->s;
        AddSocketToMap(sockObj, Misc::CreateNilUUID());
        AcceptNewSocket(listenSocketObj);
    }
    ChangeSocketState(sockObj, Socket::SocketState::CONNECTED);
    // ----------------------------- set needed options
    err = SetSocketOption(sockObj->s, option, optPtr, optSize);
    // ----------------------------- trigger first recv
    buf->operation = Buffer::Operation::Read;
    if(PostRecv(sockObj, buf) == SOCKET_ERROR){
        err = SOCKET_ERROR;
        LOG_ERROR("PostRecv failed!\n");
    }
    // ----------------------------- track isb
    if (isbFactor > 0) {
        Buffer *isbBuf = Buffer::Create(inUseBufferList, Buffer::Operation::ISBChange);
        UpdateISB(sockObj, isbBuf);
    }

    if (err != NO_ERROR){
        ChangeSocketState(sockObj, Socket::SocketState::FAILURE);
        Buffer::Delete(buf);
        Socket::DeleteOrDisconnect(sockObj, socketAccessMap);
    }
}

void SocketManager::HandleDisconnect(Socket *sockObj, Buffer *buf) {
    EnterCriticalSection(&sockObj->SockCritSec);
    {
        sockObj->state = Socket::SocketState::DISCONNECTED;
        sockObj->timeWaitStartTime = GetTickCount();
    }
    LeaveCriticalSection(&sockObj->SockCritSec);
    EnterCriticalSection(&reusableSocketQueue.critSec);
    {
        reusableSocketQueue.queue.push(sockObj);
    }
    LeaveCriticalSection(&reusableSocketQueue.critSec);
    LOG("disconnected\n");
    Buffer::Delete(buf);
}

void SocketManager::UpdateISB(Socket *sockObj, Buffer *buf) {
    ULONG isbVal;
    bool queryFail = false;

    if (PostISBNotify(sockObj, buf) == SOCKET_ERROR ||
        (idealsendbacklogquery(sockObj->s, &isbVal) == SOCKET_ERROR && (queryFail = true))){
        if (queryFail) {
            LOG_ERROR("idealsendbacklognotify failed: %d\n", WSAGetLastError());
        }
        isbVal = DEFAULT_MAX_PENDING_BYTE_SENT;
    }
    LOG("isb changed to %lu\n", isbVal);
    SetSocketOption(sockObj->s, SO_SNDBUF, (char*)&isbVal, sizeof(isbVal));
    sockObj->maxPendingByteSent = isbVal*isbFactor;
}

DWORD WINAPI SocketManager::IOCPWorkerThread(LPVOID lpParam) {
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
            LOG_ERROR("GetQueuedCompletionStatus failed for operation %d : %lu\n", buffer->operation, error);

            if(socket != nullptr) {
                rc = WSAGetOverlappedResult(socket->s, &buffer->ol, &BytesTransfered, FALSE, &Flags);
                if (rc == FALSE) {
                    error = static_cast<DWORD>(WSAGetLastError());
                }
            }
        }
        if (buffer->operation == Buffer::Operation::End)
            break;
        if (error != NO_ERROR)
            socket->client->HandleError(socket, buffer, error);
        else
            socket->client->HandleIo(socket, buffer, BytesTransfered);
    }

    LOG("exit thread");
    return NO_ERROR;
}

SocketManager::SocketManager(Type t, unsigned short factor) :   state(State::NOT_INITIALIZED), type(t), isbFactor(factor),
                                                                iocpHandle(INVALID_HANDLE_VALUE), currentAcceptSocket(nullptr) {
    int         res;

    // ----------------------------- start WSA
    
    WSADATA     wsaData; // gets populated w/ info explaining this sockets implementation

    // load Winsock 2.2 DLL. initiates use of the Winsock DLL by a process
    if ((res = WSAStartup(MAKEWORD(2, 2), &wsaData)) != NO_ERROR) {
        //WSASYSNOTREADY, WSAVERNOTSUPPORTED, WSAEINPROGRESS, WSAEPROCLIM, WSAEFAULT
        LOG_ERROR("WSAStartup failed / error %d\n", res);
        return;
    }
    LOG("WSAStartup ok\n");
    state = State::WSA_INITIALIZED;

    // ----------------------------- set up IOCP

    if ((iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE,   //FileHandle[in] : If INVALID_HANDLE_VALUE is specified, the function creates an I/O completion port without associating it with a file handle. In this case, the ExistingCompletionPort parameter must be NULL and the CompletionKey parameter is ignored.
                                             nullptr,                //ExistingCompletionPort[in, optional] : If this parameter is NULL, the function creates a new I/O completion port.
                                             0,                      //CompletionKey[in] : The per-handle user-defined completion key that is included in every I/O completion packet for the specified file handle. (ignored)
                                             0                       //NumberOfConcurrentThreads[in] : The maximum number of threads that the operating system can allow to concurrently process I/O completion packets for the I/O completion port. If this parameter is zero, the system allows as many concurrently running threads as there are processors in the system.
                                            )) == nullptr) {
        LOG_ERROR("CreateIoCompletionPort failed / error %lu\n", GetLastError());
        return;
    }
    LOG("CreateIoCompletionPort ok\n");
    state = State::IOCP_INITIALIZED;

    // count processors

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);

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
            LOG_ERROR("CreateThread failed / error %lu\n", GetLastError());
            ClearThreads();
            return;
        }
        LOG("CreateThread %lu ok\n", ThreadID);
        threadHandles.push_back(ThreadHandle);
    }
    state = State::THREADS_INITIALIZED;
    if (DisconnectEx == nullptr && !InitAsyncSocketFuncs())
        return;
    state = State::MSWSOCK_FUNC_INITIALIZED;
    if (TimeWaitValue == 0)
        InitTimeWaitValue();
    state = State::TIME_WAIT_VALUE_SELECTED;
    state = State::READY;
}

SocketManager::~SocketManager() {
    if(state >= State::THREADS_INITIALIZED){
        ClearThreads();
    }
    ListElt<Socket>::ClearList(inUseSocketList);
    ListElt<Buffer>::ClearList(inUseBufferList);
    if(state >= State::IOCP_INITIALIZED){
        CloseHandle(iocpHandle);
    }
    if(state >= State::WSA_INITIALIZED){
        if(WSACleanup() == SOCKET_ERROR){
            //WSANOTINITIALISED, WSAENETDOWN, WSAEINPROGRESS
            LOG_ERROR("WSACleanup failed / error %d\n", WSAGetLastError());
        }
    }
}

void SocketManager::ClearThreads() {
    if(!threadHandles.empty()){
        for (int i = 0 ; i < threadHandles.size() ; i++){
            Buffer *endObj = Buffer::Create(inUseBufferList, Buffer::Operation::End);

            // Unblock threads from GetQueuedCompletionStatus call and signal application close
            PostQueuedCompletionStatus(iocpHandle,                      //CompletionPort : A handle to an I/O completion port to which the I/O completion packet is to be posted.
                                       0,                               //dwNumberOfBytesTransferred : The value to be returned through the lpNumberOfBytesTransferred parameter of the GetQueuedCompletionStatus function.
                                       (ULONG_PTR)nullptr,              //dwNumberOfBytesTransferred : The value to be returned through the lpCompletionKey parameter of the GetQueuedCompletionStatus function.
                                       &(endObj->ol));                  //lpOverlapped : The value to be returned through the lpOverlapped parameter of the GetQueuedCompletionStatus function.
        }
        WaitForMultipleObjects(static_cast<DWORD>(threadHandles.size()),//nCount : The number of object handles in the array pointed to by lpHandles
                               threadHandles.data(),                    //lpHandles : An array of object handles.
                               TRUE,                                    //bWaitAll : If this parameter is TRUE, the function returns when the state of all objects in the lpHandles array is signaled
                               INFINITE);                               //dwMilliseconds : The time-out interval, in milliseconds. If dwMilliseconds is INFINITE, the function will return only when the specified objects are signaled.
        for (HANDLE &threadHandle : threadHandles){
            CloseHandle(threadHandle);
        }
        threadHandles.clear();
    }
}

Socket *SocketManager::GenerateSocket(bool reuse){
    SOCKET sock;
    Socket *sockObj = reuse ? ReuseSocket() : nullptr;

    if(sockObj == nullptr) {
        if ((sock = WSASocket(FAMILY,                      //af : The address family specification
                              SOCK_STREAM,                 //type : SOCK_STREAM -> A socket type that provides sequenced, reliable, two-way, connection-based byte streams with an OOB data transmission mechanism. This socket type uses the Transmission Control Protocol (TCP) for the Internet address family (AF_INET or AF_INET6).
                              IPPROTO_TCP,                 //protocol : IPPROTO_TCP -> The Transmission Control Protocol (TCP). This is a possible value when the af parameter is AF_INET or AF_INET6 and the type parameter is SOCK_STREAM.
                              nullptr,                     //lpProtocolInfo : A pointer to a WSAPROTOCOL_INFO structure that defines the characteristics of the socket to be created.
                              0,                           //g : An existing socket group ID or an appropriate action to take when creating a new socket and a new socket group. 0 -> No group operation is performed.
                              WSA_FLAG_OVERLAPPED          //dwFlags : A set of flags used to specify additional socket attributes. WSA_FLAG_OVERLAPPED -> Create a socket that supports overlapped I/O operations.
        )) == INVALID_SOCKET) {
            LOG_ERROR("WSASocket failed / error %d\n", WSAGetLastError());
            return nullptr;
        }
        LOG("WSASocket ok\n");
        const int fam = FAMILY;
        sockObj = Socket::Create(inUseSocketList, this, sock, fam); //can't use FAMILY directly else undefined reference to `SocketManager::FAMILY' STRANGEST ERROR EVER, compiler bug ?
    }
    return sockObj;
}

bool SocketManager::AssociateSocketToIOCP(Socket *sockObj){
    HANDLE hrc = CreateIoCompletionPort((HANDLE)sockObj->s,          //FileHandle[in] : An open file handle. The handle must be to an object that supports overlapped I/O.
                                        iocpHandle,                  //ExistingCompletionPort[in, optional] : A handle to an existing I/O completion, the function associates it with the handle specified by the FileHandle parameter.
                                        (ULONG_PTR)sockObj,          //CompletionKey[in] : The per-handle user-defined completion key that is included in every I/O completion packet for the specified file handle.
                                        0);                          //NumberOfConcurrentThreads[in] : This parameter is ignored if the ExistingCompletionPort parameter is not NULL.
    if (hrc == nullptr) {
        LOG_ERROR("CreateIoCompletionPort failed / error %lu\n", GetLastError());
        Socket::Delete(sockObj);
        return false;
    }
    LOG("CreateIoCompletionPort ok\n");
    sockObj->state = Socket::SocketState::ASSOCIATED;
    return true;
}

bool SocketManager::BindSocket(Socket *sockObj, SOCKADDR_IN sockAddr){
    if (SetSocketOption(sockObj->s, SO_REUSE_UNICASTPORT, true) == SOCKET_ERROR || //works on Windows 10 only, use SO_PORT_SCALABILITY instead on Windows 7-8
        SetSocketOption(sockObj->s, SO_EXCLUSIVEADDRUSE, true) == SOCKET_ERROR)
        return false;
    if (bind(sockObj->s,                        //s : A descriptor identifying an unconnected socket.
             (SOCKADDR*)(&sockAddr),            //name : A pointer to a sockaddr structure that specifies the address to which to connect. For IPv4, the sockaddr contains AF_INET for the address family, the destination IPv4 address, and the destination port.
             sizeof(sockAddr)                   //namelen : The length, in bytes, of the sockaddr structure pointed to by the name parameter.
    ) == SOCKET_ERROR){
        LOG_ERROR("bind failed / error %d\n", WSAGetLastError());
        Socket::Delete(sockObj);
        return false;
    }
    LOG("bind ok\n");
    sockObj->state = Socket::SocketState::BOUND;
    return true;
}

int SocketManager::SetSocketOption(SOCKET s, int option, const char *optPtr, int optSize){
    int err = NO_ERROR;

    if(setsockopt(s,                                      //s : A descriptor that identifies a socket.
                  SOL_SOCKET,                             //level : The level at which the option is defined
                  option,                                 //optname : The socket option for which the value is to be set. The optname parameter must be a socket option defined within the specified level, or behavior is undefined.
                  optPtr,                                 //optval : A pointer to the buffer in which the value for the requested option is specified.
                  optSize                                 //optlen : The size, in bytes, of the buffer pointed to by the optval parameter.
    ) == SOCKET_ERROR){ //shouldn't ever happens
        err = WSAGetLastError();
        LOG_ERROR("setsockopt for option %d failed : %d\n", option, err);
    }
    return err;
}

int SocketManager::GetSocketOption(SOCKET s, int option, char *optPtr, int optSize){
    int err = NO_ERROR;

    if(getsockopt(s,                                      //s : A descriptor that identifies a socket.
                  SOL_SOCKET,                             //level : The level at which the option is defined
                  option,                                 //optname : The socket option for which the value is to be retrieved. The optname parameter must be a socket option defined within the specified level, or behavior is undefined.
                  optPtr,                                 //optval : A pointer to the buffer in which the value for the requested option is specified.
                  &optSize                                //optlen : A pointer to the size, in bytes, of the optval buffer.
    ) == SOCKET_ERROR){ //shouldn't ever happens
        err = WSAGetLastError();
        LOG_ERROR("getsockopt for option %d failed : %d\n", option, err);
    }
    return err;
}

void SocketManager::AddSocketToMap(Socket *sockObj, UUID id){
    RPC_STATUS status;
    if (UuidIsNil(&id, &status)) {
        status = UuidCreateSequential(&id);
        if (status == RPC_S_UUID_NO_ADDRESS)
            UuidCreate(&id);
    }
    sockObj->id = id;
    EnterCriticalSection(&socketAccessMap.critSec);
    {
        socketAccessMap.map[sockObj->id] = sockObj;
    }
    LeaveCriticalSection(&socketAccessMap.critSec);
}

UUID SocketManager::ConnectToNewSocket(const char *address, u_short port, UUID id) {
    int err;
    UUID nullId = Misc::CreateNilUUID();
    if (state < State::READY || type != Type::CLIENT)
        return nullId;

    // ----------------------------- create socket

    Socket *sockObj = GenerateSocket(true);
    if (sockObj == nullptr){
        return nullId;
    }
    SOCKET sock = sockObj->s;
    sockObj->address = address;
    sockObj->port = port;
    LOG("GetSocketObj ok\n");

    SOCKADDR_IN sockAddr;
    ZeroMemory(&sockAddr, sizeof(SOCKADDR_IN));
    sockAddr.sin_family = FAMILY;
    sockAddr.sin_addr.s_addr = INADDR_ANY;
    sockAddr.sin_port = 0;

    if (sockObj->state != Socket::SocketState::DISCONNECTED) { //if not recycled socket
        // ----------------------------- associate socket to IOCP
        if (!AssociateSocketToIOCP(sockObj)){
            return nullId;
        }

        // ----------------------------- bind socket
        if (!BindSocket(sockObj, sockAddr)){
            return nullId;
        }
    }

    // ----------------------------- connect socket
    sockAddr.sin_addr.s_addr = inet_addr(address);
    if(WSAHtons(sockObj->s, port, &sockAddr.sin_port) == SOCKET_ERROR) { // host-to-network-short: big-endian conversion of a 16 byte value
        //WSANOTINITIALISED, WSAENETDOWN, WSAENOTSOCK, WSAEFAULT
        LOG_ERROR("WSAHtonl failed / error %lu\n", GetLastError());
        Socket::Delete(sockObj);
        return nullId;
    }

    Buffer *connectObj = Buffer::Create(inUseBufferList, Buffer::Operation::Connect);
    if (!ConnectEx(sock,                        //s : A descriptor identifying an unconnected socket.
                   (SOCKADDR*)(&sockAddr),      //name : A pointer to a sockaddr structure that specifies the address to which to connect. For IPv4, the sockaddr contains AF_INET for the address family, the destination IPv4 address, and the destination port.
                   sizeof(sockAddr),            //namelen : The length, in bytes, of the sockaddr structure pointed to by the name parameter.
                   nullptr,                     //lpSendBuffer : A pointer to the buffer to be transferred after a connection is established. This parameter is optional.
                   0,                           //dwSendDataLength : The length, in bytes, of data pointed to by the lpSendBuffer parameter. This parameter is ignored when the lpSendBuffer parameter is NULL.
                   nullptr,                     //lpdwBytesSent : On successful return, this parameter points to a DWORD value that indicates the number of bytes that were sent after the connection was established. This parameter is ignored when the lpSendBuffer parameter is NULL.
                   &(connectObj->ol)            //lpOverlapped : An OVERLAPPED structure used to process the request. The lpOverlapped parameter must be specified, and cannot be NULL.
                  )) {
        if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
            LOG_ERROR("ConnectToNewSocket: ConnectEx failed: %d\n", err);
            Socket::Delete(sockObj);
            return nullId; // connect error
        }
    }
    LOG("ConnectEx ok\n");
    AddSocketToMap(sockObj, id);
    return sockObj->id;
}

bool SocketManager::AcceptNewSocket(Socket *listenSockObj){
    int err;
    Socket *acceptSockObj = GenerateSocket(true);
    if (acceptSockObj == nullptr){
        return false;
    }
    if (!AssociateSocketToIOCP(acceptSockObj)){
        return false;
    }
    currentAcceptSocket = acceptSockObj;

    Buffer *acceptObj = Buffer::Create(inUseBufferList, Buffer::Operation::Accept);
    if (!AcceptEx(listenSockObj->s,             //sListenSocket : A descriptor identifying a socket that has already been called with the listen function. A server application waits for attempts to connect on this socket.
                  acceptSockObj->s,             //sAcceptSocket : A descriptor identifying a socket on which to accept an incoming connection. This socket must not be bound or connected.
                  acceptObj->buf,               //lpOutputBuffer : A pointer to a buffer that receives the first block of data sent on a new connection, the local address of the server, and the remote address of the client. The receive data is written to the first part of the buffer starting at offset zero, while the addresses are written to the latter part of the buffer. This parameter must be specified.
                  0,                            //dwReceiveDataLength : The number of bytes in lpOutputBuffer that will be used for actual receive data at the beginning of the buffer. This size should not include the size of the local address of the server, nor the remote address of the client; they are appended to the output buffer. If dwReceiveDataLength is zero, accepting the connection will not result in a receive operation. Instead, AcceptEx completes as soon as a connection arrives, without waiting for any data.
                  sizeof(SOCKADDR_IN)+16,       //dwLocalAddressLength : The number of bytes reserved for the local address information. This value must be at least 16 bytes more than the maximum address length for the transport protocol in use.
                  sizeof(SOCKADDR_IN)+16,       //dwRemoteAddressLength : The number of bytes reserved for the remote address information. This value must be at least 16 bytes more than the maximum address length for the transport protocol in use. Cannot be zero.
                  nullptr,                      //lpdwBytesReceived : A pointer to a DWORD that receives the count of bytes received. This parameter is set only if the operation completes synchronously. If it returns ERROR_IO_PENDING and is completed later, then this DWORD is never set and you must obtain the number of bytes read from the completion notification mechanism.
                  &(acceptObj->ol)              //lpOverlapped : An OVERLAPPED structure used to process the request. The lpOverlapped parameter must be specified, and cannot be NULL.
    )) {
        if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
            LOG_ERROR("ConnectEx failed: %d\n", err);
            Socket::Delete(acceptSockObj);
            return false; // connect error
        }
    }
    LOG("AcceptEx ok\n");
    acceptSockObj->state = Socket::SocketState::ACCEPTING;
    return true;
}

UUID SocketManager::ListenToNewSocket(u_short port, bool fewCLientsExpected) {
    UUID nullId = Misc::CreateNilUUID();
    if (state != State::READY || type != Type::SERVER) //can't have several listen socket, create a manager for each
        return nullId;

    // ----------------------------- create socket

    Socket *listenSockObj = GenerateSocket(false);
    if (listenSockObj == nullptr){
        return nullId;
    }
    listenSockObj->port = port;
    LOG("GetSocketObj ok\n");

    SOCKADDR_IN sockAddr;
    ZeroMemory(&sockAddr, sizeof(SOCKADDR_IN));
    sockAddr.sin_family = FAMILY;
    sockAddr.sin_addr.s_addr = INADDR_ANY;
    if(WSAHtons(listenSockObj->s, port, &sockAddr.sin_port) == SOCKET_ERROR) { // host-to-network-short: big-endian conversion of a 16 byte value
        //WSANOTINITIALISED, WSAENETDOWN, WSAENOTSOCK, WSAEFAULT
        LOG_ERROR("WSAHtonl failed / error %lu\n", GetLastError());
        Socket::Delete(listenSockObj);
        return nullId;
    }

    // ----------------------------- associate socket to IOCP
    if (!AssociateSocketToIOCP(listenSockObj)){
        return nullId;
    }

    // ----------------------------- bind socket
    if (!BindSocket(listenSockObj, sockAddr)){
        return nullId;
    }
    // ----------------------------- listen
    if (listen(listenSockObj->s,                        //s : A descriptor identifying a bound, unconnected socket.
               fewCLientsExpected ? 5 : SOMAXCONN       //backlog : The maximum length of the queue of pending connections. If set to SOMAXCONN, the underlying service provider responsible for socket s will set the backlog to a maximum reasonable value.
    ) == SOCKET_ERROR){
        LOG_ERROR("listen failed / error %d\n", WSAGetLastError());
        Socket::Delete(listenSockObj);
        return nullId;
    }
    LOG("bind ok\n");
    listenSockObj->state = Socket::SocketState::LISTENING;

    // ----------------------------- start accepting sockets

    if (!AcceptNewSocket(listenSockObj)){
        Socket::Delete(listenSockObj);
        return nullId;
    }
    AddSocketToMap(listenSockObj, nullId);
    state = State::SERVER_LISTENING;
    return listenSockObj->id;
}

bool SocketManager::InitAsyncSocketFuncs() {
    //dummy socket to pass to WSAIoctl call
    SOCKET sock = WSASocket(FAMILY,                      //af : The address family specification
                            SOCK_STREAM,                 //type : SOCK_STREAM -> A socket type that provides sequenced, reliable, two-way, connection-based byte streams with an OOB data transmission mechanism. This socket type uses the Transmission Control Protocol (TCP) for the Internet address family (AF_INET or AF_INET6).
                            IPPROTO_TCP,                 //protocol : IPPROTO_TCP -> The Transmission Control Protocol (TCP). This is a possible value when the af parameter is AF_INET or AF_INET6 and the type parameter is SOCK_STREAM.
                            nullptr,                     //lpProtocolInfo : A pointer to a WSAPROTOCOL_INFO structure that defines the characteristics of the socket to be created.
                            0,                           //g : An existing socket group ID or an appropriate action to take when creating a new socket and a new socket group. 0 -> No group operation is performed.
                            0);                          //dwFlags : A set of flags used to specify additional socket attributes.
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("WSASocket failed / error %d\n", WSAGetLastError());
        return false;
    }
    return InitAsyncSocketFunc(sock, WSAID_CONNECTEX, &ConnectEx, sizeof(ConnectEx)) &&
           InitAsyncSocketFunc(sock, WSAID_ACCEPTEX, &AcceptEx, sizeof(AcceptEx)) &&
           InitAsyncSocketFunc(sock, WSAID_DISCONNECTEX, &DisconnectEx, sizeof(DisconnectEx));
}

bool SocketManager::InitAsyncSocketFunc(SOCKET sock, GUID guid, LPVOID func, DWORD size) {
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
        LOG_ERROR("WSAIoctl failed / error %d\n", WSAGetLastError());
        return false;
    }
    return true;
}

void SocketManager::InitTimeWaitValue() {
    DWORD   timeWaitValueFromRegistry;
    int     err = Misc::GetRegistryValue(TIME_WAIT_REG_KEY, TIME_WAIT_REG_VALUE, timeWaitValueFromRegistry);

    switch (err) {
        case NO_ERROR :{
            if (timeWaitValueFromRegistry < MIN_TIME_WAIT_VALUE)
                TimeWaitValue = MIN_TIME_WAIT_VALUE;
            else if (timeWaitValueFromRegistry > MAX_TIME_WAIT_VALUE)
                TimeWaitValue = MAX_TIME_WAIT_VALUE;
            else
                TimeWaitValue = timeWaitValueFromRegistry;
            break;
        }
        case ERROR_FILE_NOT_FOUND :{ // No value present in registry, use default
            TimeWaitValue = DEFAULT_TIME_WAIT_VALUE;
            break;
        }
        default: // Something went wrong, default to max value
            TimeWaitValue = MAX_TIME_WAIT_VALUE;
    }
}

Socket *SocketManager::ReuseSocket() {
    Socket *sockObj = nullptr;
    EnterCriticalSection(&reusableSocketQueue.critSec);
    {
        if (!reusableSocketQueue.queue.empty()){
            sockObj = reusableSocketQueue.queue.front();
            DWORD currentTime = GetTickCount();
            if(currentTime - sockObj->timeWaitStartTime > TimeWaitValue) {
                LOG("Recycling socket\n");
                sockObj->timeWaitStartTime = 0;
                reusableSocketQueue.queue.pop();
            } else {
                sockObj = nullptr;
            }
        }
    }
    LeaveCriticalSection(&reusableSocketQueue.critSec);
    return sockObj;
}

bool SocketManager::ShouldReuseSocket() {
    bool reuse;
    EnterCriticalSection(&reusableSocketQueue.critSec);
    {
        reuse = reusableSocketQueue.queue.size() < MAX_UNUSED_SOCKET;
    }
    LeaveCriticalSection(&reusableSocketQueue.critSec);
    return reuse;
}