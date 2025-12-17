/*
 * C++17 Chord DHT Node (Boost-free, Winsock2, CRITICAL_SECTION, Win32 threads)
 * -----------------------------------------------------------------------------
 * - DataStore: thread-safe key/value store via Windows CRITICAL_SECTION
 * - NodeInfo: IP, port, ID
 * - FingerTable: routing table entries
 * - RequestHandler: synchronous TCP client via Winsock2
 * - Node: Chord logic + RPC server
 *
 * Networking: Winsock2 blocking sockets
 * Threading: Win32 CreateThread
 * Synchronization: CRITICAL_SECTION
 * ID hashing: std::hash<string> % (2^m), m=7
 * Compile:
 *   g++ -std=c++17 chord_node_nomboost.cpp -lws2_32 -o chord_node
 */

 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <windows.h>
 #include <iostream>
 #include <string>
 #include <vector>
 #include <unordered_map>
 #include <sstream>
 #include <functional>
 #include <cstdlib>   // for rand, srand
 #include <ctime>     // for time()
 
 #pragma comment(lib, "Ws2_32.lib")
 
 static constexpr int m = 7;
 static constexpr int RING_SIZE = 1 << m;
 
 enum { STACK_SIZE = 0 };
 
 // Lightweight Mutex using CRITICAL_SECTION
 class Mutex {
     CRITICAL_SECTION cs;
 public:
     Mutex()  { InitializeCriticalSection(&cs); }
     ~Mutex() { DeleteCriticalSection(&cs); }
     void lock()   { EnterCriticalSection(&cs); }
     void unlock() { LeaveCriticalSection(&cs); }
 };
 
 // LockGuard for our Mutex
 class LockGuard {
     Mutex &m;
 public:
     LockGuard(Mutex &m_) : m(m_) { m.lock(); }
     ~LockGuard()           { m.unlock(); }
 };
 
 // Thread-safe key/value store
 class DataStore {
 protected:
     std::unordered_map<std::string, std::string> data_;
     Mutex mu_;
 public:
     virtual ~DataStore() = default;
     virtual int self_id() const { return 0; }
 
     void insert(const std::string &k, const std::string &v) {
         LockGuard lock(mu_);
         data_[k] = v;
     }
     void remove(const std::string &k) {
         LockGuard lock(mu_);
         data_.erase(k);
     }
     std::string search(const std::string &k) {
         LockGuard lock(mu_);
         auto it = data_.find(k);
         return it != data_.end() ? it->second : std::string();
     }
     // send_keys: send key|value pairs where key_id <= joining_id
     std::string send_keys(int joining_id) {
         LockGuard lock(mu_);
         std::ostringstream oss;
         std::vector<std::string> to_remove;
         for (auto &p : data_) {
             int key_id = static_cast<int>(std::hash<std::string>{}(p.first) % RING_SIZE);
             int dist_to_join = (joining_id - key_id + RING_SIZE) % RING_SIZE;
             int dist_to_self = (self_id() - key_id + RING_SIZE) % RING_SIZE;
             if (dist_to_join < dist_to_self) {
                 oss << p.first << "|" << p.second << ":";
                 to_remove.push_back(p.first);
             }
         }
         for (auto &k : to_remove) data_.erase(k);
         return oss.str();
     }
 };
 
 // Node identity
 struct NodeInfo {
     std::string ip;
     int port;
     int id;
     NodeInfo() : ip(), port(0), id(-1) {}
     NodeInfo(std::string ip_, int port_, int id_) : ip(std::move(ip_)), port(port_), id(id_) {}
     std::string str() const { return ip + "|" + std::to_string(port); }
 };
 
 // Finger table entries
 class FingerTable {
 public:
     std::vector<std::pair<int, NodeInfo>> table;
     FingerTable(int self_id) {
         table.reserve(m);
         for (int i = 0; i < m; ++i) {
             int start = (self_id + (1 << i)) % RING_SIZE;
             table.emplace_back(start, NodeInfo());
         }
     }
     void print() const {
         for (int i = 0; i < m; ++i) {
             auto &e = table[i];
             std::cout << "Entry[" << i << "] start=" << e.first
                       << " succ_id=" << e.second.id << "\n";
         }
     }
 };
 
 // Simple blocking RPC client using Winsock2
 class RequestHandler {
 public:
     std::string send_message(const std::string &ip, int port, const std::string &msg) {
         SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
         if (sock == INVALID_SOCKET) return {};
         sockaddr_in srv{};
         srv.sin_family = AF_INET;
         srv.sin_addr.s_addr = inet_addr(ip.c_str());
         srv.sin_port = htons(port);
         if (::connect(sock, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) != 0) {
             closesocket(sock);
             return {};
         }
         std::string out = msg + "\n";
         send(sock, out.c_str(), static_cast<int>(out.size()), 0);
         char buf[1024];
         int r = recv(sock, buf, sizeof(buf), 0);
         std::string resp;
         if (r > 0) resp.assign(buf, r);
         closesocket(sock);
         return resp;
     }
 };
 
 // Forward declare for thread procedures
 class Node;

 // Chord node implementation
 class Node : public DataStore {
     NodeInfo self_, pred_, succ_;
     FingerTable fingers_;
     RequestHandler rpc_;
 
 public:
     Node(const std::string &ip, int port)
         : self_{ip, port, hash_str(ip + "|" + std::to_string(port))}
         , pred_(), succ_(self_)
         , fingers_(self_.id)
     {}
 
     static int hash_str(const std::string &s) {
         return static_cast<int>(std::hash<std::string>{}(s) % RING_SIZE);
     }
     int self_id() const override { return self_.id; }
 
     std::string process_request(const std::string &msg);
     void start();
     // In the public section of class Node
    void bootstrap(const std::string &contact_ip, int contact_port);

 private:
     static std::vector<std::string> split(const std::string &s, char d) {
         std::vector<std::string> v;
         std::istringstream iss(s);
         std::string t;
         while (std::getline(iss, t, d)) v.push_back(t);
         return v;
     }
     static NodeInfo decode(const std::string &s) {
         auto parts = split(s, '|');
         return NodeInfo(parts[0], std::stoi(parts[1]), hash_str(s));
     }
     NodeInfo find_successor(int nid) {
         return (succ_.str() == self_.str() ? self_ : succ_);
     }
     void notify(int nid, const NodeInfo &ni) {
         pred_ = ni;
     }
 };

 void Node::bootstrap(const std::string &contact_ip, int contact_port) {
    // 1) Ask the contact for your successor
    std::string reply = rpc_.send_message(
        contact_ip, contact_port,
        "join_request|" + std::to_string(self_id())
    );
    succ_ = decode(reply);

    // 2) Grab any keys you should now own
    std::string kvpairs = rpc_.send_message(
        succ_.ip, succ_.port,
        "send_keys|" + std::to_string(self_id())
    );
    for (auto &entry : split(kvpairs, ':')) {
        if (entry.empty()) continue;
        auto sep = entry.find('|');
        insert(entry.substr(0,sep), entry.substr(sep+1));
    }
}


 DWORD WINAPI stabilize_thread(LPVOID param) {
    Node *n = static_cast<Node*>(param);
    while (true) {
        Sleep(5000);
        // RPC stabilize logic stub
    }
    return 0;
}

DWORD WINAPI fix_fingers_thread(LPVOID param) {
    Node *n = static_cast<Node*>(param);
    srand((unsigned)time(nullptr));
    while (true) {
        int i = rand() % (m-1) + 1;
        // RPC fix_fingers logic stub
        Sleep(5000);
    }
    return 0;
}

DWORD WINAPI client_thread(LPVOID param) {
    auto args = static_cast<std::pair<Node*, SOCKET>*>(param);
    Node *n = args->first;
    SOCKET client = args->second;
    delete args;
    char buf[1024];
    int r = recv(client, buf, sizeof(buf), 0);
    if (r > 0) {
        std::string msg(buf, r);
        if (!msg.empty() && msg.back()=='\n') msg.pop_back();
        std::string resp = n->process_request(msg);
        send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
    }
    closesocket(client);
    return 0;
}
 
 std::string Node::process_request(const std::string &msg) {
     auto bar = msg.find('|');
     std::string op = msg.substr(0, bar);
     std::string body = (bar == std::string::npos) ? std::string() : msg.substr(bar + 1);
     
     if (op == "insert_server") {
         auto colon = body.find(':');
         insert(body.substr(0, colon), body.substr(colon+1));
         return "Inserted";
     } else if (op == "delete_server") {
         remove(body);
         return "Deleted";
     } else if (op == "search_server") {
         auto v = search(body);
         return v.empty()? "NOT FOUND": v;
     } else if (op == "send_keys") {
         int nid = std::stoi(body);
         return DataStore::send_keys(nid);
     } else if (op == "insert") {
         auto colon = body.find(':');
         int key_id = hash_str(body.substr(0,colon));
         NodeInfo node = find_successor(key_id);
         rpc_.send_message(node.ip, node.port, "insert_server|"+body);
         return "Done";
     } else if (op == "delete") {
         int key_id = hash_str(body);
         NodeInfo node = find_successor(key_id);
         rpc_.send_message(node.ip, node.port, "delete_server|"+body);
         return "Done";
     } else if (op == "search") {
         int key_id = hash_str(body);
         NodeInfo node = find_successor(key_id);
         return rpc_.send_message(node.ip, node.port, "search_server|"+body);
     } else if (op == "join_request") {
         int nid = std::stoi(body);
         NodeInfo node = find_successor(nid);
         return node.str();
     } else if (op == "get_successor") {
         return succ_.str();
     } else if (op == "get_predecessor") {
         return pred_.str();
     } else if (op == "notify") {
         auto parts = split(body, '|');
         int nid = std::stoi(parts[0]);
         NodeInfo ni = decode(parts[1]);
         notify(nid, ni);
         return {};
     }
     return {};
 }
 
 void Node::start() {
     // Start maintenance threads via CreateThread
     CreateThread(nullptr, 0, stabilize_thread, this, 0, nullptr);
     CreateThread(nullptr, 0, fix_fingers_thread, this, 0, nullptr);
 
     SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
     sockaddr_in addr{};
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = inet_addr(self_.ip.c_str());
     addr.sin_port = htons(self_.port);
     bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
     listen(listener, SOMAXCONN);
     std::cout << "Node on "<< self_.ip<<":"<< self_.port
               <<" id="<< self_.id <<"\n";
 
     // Accept loop
     while (true) {
         SOCKET client = accept(listener, nullptr, nullptr);
         auto args = new std::pair<Node*, SOCKET>(this, client);
         CreateThread(nullptr, 0, client_thread, args, 0, nullptr);
     }
     WSACleanup();
 }
 int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <port> [<contact_ip> <contact_port>]\n";
        return 1;
    }

    // 1) Initialize Winsock up front
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    Node node("127.0.0.1", port);

    // 2) Now it’s safe to bootstrap/join (uses rpc_ under the hood)
    if (argc == 4) {
        node.bootstrap(argv[2], std::stoi(argv[3]));
    }

    // 3) Finally start your server threads + accept loop
    node.start();

    // 4) Clean up Winsock when you ever get here
    WSACleanup();
    return 0;
}
