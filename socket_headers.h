#ifndef SOCKETMANAGER_SOCKET_HEADERS_H
#define SOCKETMANAGER_SOCKET_HEADERS_H

#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <conio.h>

#ifndef SO_REUSE_UNICASTPORT //because ws2def.h of mingw64 is incomplete
#define SO_REUSE_UNICASTPORT 0x3007
#endif //SO_REUSE_UNICASTPORT

#ifdef HAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS //because ws2tcpip.h of mingw64 is incomplete
#include <ws2tcpip.h>
#else //HAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS
#include <ws2ipdef.h>

#define SIO_IDEAL_SEND_BACKLOG_QUERY   _IOR('t', 123, ULONG)
#define SIO_IDEAL_SEND_BACKLOG_CHANGE   _IO('t', 122)

//
// Wrapper functions for the ideal send backlog query and change notification
// ioctls
//

WS2TCPIP_INLINE
int
idealsendbacklogquery(
        _In_ SOCKET s,
        _Out_ ULONG *pISB
)
{
    DWORD bytes;

    return WSAIoctl(s, SIO_IDEAL_SEND_BACKLOG_QUERY,
                    nullptr, 0, pISB, sizeof(*pISB), &bytes, nullptr, nullptr);
}


WS2TCPIP_INLINE
int
idealsendbacklognotify(
        _In_ SOCKET s,
        _In_opt_ LPWSAOVERLAPPED lpOverlapped,
        _In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
)
{
    DWORD bytes;

    return WSAIoctl(s, SIO_IDEAL_SEND_BACKLOG_CHANGE,
                    nullptr, 0, nullptr, 0, &bytes,
                    lpOverlapped, lpCompletionRoutine);
}

#endif //HAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS

#endif //SOCKETMANAGER_SOCKET_HEADERS_H
