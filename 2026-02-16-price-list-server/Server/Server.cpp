#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 10
#define DEFAULT_BUFLEN 4096
#define PORT 8888

struct Product
{
    wstring name;   // как в файле, например "Картошка"
    wstring lower;  // в нижнем регистре, для поиска
    int price;
};

vector<Product> products;

// utf-8 -> wstring
wstring ToWide(const string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// wstring -> utf-8
string ToUtf8(const wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

wstring ToLower(wstring s)
{
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

// читаем прайс в формате "Картошка - 16грн"
bool LoadPrices(const string& fileName)
{
    ifstream file(fileName);
    if (!file) return false;

    string line;
    while (getline(file, line))
    {
        wstring w = ToWide(line);
        size_t dash = w.find(L'-');
        if (dash == wstring::npos) continue;

        wstring name = w.substr(0, dash);
        while (!name.empty() && name.back() == L' ') name.pop_back();
        if (!name.empty() && name[0] == 0xFEFF) name.erase(0, 1); // BOM

        int price = _wtoi(w.substr(dash + 1).c_str());
        if (name.empty() || price <= 0) continue;

        products.push_back({ name, ToLower(name), price });
    }
    return !products.empty();
}

// разбиваем строку на слова (буквы и цифры)
vector<wstring> SplitWords(const wstring& text)
{
    vector<wstring> words;
    wstring word;
    for (wchar_t c : text)
    {
        if (IsCharAlphaNumericW(c) || c == L'.' || c == L',')
        {
            word += c;
        }
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

bool IsNumber(const wstring& w)
{
    return !w.empty() && iswdigit(w[0]);
}

// "картошки" тоже должна найтись по "картошка", поэтому сравниваем без окончания
bool SameProduct(const wstring& word, const Product& p)
{
    wstring stem = p.lower.size() > 4 ? p.lower.substr(0, p.lower.size() - 1) : p.lower;
    return word.compare(0, stem.size(), stem) == 0;
}

wstring PriceList()
{
    wstringstream out;
    out << L"Прайс-лист:\n";
    for (auto& p : products)
        out << L"  " << p.name << L" - " << p.price << L" грн\n";
    return out.str();
}

// главное: парсим запрос клиента и считаем сумму
wstring ProcessRequest(const wstring& request)
{
    wstring text = ToLower(request);
    if (text.find(L"прайс") != wstring::npos || text.find(L"цены") != wstring::npos)
        return PriceList();

    vector<wstring> words = SplitWords(text);
    wstringstream out;
    double total = 0;
    bool found = false;

    for (size_t i = 0; i < words.size(); i++)
    {
        for (auto& p : products)
        {
            if (!SameProduct(words[i], p)) continue;

            // количество ищем в 2-3 словах перед товаром ("2 кг картошки")
            double amount = 1;
            for (int j = (int)i - 1; j >= 0 && j >= (int)i - 3; j--)
            {
                if (IsNumber(words[j]))
                {
                    wstring num = words[j];
                    for (auto& c : num) if (c == L',') c = L'.';
                    amount = _wtof(num.c_str());
                    break;
                }
            }

            double sum = amount * p.price;
            total += sum;
            found = true;
            out << p.name << L": " << amount << L" x " << p.price << L" = " << sum << L" грн\n";
        }
    }

    if (!found)
        return L"Не нашёл товаров в запросе. Напишите, например: Сколько будет стоить 2 кг картошки и 1 кг сахара?\nСписок товаров: напишите \"прайс\"\n";

    out << L"Итого: " << total << L" грн\n";
    return out.str();
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    system("title Server");

    if (!LoadPrices("prices.txt"))
    {
        cout << "Не удалось прочитать prices.txt" << endl;
        return 1;
    }
    cout << "Загружено товаров: " << products.size() << endl;

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
    cout << "Сервер запущен на порту " << PORT << ", ждём клиентов..." << endl;

    SOCKET clients[MAX_CLIENTS] = {};
    fd_set readfds;

    while (true)
    {
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        for (SOCKET s : clients)
            if (s) FD_SET(s, &readfds);

        if (select(0, &readfds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            cout << "select error: " << WSAGetLastError() << endl;
            return 4;
        }

        // новое подключение
        if (FD_ISSET(server_socket, &readfds))
        {
            SOCKET new_socket = accept(server_socket, NULL, NULL);
            bool added = false;
            for (auto& s : clients)
            {
                if (!s) { s = new_socket; added = true; break; }
            }
            if (added)
            {
                cout << "Новый клиент подключился" << endl;
                string hello = ToUtf8(L"Здравствуйте! Спросите, сколько будет стоить заказ.\n" + PriceList());
                send(new_socket, hello.c_str(), (int)hello.size(), 0);
            }
            else
            {
                closesocket(new_socket);
            }
        }

        // сообщения от клиентов
        for (auto& s : clients)
        {
            if (!s || !FD_ISSET(s, &readfds)) continue;

            char buf[DEFAULT_BUFLEN];
            int len = recv(s, buf, DEFAULT_BUFLEN - 1, 0);
            if (len <= 0)
            {
                cout << "Клиент отключился" << endl;
                closesocket(s);
                s = 0;
                continue;
            }
            buf[len] = '\0';
            cout << "Запрос: " << buf << endl;

            string answer = ToUtf8(ProcessRequest(ToWide(buf)));
            cout << "Ответ:\n" << answer << endl;
            send(s, answer.c_str(), (int)answer.size(), 0);
        }
    }

    closesocket(server_socket);
    WSACleanup();
}
