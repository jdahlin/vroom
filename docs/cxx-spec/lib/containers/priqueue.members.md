# 22.6.5.4 Members [priqueue.members]

[Contents](../../README.md) · up: [22.6.5 Class template `priority_queue` \[priority.queue\]](priority.queue.md) · prev: [22.6.5.3 Constructors with allocators \[priqueue.cons.alloc\]](priqueue.cons.alloc.md) · next: [22.6.5.5 Specialized algorithms \[priqueue.special\]](priqueue.special.md)

```cpp
void push(const value_type& x);
```

**1** *Effects:* As if by:

```cpp
c.push_back(x);
push_heap(c.begin(), c.end(), comp);
```

```cpp
void push(value_type&& x);
```

**2** *Effects:* As if by:

```cpp
c.push_back(std::move(x));
push_heap(c.begin(), c.end(), comp);
```

```cpp
template<class... Args> void emplace(Args&&... args);
```

**3** *Effects:* As if by:

```cpp
c.emplace_back(std::forward<Args>(args)...);
push_heap(c.begin(), c.end(), comp);
```

```cpp
void pop();
```

**4** *Effects:* As if by:

```cpp
pop_heap(c.begin(), c.end(), comp);
c.pop_back();
```
