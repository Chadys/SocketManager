#include "SocketClient.h"

static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;

int main(){
    SocketClient client;
    if(client.isReady()) {
        if(client.ListenToNewSocket(address, port) != nullptr)
            for (;;);
    }
}

