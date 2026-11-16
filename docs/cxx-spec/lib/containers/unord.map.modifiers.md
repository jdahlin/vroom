# 22.5.4.4 Modifiers [unord.map.modifiers]

[Contents](../../README.md) · up: [22.5.4 Class template `unordered_map` \[unord.map\]](unord.map.md) · prev: [22.5.4.3 Element access \[unord.map.elem\]](unord.map.elem.md) · next: [22.5.4.5 Erasure \[unord.map.erasure\]](unord.map.erasure.md)

```cpp
template<class P>
  pair<iterator, bool> insert(P&& obj);
```

**1** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**2** *Effects:* Equivalent to: `return emplace(std::forward<P>(obj));`

```cpp
template<class P>
  iterator insert(const_iterator hint, P&& obj);
```

**3** *Constraints:* `is_constructible_v<value_type, P&&>` is `true`.

**4** *Effects:* Equivalent to: `return emplace_hint(hint, std::forward<P>(obj));`

```cpp
template<class... Args>
  pair<iterator, bool> try_emplace(const key_type& k, Args&&... args);
template<class... Args>
  iterator try_emplace(const_iterator hint, const key_type& k, Args&&... args);
```

**5** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `unordered_map` from `piecewise_construct`, `forward_as_tuple(k)`, `forward_as_tuple(std::forward<Args>(args)...)`.

**6** *Effects:* If the map already contains an element whose key is equivalent to `k`, there is no effect. Otherwise inserts an object of type `value_type` constructed with `piecewise_construct`, `forward_as_tuple(k)`, `forward_as_tuple(std::forward<Args>(args)...)`.

**7** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**8** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class... Args>
  pair<iterator, bool> try_emplace(key_type&& k, Args&&... args);
template<class... Args>
  iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args);
```

**9** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `unordered_map` from `piecewise_construct`, `forward_as_tuple(std::move(k))`, `forward_as_tuple(std::forward<Args>(args)...)`.

**10** *Effects:* If the map already contains an element whose key is equivalent to `k`, there is no effect. Otherwise inserts an object of type `value_type` constructed with `piecewise_construct`, `forward_as_tuple(std::move(k))`, `forward_as_tuple(std::forward<Args>(args)...)`.

**11** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**12** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class M>
  pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj);
template<class M>
  iterator insert_or_assign(const_iterator hint, const key_type& k, M&& obj);
```

**13** *Mandates:* `is_assignable_v<mapped_type&, M&&>` is `true`.

**14** *Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `unordered_map` from `k`, `std::forward<M>(obj)`.

**15** *Effects:* If the map already contains an element `e` whose key is equivalent to `k`, assigns `std::forward<M>(obj)` to `e.second`. Otherwise inserts an object of type `value_type` constructed with `k`, `std::forward<M>(obj)`.

**16** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**17** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.

```cpp
template<class M>
  pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj);
template<class M>
  iterator insert_or_assign(const_iterator hint, key_type&& k, M&& obj);
```

**18** *Mandates:* `is_assignable_v<mapped_type&, M&&>` is `true`.

*Preconditions:* `value_type` is *Cpp17EmplaceConstructible* into `unordered_map` from `std::move(k)`, `std::forward<M>(obj)`.

**19** *Effects:* If the map already contains an element `e` whose key is equivalent to `k`, assigns `std::forward<M>(obj)` to `e.second`. Otherwise inserts an object of type `value_type` constructed with `std::move(k)`, `std::forward<M>(obj)`.

**20** *Returns:* In the first overload, the `bool` component of the returned pair is `true` if and only if the insertion took place. The returned iterator points to the map element whose key is equivalent to `k`.

**21** *Complexity:* The same as `emplace` and `emplace_hint`, respectively.
