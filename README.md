A multi-threaded, event based load balancer written using libc's socket api.\
[C++ implementation](https://github.com/vanshjangir/lb/tree/c%2B%2B)
### How to run
```
git clone https://github.com/vanshjangir/lb
cd lb
cargo run . <json path for backend server's ip addresses and ports>
```
### JSON structure
```
[
    {
        "ip": <ip_address>,
        "port": <port>,
    },
    .
    .
]
```
