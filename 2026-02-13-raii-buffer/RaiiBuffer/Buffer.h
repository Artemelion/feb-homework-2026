#pragma once
#include <cstddef>

// обёртка над динамическим массивом, сама освобождает память
template <typename T>
class Buffer
{
    T* ptr = nullptr;
    size_t len = 0;

public:
    Buffer() = default;

    explicit Buffer(size_t size) : ptr(new T[size]()), len(size) {}

    ~Buffer()
    {
        delete[] ptr;
    }

    // копировать нельзя
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // перемещать можно
    Buffer(Buffer&& other) noexcept : ptr(other.ptr), len(other.len)
    {
        other.ptr = nullptr;
        other.len = 0;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            delete[] ptr;
            ptr = other.ptr;
            len = other.len;
            other.ptr = nullptr;
            other.len = 0;
        }
        return *this;
    }

    T* data() const { return ptr; }
    size_t size() const { return len; }

    // отдаёт указатель и больше за него не отвечает (удалять надо самому)
    T* release()
    {
        T* p = ptr;
        ptr = nullptr;
        len = 0;
        return p;
    }
};
