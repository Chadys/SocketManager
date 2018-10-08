#include "SocketClient.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;

int main(){
    SocketClient    client;
    UUID            socketId;
    RPC_STATUS      status;

    if(client.isReady()) {
        socketId = client.ListenToNewSocket(address, port);
        if (!UuidIsNil(&socketId, &status)) {
            for (;;) {
                if (!client.isSocketReady(socketId)) {
                    socketId = client.ListenToNewSocket(address, port);
                }
            }
        }
    }
}

