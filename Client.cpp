// client.cpp
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")  // Link Ws2_32.lib

using namespace std;

int main() {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;

    string ip = "127.0.0.1";
    int port;

    cout << "Give the port number of a node: ";
    cin >> port;
    cin.ignore(); // clear newline from input buffer

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "WSAStartup failed. Error Code: " << WSAGetLastError() << endl;
        return 1;
    }

    while (true) {
        cout << "************************MENU*************************\n";
        cout << "PRESS ***********************************************\n";
        cout << "1. TO ENTER *****************************************\n";
        cout << "2. TO SHOW ******************************************\n";
        cout << "3. TO DELETE ****************************************\n";
        cout << "4. TO EXIT ******************************************\n";
        cout << "*****************************************************\n";

        string choice;
        getline(cin, choice);

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            cout << "Could not create socket: " << WSAGetLastError() << endl;
            continue;
        }

        server.sin_addr.s_addr = inet_addr(ip.c_str());
        server.sin_family = AF_INET;
        server.sin_port = htons(port);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
            cout << "Connection failed. Error Code: " << WSAGetLastError() << endl;
            closesocket(sock);
            continue;
        }

        if (choice == "1") {
            string key, val;
            cout << "ENTER THE KEY: ";
            getline(cin, key);
            cout << "ENTER THE VALUE: ";
            getline(cin, val);
            string message = "insert|" + key + ":" + val;
            send(sock, message.c_str(), message.length(), 0);

            char buffer[1024] = {0};
            int recv_size = recv(sock, buffer, sizeof(buffer), 0);
            if (recv_size > 0) {
                cout << string(buffer, recv_size) << endl;
            }
        }
        else if (choice == "2") {
            string key;
            cout << "ENTER THE KEY: ";
            getline(cin, key);
            string message = "search|" + key;
            send(sock, message.c_str(), message.length(), 0);

            char buffer[1024] = {0};
            int recv_size = recv(sock, buffer, sizeof(buffer), 0);
            if (recv_size > 0) {
                cout << "The value corresponding to the key is: " << string(buffer, recv_size) << endl;
            }
        }
        else if (choice == "3") {
            string key;
            cout << "ENTER THE KEY: ";
            getline(cin, key);
            string message = "delete|" + key;
            send(sock, message.c_str(), message.length(), 0);

            char buffer[1024] = {0};
            int recv_size = recv(sock, buffer, sizeof(buffer), 0);
            if (recv_size > 0) {
                cout << string(buffer, recv_size) << endl;
            }
        }
        else if (choice == "4") {
            cout << "Closing the socket" << endl;
            closesocket(sock);
            cout << "Exiting Client" << endl;
            WSACleanup();
            exit(0);
        }
        else {
            cout << "INCORRECT CHOICE" << endl;
        }

        closesocket(sock); // close after each request (similar to your Python code)
    }

    WSACleanup();
    return 0;
}
