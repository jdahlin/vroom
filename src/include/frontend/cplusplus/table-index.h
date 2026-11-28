#ifndef VCC_CPLUSPLUS_TABLE_INDEX_H
#define VCC_CPLUSPLUS_TABLE_INDEX_H

#include <stddef.h>
#include <stdint.h>

namespace cplusplus
{

/*
 * The index of a record in one of the front end's tables, a type of its own
 * for each table (Tag): indexes of different tables, and plain integers, do
 * not convert to it or from it without saying so (T(i), t.index()).
 * The literal 0 is the one integer that converts: as in ast::node, it makes
 * index 0, which a condition reads as false.
 */
template <typename Tag> class table_index
{
  public:
    constexpr table_index() = default;
    constexpr table_index(decltype(nullptr))
    {
    }
    explicit constexpr table_index(uint32_t index) : value(index)
    {
    }
    constexpr uint32_t index() const
    {
        return value;
    }
    explicit constexpr operator bool() const
    {
        return value != 0;
    }
    friend constexpr bool operator==(table_index, table_index) = default;
    /* records are made in order: indexes of one table compare, step to the
       next record and take an offset within a run of records */
    friend constexpr bool operator<(table_index a, table_index b)
    {
        return a.value < b.value;
    }
    friend constexpr bool operator<=(table_index a, table_index b)
    {
        return a.value <= b.value;
    }
    friend constexpr bool operator>(table_index a, table_index b)
    {
        return a.value > b.value;
    }
    friend constexpr bool operator>=(table_index a, table_index b)
    {
        return a.value >= b.value;
    }
    constexpr table_index &operator++()
    {
        value++;
        return *this;
    }
    constexpr table_index operator++(int)
    {
        table_index old = *this;
        value++;
        return old;
    }
    friend constexpr table_index operator-(table_index i, uint32_t offset)
    {
        return table_index(i.value - offset);
    }
    friend constexpr uint32_t operator-(table_index a, table_index b)
    {
        return a.value - b.value;
    }
    constexpr table_index &operator--()
    {
        value--;
        return *this;
    }
    constexpr table_index operator--(int)
    {
        table_index old = *this;
        value--;
        return old;
    }
    constexpr table_index &operator-=(uint32_t offset)
    {
        value -= offset;
        return *this;
    }
    constexpr table_index &operator+=(uint32_t offset)
    {
        value += offset;
        return *this;
    }
    friend constexpr table_index operator+(table_index i, uint32_t offset)
    {
        return table_index(i.value + offset);
    }

  private:
    uint32_t value = 0;
};

template <typename Tag> constexpr size_t table_slot(table_index<Tag> i)
{
    return i.index();
}

/*
 * A record of one of the front end's append-only tables, or none: the
 * record's index plus one, so that 0 is none, as a type of its own for each
 * table.  record_table (owned.h) takes it as a subscript.
 */
template <typename Tag> class record_ref
{
  public:
    constexpr record_ref() = default;
    constexpr record_ref(decltype(nullptr))
    {
    }
    /* the record at index (0-based) */
    static constexpr record_ref at(size_t index)
    {
        return from_number(static_cast<uint32_t>(index + 1));
    }
    /* from number(), as a word that holds it gives it back */
    static constexpr record_ref from_number(uint32_t number)
    {
        record_ref r;
        r.value = number;
        return r;
    }
    constexpr uint32_t number() const
    {
        return value;
    }
    /* the record's index in its table; not of none */
    constexpr uint32_t index() const
    {
        return value - 1;
    }
    explicit constexpr operator bool() const
    {
        return value != 0;
    }
    friend constexpr bool operator==(record_ref, record_ref) = default;

  private:
    uint32_t value = 0;
};

using template_ref = record_ref<struct function_template_tag>;
using instantiation_ref = record_ref<struct instantiation_tag>;

} // namespace cplusplus

#endif
