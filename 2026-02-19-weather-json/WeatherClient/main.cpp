#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

// GET запрос по http через сокет, возвращает только тело ответа
string HttpGet(const string& host, const string& path)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), "80", &hints, &result) != 0)
    {
        cout << "Не удалось найти сервер " << host << endl;
        return "";
    }

    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connect(s, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
    {
        cout << "Не удалось подключиться к " << host << endl;
        freeaddrinfo(result);
        closesocket(s);
        return "";
    }
    freeaddrinfo(result);

    // HTTP/1.0 - сервер отдаст ответ целиком и сам закроет соединение
    string request =
        "GET " + path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "User-Agent: WeatherClient\r\n"
        "Connection: close\r\n\r\n";
    send(s, request.c_str(), (int)request.size(), 0);

    string response;
    char buf[4096];
    int len;
    while ((len = recv(s, buf, sizeof(buf), 0)) > 0)
        response.append(buf, len);
    closesocket(s);

    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == string::npos) return "";
    return response.substr(headerEnd + 4);
}

// кодируем текст для адресной строки (кириллица -> %D0%9E...)
string UrlEncode(const string& text)
{
    string result;
    char hex[4];
    for (unsigned char c : text)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.') result += c;
        else
        {
            sprintf_s(hex, "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

// очень простой поиск значения в json: "key":value
string JsonValue(const string& json, const string& key, size_t from = 0)
{
    size_t pos = json.find("\"" + key + "\":", from);
    if (pos == string::npos) return "";
    pos += key.size() + 3;

    if (json[pos] == '[') pos++;          // массив - берём первый элемент
    if (json[pos] == '"')                 // строка
    {
        size_t end = json.find('"', pos + 1);
        return json.substr(pos + 1, end - pos - 1);
    }
    size_t end = json.find_first_of(",}]", pos);  // число
    return json.substr(pos, end - pos);
}

string ReadLineUtf8()
{
    wchar_t wbuf[256];
    DWORD read = 0;
    ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), wbuf, 255, &read, NULL);
    while (read > 0 && (wbuf[read - 1] == L'\n' || wbuf[read - 1] == L'\r')) read--;

    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, read, nullptr, 0, nullptr, nullptr);
    string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, read, &s[0], n, nullptr, nullptr);
    return s;
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        cout << "WSAStartup error" << endl;
        return 1;
    }

    cout << "Введите город: ";
    string city = ReadLineUtf8();
    if (city.empty()) city = "Одесса";

    // 1. ищем координаты города
    string geo = HttpGet("geocoding-api.open-meteo.com",
        "/v1/search?count=1&language=ru&name=" + UrlEncode(city));
    if (geo.find("\"results\"") == string::npos)
    {
        cout << "Город не найден" << endl;
        WSACleanup();
        return 2;
    }

    string name = JsonValue(geo, "name");
    string country = JsonValue(geo, "country");
    string lat = JsonValue(geo, "latitude");
    string lon = JsonValue(geo, "longitude");

    // 2. погода по координатам
    string weather = HttpGet("api.open-meteo.com",
        "/v1/forecast?latitude=" + lat + "&longitude=" + lon +
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m"
        "&daily=sunrise,sunset&timezone=auto&forecast_days=1");

    size_t current = weather.find("\"current\":{");
    size_t daily = weather.find("\"daily\":{");
    if (current == string::npos || daily == string::npos)
    {
        cout << "Не удалось получить погоду" << endl;
        WSACleanup();
        return 3;
    }

    string time = JsonValue(weather, "time", current);
    string temp = JsonValue(weather, "temperature_2m", current);
    string feels = JsonValue(weather, "apparent_temperature", current);
    string humidity = JsonValue(weather, "relative_humidity_2m", current);
    string wind = JsonValue(weather, "wind_speed_10m", current);
    string sunrise = JsonValue(weather, "sunrise", daily);
    string sunset = JsonValue(weather, "sunset", daily);

    cout << "\n" << name << ", " << country << "\n";
    cout << "Температура: " << temp << " °C (ощущается как " << feels << ")\n";
    cout << "Влажность: " << humidity << " %\n";
    cout << "Ветер: " << wind << " км/ч\n";
    cout << "Рассвет: " << sunrise.substr(11) << ", закат: " << sunset.substr(11) << "\n";

    // 3. записываем результат в json файл
    ofstream file("weather.json");
    file << "{\n"
        << "  \"city\": \"" << name << "\",\n"
        << "  \"country\": \"" << country << "\",\n"
        << "  \"time\": \"" << time << "\",\n"
        << "  \"temperature\": " << temp << ",\n"
        << "  \"feels_like\": " << feels << ",\n"
        << "  \"humidity\": " << humidity << ",\n"
        << "  \"wind_speed\": " << wind << ",\n"
        << "  \"sunrise\": \"" << sunrise.substr(11) << "\",\n"
        << "  \"sunset\": \"" << sunset.substr(11) << "\"\n"
        << "}\n";
    file.close();

    cout << "\nРезультат сохранён в weather.json" << endl;

    WSACleanup();
    return 0;
}
