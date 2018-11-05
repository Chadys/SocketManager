#include "SocketManager.h"


void Socket::Delete(Socket *obj) {
    EnterCriticalSection(&obj->SockCritSec);
    {
        // Close the socket if it hasn't already been closed
        if (obj->s != INVALID_SOCKET && (obj->state == CONNECTED || obj->state == FAILURE)) {
            _cprintf("Socket::Delete: closing socket\n");
            obj->Close(obj->state == FAILURE);
        }
    }
    LeaveCriticalSection(&obj->SockCritSec);
    ListElt<Socket>::Delete(obj);
}

void Socket::DeleteOrDisconnect(Socket *obj, CriticalMap<UUID, Socket*> &critMap) {
    bool needDelete;

    EnterCriticalSection(&obj->SockCritSec);
    {
        // Close the socket if it hasn't already been closed
        if (obj->state < DISCONNECTING) {
            switch (obj->state){
                case CLOSING : {
                    if (obj->client->ShouldReuseSocket()) {
                        _cprintf("Socket::Delete: disconnecting socket\n");
                        obj->Disconnect(critMap);
                        return;
                    }
                    /** NOBREAK **/
                }
                case CONNECTED :{
                    _cprintf("Socket::Delete: closing socket\n");
                    obj->Close(false);
                    break;
                }
                case FAILURE : /** NOBREAK **/
                case LISTENING :{
                    _cprintf("Socket::Delete: closing socket\n");
                    obj->Close(true);
                    break;
                }
            }
        }
        if (obj->state != RETRY_CONNECTION) {
            EnterCriticalSection(&critMap.critSec);
            {
                critMap.map.erase(obj->id);
            }
            LeaveCriticalSection(&critMap.critSec);
        }
        needDelete = obj->state == CLOSED || obj->state == RETRY_CONNECTION;
    }
    LeaveCriticalSection(&obj->SockCritSec);
    if (needDelete)
        ListElt<Socket>::Delete(obj);
}

void Socket::Disconnect(CriticalMap<UUID, Socket*> &critMap) {
    int err;

    // ----------------------------- enqueue disconnect operation
    Buffer *disconnectobj = Buffer::Create(client->inUseBufferList, Buffer::Operation::Disconnect);
    if (!SocketManager::DisconnectEx(s,                           // hSocket : A handle to a connected, connection-oriented socket.
                                    &(disconnectobj->ol),        // lpOverlapped : A pointer to an OVERLAPPED structure. If the socket handle has been opened as overlapped, specifying this parameter results in an overlapped (asynchronous) I/O operation.
                                    TF_REUSE_SOCKET,             // dwFlags : A set of flags that customizes processing of the function call. TF_REUSE_SOCKET -> Prepares the socket handle to be reused. When the DisconnectEx request completes, the socket handle can be passed to the AcceptEx or ConnectEx function.
                                    0                            // reserved : Reserved. Must be zero. If nonzero, WSAEINVAL is returned.
    )) {
        if ((err = WSAGetLastError()) != WSA_IO_PENDING) {
            _cprintf("Socket::Delete: DisconnectEx failed: %d\n", err);
            state = Socket::SocketState::FAILURE;
            LeaveCriticalSection(&SockCritSec);
            return Socket::DeleteOrDisconnect(this, critMap);
        }
    }
    state = Socket::SocketState::DISCONNECTING;
    LeaveCriticalSection(&SockCritSec);
    _cprintf("DisconnectEx ok\n");
}

void Socket::Close(bool forceClose) {
    int err;

    if (!forceClose) {
    // ----------------------------- shutdown connexion
        if (shutdown(s, SD_SEND) == SOCKET_ERROR) {
            err = WSAGetLastError();
            _cprintf("Socket::Delete: shutdown failed / error %d\n", err);
            if (err == WSAEINPROGRESS) {
                return;
            }
            forceClose = true;
        }
    }
    if (forceClose) {
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
            return;
        }
    }
    s = INVALID_SOCKET;
    state = CLOSED;
}