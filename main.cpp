#include "SocketManager.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;

int main(int argc, char *argv[]){
    bool isServer = argc > 1;
    SocketManager    manager(isServer ? SocketManager::Type::SERVER : SocketManager::Type::CLIENT);
    UUID            socketId;
    RPC_STATUS      status;

    if(manager.isReady()) {
        socketId = isServer ? manager.ListenToNewSocket(port) : manager.ConnectToNewSocket(address, port);
        if (!UuidIsNil(&socketId, &status)) {
            for (;;) {
                if (!manager.isSocketReady(socketId)) {
                    socketId = isServer ? manager.ListenToNewSocket(port) : manager.ConnectToNewSocket(address, port);
                }
            }
        }
    }
}

