#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 10
#define DEFAULT_BUFLEN 4096
#define PORT 8888

struct Client
{
    SOCKET socket = 0;
    string nick;    // пустой, пока клиент не прислал ник
    string buffer;  // сюда копим данные, пока не придёт целая строка
};

vector<Client> clients(MAX_CLIENTS);

void SendLine(SOCKET s, const string& text)
{
    string line = text + "\n";
    send(s, line.c_str(), (int)line.size(), 0);
}

// отправить всем, кроме отправителя
void Broadcast(const string& text, SOCKET except)
{
    for (auto& c : clients)
    {
        if (c.socket && !c.nick.empty() && c.socket != except)
            SendLine(c.socket, text);
    }
}

bool NickTaken(const string& nick)
{
    for (auto& c : clients)
    {
        if (c.socket && _stricmp(c.nick.c_str(), nick.c_str()) == 0)
            return true;
    }
    return false;
}

bool NickValid(const string& nick)
{
    return !nick.empty() && nick.size() <= 20 && nick.find(' ') == string::npos && nick[0] != '/';
}

void Disconnect(Client& c)
{
    if (!c.nick.empty())
    {
        cout << "[отключение] " << c.nick << endl;
        Broadcast("* " + c.nick + " вышел из чата", c.socket);
    }
    else
    {
        cout << "[отключение] клиент без ника" << endl;
    }
    closesocket(c.socket);
    c = Client();
}

// обработка одной строки от клиента
void HandleLine(Client& c, const string& line)
{
    // первая строка - это ник
    if (c.nick.empty())
    {
        if (!NickValid(line))
        {
            SendLine(c.socket, "ERROR: некорректный ник");
            Disconnect(c);
            return;
        }
        if (NickTaken(line))
        {
            cout << "[отказ] ник " << line << " уже занят" << endl;
            SendLine(c.socket, "ERROR: ник " + line + " уже занят");
            Disconnect(c);
            return;
        }
        c.nick = line;
        cout << "[подключение] " << c.nick << endl;
        SendLine(c.socket, "Добро пожаловать в чат, " + c.nick + "! Команды: /users, /nick <ник>, /quit");
        Broadcast("* " + c.nick + " вошёл в чат", c.socket);
        return;
    }

    if (line == "/users")
    {
        string list = "Online users:";
        for (auto& other : clients)
            if (other.socket && !other.nick.empty()) list += "\n- " + other.nick;
        SendLine(c.socket, list);
    }
    else if (line.rfind("/nick", 0) == 0)
    {
        string newNick = line.size() > 6 ? line.substr(6) : "";
        if (!NickValid(newNick))
        {
            SendLine(c.socket, "ERROR: использование /nick <новый_ник>");
        }
        else if (NickTaken(newNick))
        {
            SendLine(c.socket, "ERROR: ник " + newNick + " уже занят");
        }
        else
        {
            cout << "[смена ника] " << c.nick << " -> " << newNick << endl;
            Broadcast("* " + c.nick + " теперь " + newNick, c.socket);
            c.nick = newNick;
            SendLine(c.socket, "Теперь ваш ник: " + newNick);
        }
    }
    else if (line == "/quit")
    {
        SendLine(c.socket, "Пока!");
        Disconnect(c);
    }
    else if (line[0] == '/')
    {
        SendLine(c.socket, "ERROR: неизвестная команда");
    }
    else
    {
        cout << "[сообщение] " << c.nick << ": " << line << endl;
        Broadcast("[" + c.nick + "]: " + line, c.socket);
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    system("title Chat Server");

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        cout << "WSAStartup error: " << WSAGetLastError() << endl;
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET)
    {
        cout << "socket error: " << WSAGetLastError() << endl;
        return 2;
    }

    sockaddr_in server = {};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_socket, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
    {
        cout << "bind error: " << WSAGetLastError() << endl;
        return 3;
    }

    listen(server_socket, MAX_CLIENTS);
    cout << "Чат-сервер запущен на порту " << PORT << endl;

    fd_set readfds;
    while (true)
    {
        // один поток: select ждёт событие на любом сокете
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        for (auto& c : clients)
            if (c.socket) FD_SET(c.socket, &readfds);

        if (select(0, &readfds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            cout << "select error: " << WSAGetLastError() << endl;
            return 4;
        }

        if (FD_ISSET(server_socket, &readfds))
        {
            SOCKET new_socket = accept(server_socket, NULL, NULL);
            bool added = false;
            for (auto& c : clients)
            {
                if (!c.socket)
                {
                    c.socket = new_socket;
                    added = true;
                    break;
                }
            }
            if (!added)
            {
                SendLine(new_socket, "ERROR: сервер переполнен");
                closesocket(new_socket);
            }
        }

        for (auto& c : clients)
        {
            if (!c.socket || !FD_ISSET(c.socket, &readfds)) continue;

            char buf[DEFAULT_BUFLEN];
            int len = recv(c.socket, buf, DEFAULT_BUFLEN, 0);
            if (len <= 0)
            {
                Disconnect(c);
                continue;
            }
            c.buffer.append(buf, len);

            // разбираем все полные строки
            size_t pos;
            while (c.socket && (pos = c.buffer.find('\n')) != string::npos)
            {
                string line = c.buffer.substr(0, pos);
                c.buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) HandleLine(c, line);
            }
        }
    }

    closesocket(server_socket);
    WSACleanup();
}
