/*
 * owned.h: the C++ front end's memory, held by owners.
 *
 * Code in src/frontend/cplusplus and src/include/frontend/cplusplus never
 * calls free(), malloc(), realloc() or strdup() itself
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

#include "common/asan.h"
#include "common/buffer.h"
#include "common/fail.h"

namespace cplusplus
{

/* the position of an index in its table: a plain size_t here, and an
   overload for each index type (table-index.h) */
inline size_t table_slot(size_t i)
{
    return i;
}

/* an element asked for that is not there: only the ASan build asks */
inline void check_element(bool there)
{
#if VCC_ASAN
    if (!there)
        VBUG("index out of an array's bounds");
#else
    (void)there;
#endif
}

/* Index: the type operator[] takes.  In an ASan build every subscript is
   checked, and the room past the last element is poisoned, so a pointer
   from data() that runs past the end is reported too. */
template <typename T, typename Index = size_t> class owned_array
{
    /* the elements are moved with realloc and never destroyed */
    static_assert(__is_trivially_copyable(T) && __is_trivially_destructible(T),
                  "owned_array<T>: T is not trivially copyable");

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
        size_t grown = 16;
        if (capacity)
            grown = capacity * 2;
        if (grown < n)
            grown = n;
        /* (realloc copies the whole block) */
        unpoison_room();
        T *p = static_cast<T *>(
            ::realloc(static_cast<void *>(elements), grown * sizeof(T)));
        if (!p)
            abort();
        elements = p;
        capacity = grown;
        poison_room();
    }
    /* value may be one of this array's own elements, which reserve moves:
       copied before (std::vector's push_back allows the same) */
    T &append(const T &value)
    {
        T copy = value;
        reserve(count + 1);
        unpoison(count, count + 1);
        elements[count] = copy;
        return elements[count++];
    }
    /* n elements more, their bytes unset; returns the first */
    T *extend(size_t n)
    {
        reserve(count + n);
        unpoison(count, count + n);
        T *first = elements + count;
        count += n;
        return first;
    }
    /* n elements: the first ones kept, any new ones unset */
    void resize(size_t n)
    {
        reserve(n);
        unpoison(count, n);
        count = n;
        poison_room();
    }
    void clear()
    {
        count = 0;
        poison_room();
    }

    T &operator[](Index i)
    {
        check_element(table_slot(i) < count);
        return elements[table_slot(i)];
    }
    const T &operator[](Index i) const
    {
        check_element(table_slot(i) < count);
        return elements[table_slot(i)];
    }
    T &back()
    {
        check_element(count);
        return elements[count - 1];
    }
    const T &back() const
    {
        check_element(count);
        return elements[count - 1];
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
    /* elements [from, to) may be used */
    void unpoison(size_t from, size_t to)
    {
        if (to > from)
            VASAN_UNPOISON(elements + from, (to - from) * sizeof(T));
    }
    void unpoison_room()
    {
        unpoison(count, capacity);
    }
    void poison_room()
    {
        if (capacity > count)
            VASAN_POISON(elements + count, (capacity - count) * sizeof(T));
    }

    T *elements = nullptr;
    size_t count = 0, capacity = 0;
};

/*
 * A vbuffer whose storage is freed with the owner.  The first bytes are in
 * the owner itself (VBUFFER_INLINE): most buffers hold one spelling or one
 * path, and a function that makes one for every token must not allocate for
 * it (doc/RESEARCH-cxx-performance.md, finding 2).  An owner therefore does
 * not move.
 */
class owned_buffer
{
  public:
    owned_buffer()
    {
        vbuffer_inline(&buffer, first_bytes, sizeof first_bytes);
    }
    ~owned_buffer()
    {
        if (!(buffer.flags & VBUFFER_INLINE))
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
        return buffer.p;
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
    vbuffer buffer;
    char first_bytes[128];
};

/* objects of any type T, each made in place at an address that does not
   change, all destroyed with the owner (the source files of a translation
   unit and their lexers, which refer to each other) */
/* placement in storage owned_objects allocated (the C++ runtime's operator
   new is not linked) */
struct owned_placement {};

} // namespace cplusplus

inline void *operator new(size_t, cplusplus::owned_placement, void *p) noexcept
{
    return p;
}

namespace cplusplus
{

template <typename T, typename Index = size_t> class owned_objects
{
  public:
    owned_objects() = default;
    ~owned_objects()
    {
        for (size_t i = pointers.size(); i-- > 0;) {
            pointers[i]->~T();
            ::free(pointers[i]);
        }
    }
    owned_objects(const owned_objects &) = delete;
    owned_objects &operator=(const owned_objects &) = delete;

    template <typename... A> T &make(A &&...arguments)
    {
        void *storage = ::malloc(sizeof(T));
        if (!storage)
            abort();
        T *object =
            new (owned_placement{}, storage) T(static_cast<A &&>(arguments)...);
        pointers.append(object);
        return *object;
    }
    T &operator[](Index i)
    {
        return *pointers[table_slot(i)];
    }
    const T &operator[](Index i) const
    {
        return *pointers[table_slot(i)];
    }
    size_t size() const
    {
        return pointers.size();
    }

  private:
    owned_array<T *> pointers;
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
        if (!characters)
            return "";
        return characters;
    }

  private:
    char *characters = nullptr;
};

/*
 * The records of an append-only array by a key of theirs: a chain per key,
 * in the array's order.  Keys are dense numbers (entities, identifiers), so
 * they index the chains' ends directly.  update() takes in the records
 * appended since the last call; a record's key must not change.
 */
class record_chains
{
  public:
    template <typename T, typename Key>
    void update(const owned_array<T> &records, Key key)
    {
        for (size_t i = next.size(); i < records.size(); i++) {
            size_t k = table_slot(key(records[i]));
            while (ends.size() <= k)
                ends.append(chain{0, 0});
            uint32_t at = static_cast<uint32_t>(i + 1);
            if (ends[k].last)
                next[ends[k].last - 1] = at;
            else
                ends[k].first = at;
            ends[k].last = at;
            next.append(0);
        }
    }
    /* a record's index plus one, 0 at the chain's end */
    template <typename Key> uint32_t first(Key key) const
    {
        size_t k = table_slot(key);
        if (k < ends.size())
            return ends[k].first;
        return 0;
    }
    uint32_t after(uint32_t record) const
    {
        return next[record - 1];
    }

  private:
    struct chain {
        uint32_t first, last;
    };
    owned_array<chain> ends;
    owned_array<uint32_t> next;
};

} // namespace cplusplus

#endif
