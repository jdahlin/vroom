# 22.4.4.4 Modifiers [map.modifiers]

[Contents](../README.md) · up: [22.4.4 Class template `map` \[map\]](map.md) · prev: [22.4.4.3 Element access \[map.access\]](map.access.md) · next: [22.4.4.5 Erasure \[map.erasure\]](map.erasure.md)

```cpp
template<class P>
  pair<iterator, bool> insert(P&& x);
template<class P>
  iterator insert(const_iterator position, P&& x);
```

**1** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**2** *Effects:* The first form is equivalent to `return emplace(std::forward<P>(x))`. The second form is equivalent to `return emplace_hint(position, std::forward<P>(x))`.

```cpp
template<class... Args>
  pair<iterator, bool> try_emplace(const key_type& k, Args&&... args);
template<class... Args>
  iterator try_emplace(const_iterator hint, const key_type& k, Args&&... args);
```

**3** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `map` from `piecewise_construct`, `forward_as_tuple(k)`, `forward_as_tuple(std::forward<Args>(args)...)`.

**4** *Effects:* If the map already contains an element whose key is equivalent to `k`, there is no effect. Otherwise inserts an object of type `value_type` constructed with `piecewise_construct`, `forward_as_tuple(k)`, `forward_as_tuple(std::forward<Args>(args)...)`.

**5** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**6** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class... Args>
  pair<iterator, bool> try_emplace(key_type&& k, Args&&... args);
template<class... Args>
  iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args);
```

**7** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `map` from `piecewise_construct`, `forward_as_tuple(std::move(k))`, `forward_as_tuple(std::forward<Args>(args)...)`.

**8** *Effects:* If the map already contains an element whose key is equivalent to `k`, there is no effect. Otherwise inserts an object of type `value_type` constructed with `piecewise_construct`, `forward_as_tuple(std::move(k))`, `forward_as_tuple(std::forward<Args>(args)...)`.

**9** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**10** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class M>
  pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj);
template<class M>
  iterator insert_or_assign(const_iterator hint, const key_type& k, M&& obj);
```

**11** *Mandates:* `is_assignable_v<mapped_type&, M&&>` is `true`.

**12** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `map` from `k`, `forward<M>(obj)`.

**13** *Effects:* If the map already contains an element `e` whose key is equivalent to `k`, assigns `std::forward<M>(obj)` to `e.second`. Otherwise inserts an object of type `value_type` constructed with `k`, `std::forward<M>(obj)`.

**14** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**15** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class M>
  pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj);
template<class M>
  iterator insert_or_assign(const_iterator hint, key_type&& k, M&& obj);
```

**16** *Mandates:* `is_assignable_v<mapped_type&, M&&>` is `true`.

**17** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `map` from `move(k)`, `forward<M>(obj)`.

**18** *Effects:* If the map already contains an element `e` whose key is equivalent to `k`, assigns `std::forward<M>(obj)` to `e.second`. Otherwise inserts an object of type `value_type` constructed with `std::move(k)`, `std::forward<M>(obj)`.

**19** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**20** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.
