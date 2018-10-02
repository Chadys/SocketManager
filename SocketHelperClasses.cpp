#include "SocketClient.h"


void Socket::Disconnect() {
    EnterCriticalSection(&SockCritSec);
    {
        int err;
        _cprintf("Socket::Delete: disconnecting socket\n");

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
    LeaveCriticalSection(&SockCritSec);
}

void Socket::Delete(Socket *obj) {
    EnterCriticalSection(&obj->SockCritSec);
    {
        // Close the socket if it hasn't already been closed
        if (obj->s != INVALID_SOCKET) {
            if (obj->state == CLOSING){
                LeaveCriticalSection(&obj->SockCritSec);
                return obj->Disconnect();
            }
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
    }
    LeaveCriticalSection(&obj->SockCritSec);

    ListElt<Socket>::Delete(obj);
}