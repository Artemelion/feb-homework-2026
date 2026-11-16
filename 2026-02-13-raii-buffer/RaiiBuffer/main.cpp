#include <windows.h>
#include <iostream>
#include "Buffer.h"
using namespace std;

#define CHECK(x) if (!(x)) { cout << "Ошибка: " #x << endl; return 1; }

// char* -> wchar_t*
Buffer<wchar_t> ToWide(const char* str, UINT codePage)
{
    int count = MultiByteToWideChar(codePage, 0, str, -1, nullptr, 0);
    Buffer<wchar_t> result(count);
    MultiByteToWideChar(codePage, 0, str, -1, result.data(), count);
    return result;
}

// wchar_t* -> char*
Buffer<char> ToNarrow(const wchar_t* str, UINT codePage)
{
    int count = WideCharToMultiByte(codePage, 0, str, -1, nullptr, 0, nullptr, nullptr);
    Buffer<char> result(count);
    WideCharToMultiByte(codePage, 0, str, -1, result.data(), count, nullptr, nullptr);
    return result;
}

void PrintWide(const wchar_t* text)
{
    DWORD written;
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)wcslen(text), &written, nullptr);
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"\n", 1, &written, nullptr);
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    const char* utf8Text = u8"Привет, мир! Hello!";

    // UTF-8 -> wchar_t
    Buffer<wchar_t> wide = ToWide(utf8Text, CP_UTF8);
    CHECK(wide.data() != nullptr);
    cout << "wchar_t строка (" << wide.size() - 1 << " символов): ";
    PrintWide(wide.data());

    // wchar_t -> windows-1251
    Buffer<char> cp1251 = ToNarrow(wide.data(), 1251);
    cout << "В кодировке 1251 занимает " << cp1251.size() - 1 << " байт" << endl;

    // и обратно в UTF-8
    Buffer<char> backToUtf8 = ToNarrow(ToWide(cp1251.data(), 1251).data(), CP_UTF8);
    cout << "Обратно в UTF-8: " << backToUtf8.data() << endl;

    // перемещение: старый буфер становится пустым
    Buffer<char> moved = move(backToUtf8);
    cout << "После move: старый size = " << backToUtf8.size() << ", новый size = " << moved.size() << endl;

    // release: память забираем себе, тогда удалять надо вручную
    char* raw = moved.release();
    cout << "После release: size = " << moved.size() << endl;
    delete[] raw;

    return 0;
}
