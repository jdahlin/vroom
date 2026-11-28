/*
 * stable-array.h: an append-only array whose elements never move.
 *
 * Analysis makes types and entities while it holds references to others, so
 * a table that grew with realloc left those references dangling.  A
 * stable_array reserves its address space once (STABLE_ARRAY_BYTES) and
 * appends into it: a reference or pointer to an element, and a pointer into
 * a run of them, is good for as long as the array is.  Indexing costs what
 * a plain array's does.
 *
 * The reservations are kept from one translation unit to the next
 * (port/job-regions.h); under ASan the bytes past the last element are
 * poisoned (common/region.h).
 */
#ifndef VCC_FRONTEND_CPLUSPLUS_STABLE_ARRAY_H
#define VCC_FRONTEND_CPLUSPLUS_STABLE_ARRAY_H

#include <stddef.h>

#include "common/region.h"
#include "frontend/cplusplus/owned.h"
#include "port/job-regions.h"

namespace cplusplus
{

/* address space per array, not memory: pages are taken as they are written */
inline constexpr size_t STABLE_ARRAY_BYTES = size_t{1} << 30;

inline vjob_region_pool stable_array_regions = VJOB_REGION_POOL_INIT(1);

/* Index: the type operator[] takes */
template <typename T, typename Index = size_t> class stable_array
{
    /* the elements are never destroyed */
    static_assert(__is_trivially_copyable(T) && __is_trivially_destructible(T),
                  "stable_array<T>: T is not trivially copyable");

  public:
    stable_array() : set(vjob_region_pool_take(&stable_array_regions))
    {
        void *base =
            vjob_region_acquire(set, STABLE_ARRAY_BYTES, "stable array");
        vregion_init(&region, base, STABLE_ARRAY_BYTES);
        elements = static_cast<T *>(base);
    }
    ~stable_array()
    {
        vregion_unpoison(&region);
        /* the elements are written before they are read, so a region goes
           back as it is unless it holds more than the pool should keep */
        size_t dirty = region.used;
        if (dirty <= VJOB_REGION_KEEP)
            dirty = VJOB_REGION_SCRATCH;
        vjob_region_release(set, dirty);
        vjob_region_pool_give(&stable_array_regions, set);
    }
    stable_array(const stable_array &) = delete;
    stable_array &operator=(const stable_array &) = delete;

    T &append(const T &value)
    {
        void *at = vregion_push(&region, sizeof(T), alignof(T),
                                "too many records in one translation unit");
        count++;
        return *new (owned_placement{}, at) T(value);
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
    const T *data() const
    {
        return elements;
    }
    size_t size() const
    {
        return count;
    }

  private:
    vjob_region *set;
    vregion region{};
    T *elements = nullptr;
    size_t count = 0;
};

} // namespace cplusplus

#endif
