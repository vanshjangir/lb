use libc::{
    socket, accept, listen, recv, send, connect, setsockopt,
    sockaddr, sockaddr_in,
    AF_INET, SOCK_STREAM, SOL_SOCKET, SO_REUSEADDR, SO_REUSEPORT,
    epoll_create1, epoll_ctl, epoll_wait,
    epoll_event,
    EPOLL_CTL_ADD, EPOLL_CTL_MOD, EPOLLIN, EPOLLRDHUP, EPOLLONESHOT,
};
use std::{thread, mem, fs, env};
use std::os::unix::io::RawFd;
use std::sync::{Arc, Mutex, Condvar};
use std::collections::{VecDeque, HashSet, HashMap};
use std::net::Ipv4Addr;
use serde::Deserialize;
use serde_json;

const MAX_THREADS: i32 = 4;
const MAX_BACKLOG: i32 = 4;

#[derive(Debug)]
enum TaskType {
    Request,
    Response,
    NewConn
}

struct Task {
    fd: i32,
    ty: TaskType,
}

#[derive(Deserialize)]
struct PeerInfo {
    ip: String,
    port: i32,
}

fn init_epoll() -> RawFd {
    let epoll_fd = unsafe { epoll_create1(0) };
    if epoll_fd < 0 {
        panic!("Failed to initialize epoll");
    }
    epoll_fd
}

fn init_socket() -> RawFd {
    let sock_fd = unsafe { socket(AF_INET, SOCK_STREAM, 0) };
    if sock_fd < 0 {
        panic!("Failed to create socket");
    }
    sock_fd
}

fn get_next_server(
    backend_servers_mutex: Arc<Mutex<VecDeque<PeerInfo>>>
) -> PeerInfo {
    let mut backend_servers = backend_servers_mutex.lock().unwrap();
    let top_server = backend_servers.pop_front().unwrap();
    backend_servers.push_back(PeerInfo{
        ip: top_server.ip.clone(),
        port: top_server.port
    });
    drop(backend_servers);

    top_server
}

fn send_to_server(
    conn: PeerInfo,
    data: Vec<u8>,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
) -> i32 {

    let server_fd = init_socket();
    let ip_addr = conn.ip.parse::<Ipv4Addr>().expect("Invalid IP address");
    let octets = ip_addr.octets();

    let mut server_addr: sockaddr_in = unsafe { mem::zeroed() };
    server_addr.sin_family = AF_INET as u16;
    server_addr.sin_port = u16::to_be(conn.port as u16);
    server_addr.sin_addr.s_addr = u32::from_be_bytes(octets).to_be();

    let connect_result = unsafe {
        connect(
            server_fd,
            &server_addr as *const sockaddr_in as *const sockaddr,
            mem::size_of::<sockaddr_in>() as u32
        )
    };

    if connect_result < 0 {
        println!(
            "Failed to connect to server {}:{}-> {}", conn.ip, conn.port,
            std::io::Error::last_os_error()
        );
        unsafe { libc::close(server_fd) };
        return -1;
    }

    println!("Connected to server {}:{}", conn.ip, conn.port);

    let mut is_server = is_server_mutex.lock().unwrap();
    is_server.insert(server_fd);
    drop(is_server);

    let send_result = unsafe {
        send(
            server_fd,
            data.as_ptr() as *const libc::c_void,
            data.len(),
            0
        )
    };

    if send_result < 0 {
        println!(
            "Error sending data to server: {}",
            std::io::Error::last_os_error()
        );
        return -1;
    } else {
        println!("Sent {} bytes to server", send_result);
    }

    return server_fd;
}

fn rearm_to_epoll(epoll_fd: Arc<RawFd>, fd: i32, flags: i32) {
    let mut events: epoll_event = unsafe { mem::zeroed() };
    events.events = (flags) as u32;
    events.u64 = fd as u64;

    let epoll_ctl_result = unsafe {
        epoll_ctl(
            *epoll_fd,
            EPOLL_CTL_MOD,
            fd,
            &mut events
        )
    };
    
    if epoll_ctl_result < 0 {
        println!(
            "Error rearming fd to epoll: {}",
            std::io::Error::last_os_error()
        );
        unsafe { libc::close(fd) };
    } else {
        println!("Rearmed fd {fd} to epoll");
    }

}

fn add_to_epoll(epoll_fd: Arc<RawFd>, fd: i32, flags: i32){
    let mut events: epoll_event = unsafe { mem::zeroed() };
    events.events = flags as u32;
    events.u64 = fd as u64;

    let epoll_ctl_result = unsafe {
        epoll_ctl(
            *epoll_fd,
            EPOLL_CTL_ADD,
            fd,
            &mut events
        )
    };
    
    if epoll_ctl_result < 0 {
        println!(
            "Error adding fd to epoll: {}",
            std::io::Error::last_os_error()
        );
        unsafe { libc::close(fd) };
    } else {
        println!("Added fd {fd} to epoll");
    }
}

fn handle_request(
    client_fd: i32,
    epoll_fd: Arc<RawFd>,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
    backend_servers_mutex: Arc<Mutex<VecDeque<PeerInfo>>>,
    client_from_server_mutex: Arc<Mutex<HashMap<i32,i32>>>,
) {
    let mut buf = vec![0u8; 4096];
    let recv_result = unsafe {
        recv(
            client_fd,
            buf.as_mut_ptr() as *mut libc::c_void,
            buf.len(),
            0
        )
    };

    if recv_result <= 0 {
        println!(
            "Error occurred while receiving data or connection closed: {}",
            std::io::Error::last_os_error()
        );
        unsafe { libc::close(client_fd) };
        return;
    }

    buf.resize(recv_result as usize, 0);

    let server_data = get_next_server(backend_servers_mutex);
    let server_fd = send_to_server(server_data, buf, is_server_mutex);
    
    if server_fd != -1 {
        let mut client_from_server = client_from_server_mutex.lock().unwrap();
        client_from_server.insert(server_fd, client_fd);
        drop(client_from_server);
    }

    add_to_epoll(epoll_fd, server_fd, EPOLLIN | EPOLLONESHOT | EPOLLRDHUP);
}

fn send_to_client(client_fd: i32, data: Vec<u8>) {
    let send_result = unsafe {
        send(
            client_fd,
            data.as_ptr() as *const libc::c_void,
            data.len(),
            0
        )
    };

    if send_result < 0 {
        println!(
            "Error sending data to client: {}",
            std::io::Error::last_os_error()
        );
    } else {
        println!("Sent {} bytes to client", send_result);
    }
}

fn handle_response(
    server_fd: i32,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
    client_from_server_mutex: Arc<Mutex<HashMap<i32,i32>>>,
) {
    let mut buf = vec![0u8; 4096];
    let recv_result = unsafe {
        recv(
            server_fd,
            buf.as_mut_ptr() as *mut libc::c_void,
            buf.len(),
            0
        )
    };

    if recv_result <= 0 {
        println!(
            "Error occurred while receiving data or connection closed: {}",
            std::io::Error::last_os_error()
        );
    } else {
        buf.resize(recv_result as usize, 0);

        let client_from_server = client_from_server_mutex.lock().unwrap();
        if let Some(&client_fd) = client_from_server.get(&server_fd) {
            drop(client_from_server);
            send_to_client(client_fd, buf);
        } else {
            drop(client_from_server);
            println!("No client found for server {}", server_fd);
        }
    }

    let mut is_server = is_server_mutex.lock().unwrap();
    unsafe { libc::close(server_fd); }
    is_server.remove(&server_fd);
    drop(is_server);

    let mut client_from_server = client_from_server_mutex.lock().unwrap();
    client_from_server.remove(&server_fd);
    drop(client_from_server);
}

fn task_handler(
    task_queue: Arc<(Mutex<VecDeque<Task>>, Condvar)>,
    epoll_fd: Arc<RawFd>,
    backend_servers_mutex: Arc<Mutex<VecDeque<PeerInfo>>>,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
    client_from_server_mutex: Arc<Mutex<HashMap<i32,i32>>>,
) {
    let (queue_mutex, condvar) = &*task_queue;

    loop {
        let mut task_queue = queue_mutex.lock().unwrap();

        while task_queue.is_empty() {
            task_queue = condvar.wait(task_queue).unwrap();
        }

        if let Some(task) = task_queue.pop_front() {
            drop(task_queue);
            match task.ty {
                TaskType::Request => handle_request(
                    task.fd,
                    Arc::clone(&epoll_fd),
                    Arc::clone(&is_server_mutex),
                    Arc::clone(&backend_servers_mutex),
                    Arc::clone(&client_from_server_mutex),
                ),
                TaskType::Response => handle_response(
                    task.fd,
                    Arc::clone(&is_server_mutex),
                    Arc::clone(&client_from_server_mutex),
                ),
                TaskType::NewConn => {
                    accept_and_add(Arc::clone(&epoll_fd), task.fd);
                },
            }
        } else {
            drop(task_queue);
        }
    }
}

fn set_non_blocking(fd: i32) {
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL, 0);
        if flags < 0 {
            println!(
                "Error getting socket flags: {}",
                std::io::Error::last_os_error()
            );
            libc::close(fd);
            return;
        }
        
        let result = libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
        if result < 0 {
            println!(
                "Error setting socket to non-blocking mode: {}",
                std::io::Error::last_os_error()
            );
            libc::close(fd);
        }
    }
}

fn accept_and_add(epoll_fd: Arc<RawFd>, listener_fd: RawFd) {
    let mut client_addr: sockaddr_in = unsafe { mem::zeroed() };
    let mut addr_len = mem::size_of::<sockaddr_in>() as u32;

    let client_fd = unsafe {
        accept(
            listener_fd,
            &mut client_addr as *mut sockaddr_in as *mut sockaddr,
            &mut addr_len
        )
    };
    if client_fd < 0 {
        println!(
            "Error accepting connection: {}",
            std::io::Error::last_os_error()
        );
        return;
    }

    set_non_blocking(client_fd);

    add_to_epoll(
        Arc::clone(&epoll_fd),
        client_fd,
        EPOLLIN | EPOLLRDHUP | EPOLLONESHOT
    );

    rearm_to_epoll(
        Arc::clone(&epoll_fd),
        listener_fd,
        EPOLLIN | EPOLLONESHOT | EPOLLRDHUP
    );
}

fn event_loop(
    task_queue: Arc<(Mutex<VecDeque<Task>>, Condvar)>,
    epoll_fd: Arc<RawFd>,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
    listener_fd: i32,
) {
    let (queue_mutex, condvar) = &*task_queue;
    let mut events: [epoll_event; 10] = unsafe { mem::zeroed() };

    loop {
        let events_count = unsafe {
            epoll_wait(
                *epoll_fd,
                events.as_mut_ptr(),
                events.len() as i32,
                -1,
            )
        };

        if events_count < 0 {
            println!(
                "Error in epoll_wait: {}",
                std::io::Error::last_os_error()
            );
            continue;
        }

        if events_count == 0 {
            continue;
        }

        let mut task_queue = queue_mutex.lock().unwrap();
        let is_server = is_server_mutex.lock().unwrap();

        for i in 0..events_count as usize {
            let event_fd = events[i].u64 as i32;
            let mut task = Task {fd: event_fd, ty: TaskType::NewConn};
            
            if event_fd == listener_fd {
                if (events[i].events & EPOLLIN as u32) != 0 {
                    task.ty = TaskType::NewConn;
                    task_queue.push_back(task);
                    condvar.notify_one();
                }
            } else if (events[i].events & EPOLLIN as u32) != 0 {
                if is_server.contains(&event_fd) {
                    task.ty = TaskType::Response;
                } else {
                    task.ty = TaskType::Request;
                }
                task_queue.push_back(task);
                condvar.notify_one();
            } else if (events[i].events & EPOLLRDHUP as u32) != 0 {
                unsafe { libc::close(event_fd) };
            }
        }

        drop(is_server);
        drop(task_queue);
    }
}

fn set_socket_reuse(fd: i32) {
    unsafe {
        let reuse: i32 = 1;
        let result = setsockopt(
            fd, SOL_SOCKET, SO_REUSEADDR,
            &reuse as *const _ as *const _, mem::size_of::<i32>() as u32
        );
        if result < 0 {
            println!(
                "Error setting SO_REUSEADDR opt: {}",
                std::io::Error::last_os_error()
            );
            return;
        }

        let result = setsockopt(
            fd, SOL_SOCKET, SO_REUSEPORT,
            &reuse as *const _ as *const _, mem::size_of::<i32>() as u32
        );
        if result < 0 {
            println!(
                "Error setting SO_REUSEPORT opt: {}",
                std::io::Error::last_os_error()
            );
            return;
        }
    }
}

fn epoll_listener(
    task_queue: Arc<(Mutex<VecDeque<Task>>, Condvar)>,
    epoll_fd: Arc<RawFd>,
    is_server_mutex: Arc<Mutex<HashSet<i32>>>,
) {
    let listener_fd = init_socket();

    let mut server_addr: sockaddr_in = unsafe { mem::zeroed() };
    server_addr.sin_family = AF_INET as u16;
    server_addr.sin_port = 8080u16.to_be();
    server_addr.sin_addr.s_addr = libc::INADDR_ANY.to_be();

    set_socket_reuse(listener_fd);

    let bind_result = unsafe {
        libc::bind(
            listener_fd,
            &server_addr as *const sockaddr_in as *const sockaddr,
            mem::size_of::<sockaddr_in>() as u32
        )
    };
    if bind_result < 0 {
        panic!("Failed to bind socket: {}", std::io::Error::last_os_error());
    }

    let listen_result = unsafe {
        listen(listener_fd, MAX_BACKLOG)
    };
    if listen_result < 0 {
        panic!("Error in listen: {}", std::io::Error::last_os_error());
    }
    println!("Listening on port {}", 8080);

    add_to_epoll(Arc::clone(&epoll_fd), listener_fd, EPOLLIN | EPOLLONESHOT);
    println!("Added listener to epoll");

    event_loop(
        Arc::clone(&task_queue),
        Arc::clone(&epoll_fd),
        Arc::clone(&is_server_mutex),
        listener_fd,
    );
}

fn load_server_data(file_path: Option<&str>) -> VecDeque<PeerInfo> {
    let path = file_path.unwrap_or("file.json");
    match fs::read_to_string(path) {
        Ok(data) => match serde_json::from_str(&data) {
            Ok(servers) => servers,
            Err(e) => {
                eprintln!("Error parsing server data from {}: {}", path, e);
                VecDeque::from(vec![
                    PeerInfo { ip: "127.0.0.1".to_string(), port: 8081 },
                    PeerInfo { ip: "127.0.0.1".to_string(), port: 8082 }
                ])
            }
        },
        Err(e) => {
            eprintln!("Error reading server data file {}: {}", path, e);
            VecDeque::from(vec![
                PeerInfo { ip: "127.0.0.1".to_string(), port: 8081 },
                PeerInfo { ip: "127.0.0.1".to_string(), port: 8082 }
            ])
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let server_file = args.get(1).map(|s| s.as_str());

    let mut thread_handles = vec![];
    let is_server_mutex = Arc::new(Mutex::new(HashSet::<i32>::new()));
    let epoll_fd = Arc::new(init_epoll());

    let task_queue = Arc::new((Mutex::new(
        VecDeque::<Task>::new()),
        Condvar::new()
    ));

    let client_from_server_mutex = Arc::new(Mutex::new(
        HashMap::<i32,i32>::new()));
    let backend_servers_mutex = Arc::new(Mutex::new(
        VecDeque::<PeerInfo>::from(load_server_data(server_file))
    ));

    for _ in 0..MAX_THREADS {
        let epoll_fd = Arc::clone(&epoll_fd);
        let task_queue = Arc::clone(&task_queue);
        let backend_servers_mutex = Arc::clone(&backend_servers_mutex);
        let is_server_mutex = Arc::clone(&is_server_mutex);
        let client_from_server_mutex = Arc::clone(&client_from_server_mutex);

        let handle = thread::spawn(move || {
            task_handler(
                task_queue,
                epoll_fd,
                backend_servers_mutex,
                is_server_mutex,
                client_from_server_mutex,
            );
        });
        thread_handles.push(handle);
    }

    let listen_handler = thread::spawn(move || {
        epoll_listener(
            Arc::clone(&task_queue),
            Arc::clone(&epoll_fd),
            Arc::clone(&is_server_mutex),
        );
    });

    listen_handler.join().unwrap();
    for handle in thread_handles {
        handle.join().unwrap();
    }
}
