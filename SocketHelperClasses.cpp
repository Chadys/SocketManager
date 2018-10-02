#include "SocketClient.h"


void Socket::Delete(Socket *obj) {
    EnterCriticalSection(&obj->SockCritSec);
    {
        // Close the socket if it hasn't already been closed
        if (obj->s != INVALID_SOCKET) {
            if (obj->state == CLOSING){
                _cprintf("Socket::Delete: disconnecting socket\n");
                obj->Disconnect();
                LeaveCriticalSection(&obj->SockCritSec);
                return;
            } else if (obj->state >= CONNECTED) { //CONNECTED or FAILURE
                _cprintf("Socket::Delete: closing socket\n");
                obj->Close();
            }
        }
    }
    LeaveCriticalSection(&obj->SockCritSec);

    ListElt<Socket>::Delete(obj);
}

void Socket::Disconnect() {
    int err;

    // ----------------------------- enqueue disconnect operation
    Buffer *disconnectobj = Buffer::Create(client->inUseBufferList, Buffer::Operation::Disconnect);
    if (!SocketClient::DisconnectEx(s,                           // hSocket : A handle to a connected, connection-oriented socket.
                                    &(disconnectobj->ol),        // lpOverlapped : A pointer to an OVERLAPPED structure. If the socket handle has been opened as overlapped, specifying this parameter results in an overlapped (asynchronous) I/O operation.
                                    TF_REUSE_SOCKET,             // dwFlags : A set of flags that customizes processing of the function call. TF_REUSE_SOCKET -> Prepares the socket handle to be reused. When the DisconnectEx request completes, the socket handle can be passed to the AcceptEx or ConnectEx function.
                                    0                            // reserved : Reserved. Must be zero. If nonzero, WSAEINVAL is returned.
    )) {
        if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
            _cprintf("Socket::Delete: DisconnectEx failed: %d\n", err);
            state = Socket::SocketState::FAILURE;
            LeaveCriticalSection(&SockCritSec);
            return Socket::Delete(this);
        }
    }
    _cprintf("DisconnectEx ok\n");
}

void Socket::Close() {
    int err;

    // ----------------------------- shutdown connexion
    if (shutdown(s, SD_SEND) == SOCKET_ERROR) {
        err = WSAGetLastError();
        _cprintf("Socket::Delete: shutdown failed / error %d\n", err);
        if (err == WSAEINPROGRESS) {
            LeaveCriticalSection(&SockCritSec);
            return;
        }
        // ------------------------- change socket option to trigger abortive close
        linger sl = { 1,                                    //l_onoff : non-zero value enables linger option in kernel.
                      0 };                                  //l_linger : timeout interval in seconds.
        if (setsockopt(s,                                   //s : A descriptor that identifies a socket.
                       SOL_SOCKET,                          //level : The level at which the option is defined.
                       SO_LINGER,                           //optname : The socket option for which the value is to be set. The optname parameter must be a socket option defined within the specified level, or behavior is undefined.
                       reinterpret_cast<char*>(&sl),        //optval  : A pointer to the buffer in which the value for the requested option is specified.
                       sizeof(sl)                           //optlen : The size, in bytes, of the buffer pointed to by the optval parameter.
        ) == SOCKET_ERROR ) {
            err = WSAGetLastError();
            _cprintf("Socket::Delete: setsockopt failed / error %d\n", err);
        }
    }
    // ----------------------------- graceful or abortive close depending on if shutdown failed
    if (closesocket(s) == SOCKET_ERROR) {
        err = WSAGetLastError();
        _cprintf("Socket::Delete: closesocket failed / error %d\n", err);
        if (err == WSAEINPROGRESS) {
            LeaveCriticalSection(&SockCritSec);
            return;
        }
    }
    s = INVALID_SOCKET;
}