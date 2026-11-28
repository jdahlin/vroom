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

} // namespace cplusplus

#endif
