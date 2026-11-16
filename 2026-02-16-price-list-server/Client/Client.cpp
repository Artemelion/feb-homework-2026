#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFLEN 4096
#define SERVER_IP "127.0.0.1"
#define DEFAULT_PORT "8888"

SOCKET client_socket;

// читаем строку из консоли в юникоде, чтобы русские буквы не ломались
string ReadLineUtf8()
{
    wchar_t wbuf[1024];
    DWORD read = 0;
    ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), wbuf, 1023, &read, NULL);
    while (read > 0 && (wbuf[read - 1] == L'\n' || wbuf[read - 1] == L'\r')) read--;

    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, read, nullptr, 0, nullptr, nullptr);
    string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, read, &s[0], n, nullptr, nullptr);
    return s;
}

void Sender()
{
    while (true)
    {
        string query = ReadLineUtf8();
        if (query.empty()) continue;
        send(client_socket, query.c_str(), (int)query.size(), 0);
    }
}

void Receiver()
{
    while (true)
    {
        char response[DEFAULT_BUFLEN];
        int result = recv(client_socket, response, DEFAULT_BUFLEN - 1, 0);
        if (result <= 0)
        {
            cout << "Сервер отключился" << endl;
            exit(0);
        }
        response[result] = '\0';
        cout << response << endl;
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    system("title Client");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cout << "WSAStartup error" << endl;
        return 1;
    }

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(SERVER_IP, DEFAULT_PORT, &hints, &result) != 0)
    {
        cout << "getaddrinfo error" << endl;
        WSACleanup();
        return 2;
    }

    client_socket = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        client_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (connect(client_socket, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR)
        {
            closesocket(client_socket);
            client_socket = INVALID_SOCKET;
            continue;
        }
        break;
    }
    freeaddrinfo(result);

    if (client_socket == INVALID_SOCKET)
    {
        cout << "Не удалось подключиться к серверу" << endl;
        WSACleanup();
        return 3;
    }

    thread t1(Sender);
    thread t2(Receiver);
    t1.join();
    t2.join();
}
