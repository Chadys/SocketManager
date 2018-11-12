#include "SocketManager.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;
#define N 3001

int main(int argc, char *argv[]){
//    bool isServer = argc > 1;
//    SocketManager   manager(isServer ? SocketManager::Type::SERVER : SocketManager::Type::CLIENT);
    SocketManager   serverManager(SocketManager::Type::SERVER);
    SocketManager   clientManager(SocketManager::Type::CLIENT);
    UUID            serverSocketId, socketId[N];
    RPC_STATUS      status;

    if(serverManager.isReady()) {
        serverSocketId = serverManager.ListenToNewSocket(port);
        if (UuidIsNil(&serverSocketId, &status))
            return 1;
    } else
        return 1;
    if(clientManager.isReady()) {
        for(int i = 0 ; i < N ; i++) {
            socketId[i] = clientManager.ConnectToNewSocket(address, port);
            if (UuidIsNil(socketId+i, &status))
                return 1;
        }
    } else
        return 1;

    for (;;) {
        if (!serverManager.isServerSocketReady(serverSocketId)) {
            return 1;
        }
        for (auto &id : socketId) {
            if (!clientManager.isClientSocketReady(id)) {
//                return 1;
            }
        }
        clientManager.SendDataToAll("ping\n", 5);
    }
/*
    if(manager.isReady()) {
        if (isServer) {
            socketId = manager.ListenToNewSocket(port);
            if (!UuidIsNil(&socketId, &status)) {
                for (;;) {
                    if (!manager.isServerSocketReady(socketId)) {
                        socketId = manager.ListenToNewSocket(port);
                    }
                }
            }
        } else {
            socketId = manager.ConnectToNewSocket(address, port);
            if (!UuidIsNil(&socketId, &status)) {
                for (;;) {
                    if (!manager.isClientSocketReady(socketId)) {
                        socketId = manager.ConnectToNewSocket(address, port);
                    }
                }
            }
        }
    }
*/
}

