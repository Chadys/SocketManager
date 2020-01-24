# Presentation

This project can be used to create high performance C++ socket server or client in Windows.

Unfortunately, unlike in other, more modern languages, the standard lib in C++ still doesn't provide a cross-platform abstraction over sockets. So every developer keeps reinventing the wheel with their own implementation. This is my take on the Windows version.

I tried to use every tools I could from the Windows API and to optimize some Windows specific socket options to make my implementation efficient. I looked at the Google and Python implementation of Windows socket for some inspiration in the useful options.

#Usage

To use this lib, you need to copy every files other than [main.cpp](main.cpp) in your project and include [SocketManager.h](SocketManager.h) where you want to use it.
`SocketManager` is an abstract class, so you need to create a class that inherit from it.
You won't be able to directly manipulate `Socket` objects, instead, you'll use the manager you created for all operations, by giving it the unique id it previously provided to identify the socket you want to make the call on.

## Logs

Two types of debug log type can be activated: simple log or error log. They're both displayed in the console, either in the standard output stream or the error one. To disable them, comment out the lines `#define DEBUG` and/or `#define DEBUG_ERROR` in [Misc.h](Misc.h)

## Methods
- `constructor(Type t, unsigned short factor = 0)` *override*

The `constructor` can be overridden simply:
```c++
explicit SocketManagerImplExample(Type t) : SocketManager(t) {}
```
`t` is the type of the `SocketManager` you want to create, either `SocketManager::Type::SERVER` or `SocketManager::Type::CLIENT`.
The constructor contains another, optional argument, `factor`. When this argument is 0 (default value) the maximum pending sent bytes you can have for one socket is 64kB which is a good default on modern computers.
For any other value, the program will query the ideal send backlog (ISB) size (aka "optimal amount of send data that needs to be kept outstanding") and to use it to change the maximum pending sent bytes you can have.
The send buffer will be modified to equal the ISB and the maximum pending bytes will be ISB*`factor`.
The ISB is dynamic and can change depending on the connexion performance and the application will respond to these changes. Either 0 or 1 are good values for the `factor` parameter.

- `int ReceiveData(const char* data, u_long length, Socket *socket)` *override*

The only method beside the constructor that you need to overwrite is `ReceiveData`. It has three arguments: `data` and `length`, that you use to know what you've been sent, and `socket` that you can use for any other operation you need to do (either respond by sending more data or close socket).

There is a subtlety on this implementation that you need to take care of: when the data that you'll receive is marshalled, a single write can be transformed in several reads for you.
But don't worry, the data are guaranteed to be received in order. That still means however that you need to have a solid protocol to be able to detect the true end of a single message.
You can, for example, create an internal `map<Socket*, string>` that you'll fill in each `ReceiveData` until the end of the message is reached (without forgetting to use a `CRITICAL_SECTION` if needed).
Don't worry about concatenating the data if you expect to receive only very small messages.

For the read operation to always arrive in order, a pending read is only emitted when the previous one is finished, so don't make `ReceiveData` a long operation.

- `void CloseSocket(Socket *sock)` *protected*

Manually close socket, this method is protected so it can only be called from `ReceiveData`.

- `bool SendData(const char *data, u_long length, Socket *socket)` *protected*

Send data through a socket, this method is protected so it can only be called from `ReceiveData`.
Return false if socket is not connected or if the maximum number of pending sends was reached. Returns true otherwise, even if the send operation itself failed.

- `UUID ListenToNewSocket (u_short port, bool fewCLientsExpected = false)` *public*

Use that function once you have a server manager to start listening on a given port. You can only have one listening socket on a given manager.
If `fewCLientsExpected` is true, the maximum length of the queue of pending connections will be 5. Else (default) the underlying service provider responsible for socket will set the backlog to a maximum reasonable value.
Return a Nil UUID on failure, UUID of socket on success. You can test the success of this function with `UuidIsNil`.

- `UUID ConnectToNewSocket (const char *address, u_short port)` *public*

Use that function for your client manager to connect to the server at address:port.
Return a Nil UUID on failure, UUID of socket on success. You can test the success of this function with `UuidIsNil`.

- `bool         isReady                 () const` *public*

Use to check that the initialisation of the manager went well and that you can use it to open a new socket.
Always check it before using `ListenToNewSocket` or `ConnectToNewSocket`.
Return false if an error was raised in the construction of the manager, or, in case of a server manager, if the manager already has an accept socket listening.

- `bool         isClientSocketReady     (UUID socketId)` *public*

Check to see if the client socket identified by its `socketId` is ready to send data. Always check before calling `SendData`.

- `bool         isServerSocketReady     (UUID socketId)` *public*

Check to see if the server socket identified by its `socketId` is ready to send data. Always check before calling `SendDataToAll`.

- `bool         isSocketInitialising    (UUID socketId)` *public*

If either `isClientSocketReady` or `isServerSocketReady` returned false, you can check if it's because the socket is in an error state (then you need to discard it), or because it didn't finish initialising yet and you need to wait a little.
If `isSocketInitialising` returns false, discard the socket, else wait.

- `bool         SendData                (const char *data, u_long length, UUID socketId)` *public*

Send data to the specified client socket. Can only be used with client manager.
Return false if invalid `socketId` is given or if the maximum number of pending sends was reached. Returns true otherwise, even if the send operation itself failed.
The data sent is cut if needed in smaller packages of `DEFAULT_BUFFER_SIZE`, which is 4kB.

- `int          SendDataToAll           (const char *data, u_long length)` *public*

Send data to all client sockets currently connected to this server manager.
Returns number of send done, using same success definition of a send as `SendData`.
The data sent is cut if needed in smaller packages of `DEFAULT_BUFFER_SIZE`, which is 4kB.
    

# Sample && benchmarks
The file [main.cpp](main.cpp) contains an example of how you can use the `SocketManager` class. It contains a function `pingpongStressTest` to test performance with a server and N number of clients, the server sending "ping" as fast as possible to all its clients and all clients responding with "pong".
This program was tested with N=10_000 for a couple hours and no memory or latency problem was noted.

This code was written for Windows 10, so minor adjustment might be necessary to make it work on previous version (for example in Windows 7-8 you need to replace `SO_REUSE_UNICASTPORT` with `SO_PORT_SCALABILITY` in [SocketManager.cpp](SocketManager.cpp)).

Also no unit test were written, and no integration test to check bugs in specific use cases were done.
If you are searching for a robust opensource Windows solution for efficient socket handling, you might be better off looking at projects like [The Free Framework](http://www.serverframework.com/products---the-free-framework.html).

# Implementation details

I used an IOCP to manage the threads pool that will execute all socket operations (see [here](https://www.codeproject.com/Articles/10330/A-simple-IOCP-Server-Client-Class) for an explanation on how to do that).
All operations are queued asynchronously.

There is no direct access to the `Socket` object possessed by the manager, because sockets can be closed anytime, which could lead to an invalid pointer reference.
Instead, all public functions of the manager fetch socket from an internal `map<UUID, Socket*>`.
The only place you can manipulate `Socket` directly is in your override of `ReceiveData`, where the `Socket*` is guaranteed to be valid.

Linked lists are used internally in the `SocketManager` because they are the only type of container in the standard library that guarantees none of its element will ever be moved after allocation, no matter what's done to the container. I needed that constraint.