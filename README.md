A multi-threaded, event based load balancer written from scratch in C++ using socket api. \
Uses thread pooling and a very basic implementation of event loop using epoll to select readable and writable file descriptors.

### Installation
```
git clone https://github.com/vanshjangir/lb
cd lb
make
./build/lb
```

### Usage
Commands are passed through a CLI.
* Add a list of servers to send the incoming requests.
```
add <server_ip> <server_port>
```
* List the servers with their ip address, port and avg latency
```
list
```
* Start and stop the server(up/down)
```
up/down
```
* Exit
```
exit
```
### Todo
- [ ] Sticky sessions
- [ ] Direct Server Return (Layer 3)
