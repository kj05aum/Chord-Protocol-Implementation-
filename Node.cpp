// Node.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <string>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// Forward declaration
class Node;

// Arguments for per-client thread
struct ClientArgs {
    Node* self;
    SOCKET sock;
};

// Thread entry point
static DWORD WINAPI ClientThread(LPVOID param) {
    ClientArgs* args = static_cast<ClientArgs*>(param);
    args->self->serve_request(args->sock);
    delete args;
    return 0;
}

// Simple RAII wrapper for a Windows CRITICAL_SECTION
class Mutex {
    CRITICAL_SECTION cs;
public:
    Mutex()  { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }
    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};

// Simple lock‐guard for our Mutex
class LockGuard {
    Mutex &m;
public:
    LockGuard(Mutex &m_) : m(m_) { m.lock(); }
    ~LockGuard()             { m.unlock(); }
};

// In-memory key/value store, protected by our Mutex
class DataStore {
    unordered_map<string,string> data;
    Mutex mtx;
public:
    void insert(const string &k, const string &v) {
        LockGuard lock(mtx);
        data[k] = v;
    }
    void remove(const string &k) {
        LockGuard lock(mtx);
        data.erase(k);
    }
    string search(const string &k) {
        LockGuard lock(mtx);
        auto it = data.find(k);
        return (it != data.end() ? it->second : string());
    }
};

// Helper to forward lookups if local miss
class RequestHandler {
public:
    string send_message(const string &ip, int port, const string &msg) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return string();

        sockaddr_in srv{};
        srv.sin_family = AF_INET;
        srv.sin_addr.s_addr = inet_addr(ip.c_str());
        srv.sin_port = htons(port);

        if (connect(s, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) != 0) {
            closesocket(s);
            return string();
        }
        send(s, msg.c_str(), static_cast<int>(msg.size()), 0);

        char buf[1024];
        int r = recv(s, buf, sizeof(buf), 0);
        string resp;
        if (r > 0) resp.assign(buf, r);

        closesocket(s);
        return resp;
    }
};

class Node {
    string ip;
    int port;
    DataStore ds;
    RequestHandler rh;

public:
    Node(const string &ip_, int port_) : ip(ip_), port(port_) {}

    void start() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            cerr << "WSAStartup failed" << endl;
            return;
        }

        SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener == INVALID_SOCKET) {
            cerr << "socket() failed: " << WSAGetLastError() << endl;
            WSACleanup();
            return;
        }

        int opt = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        addr.sin_port = htons(port);

        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            cerr << "bind() failed: " << WSAGetLastError() << endl;
            closesocket(listener);
            WSACleanup();
            return;
        }
        if (listen(listener, SOMAXCONN) != 0) {
            cerr << "listen() failed: " << WSAGetLastError() << endl;
            closesocket(listener);
            WSACleanup();
            return;
        }

        cout << "Listening on " << ip << ":" << port << endl;

        while (true) {
            sockaddr_in clientAddr;
            int len = sizeof(clientAddr);
            SOCKET clientSock = accept(listener, reinterpret_cast<sockaddr*>(&clientAddr), &len);
            if (clientSock == INVALID_SOCKET) {
                cerr << "accept() failed: " << WSAGetLastError() << endl;
                continue;
            }
            // allocate args and start thread
            ClientArgs *args = new ClientArgs{ this, clientSock };
            CreateThread(
                nullptr, 0,
                ClientThread,
                args,
                0, nullptr
            );
        }

        closesocket(listener);
        WSACleanup();
    }

    // handle one client
    void serve_request(SOCKET sock) {
        char buf[1024];
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r <= 0) {
            closesocket(sock);
            return;
        }
        string msg(buf, r);
        if (!msg.empty() && msg.back()=='\n') msg.pop_back();

        cout << "[Req] " << msg << endl;
        string resp = process_request(msg);
        cout << "[Res] " << resp << endl;

        send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
        closesocket(sock);
    }

private:
    string process_request(const string &msg) {
        auto bar = msg.find('|');
        if (bar == string::npos) return "ERROR";

        string op   = msg.substr(0, bar);
        string body = msg.substr(bar+1);

        if (op == "insert") {
            auto col = body.find(':');
            if (col == string::npos) return "ERROR";
            ds.insert(body.substr(0, col), body.substr(col+1));
            return "Inserted";
        } else if (op == "delete") {
            ds.remove(body);
            return "Deleted";
        } else if (op == "search") {
            string v = ds.search(body);
            if (!v.empty()) return v;
            // forward on miss
            cout << "Local miss → forward to 127.0.0.1:5555" << endl;
            string fwd = "search|" + body;
            string got = rh.send_message("127.0.0.1", 5555, fwd);
            return got.empty() ? "Not found" : got;
        }
        return "UNKNOWN";
    }
};

int main() {
    int port;
    cout << "Enter port: ";
    cin >> port;
    Node node("127.0.0.1", port);
    node.start();
    return 0;
}
