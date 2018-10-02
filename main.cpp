#include "SocketClient.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;

int main(){
    SocketClient    client;
    Socket          *socket;
    if(client.isReady()) {
        if((socket = client.ListenToNewSocket(address, port)) != nullptr) {
            for (;;) {
                if (socket->IsDisconnected()) {
//                    Sleep(300000); // Sleep five minutes
                    socket = client.ListenToNewSocket(address, port);
                }
            }
        }
    }
}

