/*
 * owned.h: the C++ front end's memory, held by owners.
 *
 * Code in src/frontend/cplusplus, src/include/frontend/cplusplus and
 * src/bin/cxxlex never calls free(), malloc(), realloc() or strdup() itself
 * (the pre-commit hook no-raw-allocation enforces it): what it allocates
 * belongs to one of the owners below, whose destructor gives it back when
 * the owner goes out of scope (RAII).  This header is the one place those
 * functions are called.
 *
 *   owned_array<T>   a growable array of trivially copyable T
 *   owned_buffer     a vbuffer (common/buffer.h) freed at the end of scope
 *   owned_string     a copy of some characters, NUL-terminated
 *
 * Owners move and do not copy.  An allocation that fails aborts: the front
 * end has no way to continue without the memory, and exceptions are off.
 */
#ifndef VCC_FRONTEND_CPLUSPLUS_OWNED_H
#define VCC_FRONTEND_CPLUSPLUS_OWNED_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "common/buffer.h"

namespace cplusplus
{

template <typename T> class owned_array
{
  public:
    owned_array() = default;
    ~owned_array()
    {
        ::free(elements);
    }
    owned_array(const owned_array &) = delete;
    owned_array &operator=(const owned_array &) = delete;
    owned_array(owned_array &&other) noexcept
        : elements(other.elements), count(other.count), capacity(other.capacity)
    {
        other.elements = nullptr;
        other.count = other.capacity = 0;
    }
    owned_array &operator=(owned_array &&other) noexcept
    {
        if (this != &other) {
            ::free(elements);
            elements = other.elements;
            count = other.count;
            capacity = other.capacity;
            other.elements = nullptr;
            other.count = other.capacity = 0;
        }
        return *this;
    }

    /* room for at least n elements */
    void reserve(size_t n)
    {
        if (n <= capacity)
            return;
        size_t grown = capacity ? capacity * 2 : 16;
        if (grown < n)
            grown = n;
        T *p = static_cast<T *>(
            ::realloc(static_cast<void *>(elements), grown * sizeof(T)));
        if (!p)
            abort();
        elements = p;
        capacity = grown;
    }
    T &append(const T &value)
    {
        reserve(count + 1);
        elements[count] = value;
        return elements[count++];
    }
    /* n elements more, their bytes unset; returns the first */
    T *extend(size_t n)
    {
        reserve(count + n);
        T *first = elements + count;
        count += n;
        return first;
    }
    /* n elements: the first ones kept, any new ones unset */
    void resize(size_t n)
    {
        reserve(n);
        count = n;
    }
    void clear()
    {
        count = 0;
    }

    T &operator[](size_t i)
    {
        return elements[i];
    }
    const T &operator[](size_t i) const
    {
        return elements[i];
    }
    T *data()
    {
        return elements;
    }
    const T *data() const
    {
        return elements;
    }
    size_t size() const
    {
        return count;
    }
    bool empty() const
    {
        return count == 0;
    }
    T *begin()
    {
        return elements;
    }
    T *end()
    {
        return elements + count;
    }
    const T *begin() const
    {
        return elements;
    }
    const T *end() const
    {
        return elements + count;
    }

  private:
    T *elements = nullptr;
    size_t count = 0, capacity = 0;
};

/* a vbuffer whose storage is freed with the owner */
class owned_buffer
{
  public:
    owned_buffer() = default;
    ~owned_buffer()
    {
        ::free(buffer.p);
    }
    owned_buffer(const owned_buffer &) = delete;
    owned_buffer &operator=(const owned_buffer &) = delete;

    vbuffer *get()
    {
        return &buffer;
    }
    vbuffer *operator->()
    {
        return &buffer;
    }
    const char *data() const
    {
        return buffer.p ? buffer.p : "";
    }
    size_t size() const
    {
        return buffer.n;
    }
    void clear()
    {
        buffer.n = 0;
    }

  private:
    vbuffer buffer = {};
};

/* a NUL-terminated copy of n characters */
class owned_string
{
  public:
    owned_string() = default;
    owned_string(const char *s, size_t n)
    {
        characters = static_cast<char *>(::malloc(n + 1));
        if (!characters)
            abort();
        memcpy(characters, s, n);
        characters[n] = 0;
    }
    ~owned_string()
    {
        ::free(characters);
    }
    owned_string(const owned_string &) = delete;
    owned_string &operator=(const owned_string &) = delete;
    owned_string(owned_string &&other) noexcept : characters(other.characters)
    {
        other.characters = nullptr;
    }
    owned_string &operator=(owned_string &&other) noexcept
    {
        if (this != &other) {
            ::free(characters);
            characters = other.characters;
            other.characters = nullptr;
        }
        return *this;
    }
    const char *c_str() const
    {
        return characters ? characters : "";
    }

  private:
    char *characters = nullptr;
};

} // namespace cplusplus

#endif
