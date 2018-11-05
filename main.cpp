#include "SocketManager.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;

int main(int argc, char *argv[]){
    bool isServer = argc > 1;
    SocketManager    manager(isServer ? SocketManager::Type::SERVER : SocketManager::Type::CLIENT);
    UUID            socketId;
    RPC_STATUS      status;

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
}

