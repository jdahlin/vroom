# 27.11.2.4 Remote time zone database support [time.zone.db.remote]

[Contents](../../README.md) · up: [27.11.2 Time zone database \[time.zone.db\]](time.zone.db.md) · prev: [27.11.2.3 Time zone database access \[time.zone.db.access\]](time.zone.db.access.md) · next: [27.11.3 Exception classes \[time.zone.exception\]](time.zone.exception.md)

**1** The local time zone database is that supplied by the implementation when the program first accesses the database, for example via `current_zone()`. While the program is running, the implementation may choose to update the time zone database. This update shall not impact the program in any way unless the program calls the functions in this subclause. This potentially updated time zone database is referred to as the *remote time zone database*.

```cpp
const tzdb& reload_tzdb();
```

**2** *Effects:* This function first checks the version of the remote time zone database. If the versions of the local and remote databases are the same, there are no effects. Otherwise the remote database is pushed to the front of the `tzdb_list` accessed by `get_tzdb_list()`.

**3** *Synchronization:* This function is thread-safe with respect to `get_tzdb_list().front()` and `get_tzdb_list().erase_after()`.

**4** *Postconditions:* No pointers, references, or iterators are invalidated.

**5** *Returns:* `get_tzdb_list().front()`.

**6** *Throws:* `runtime_error` if for any reason a reference cannot be returned to a valid `tzdb`.

```cpp
string remote_version();
```

**7** *Returns:* The latest remote database version.

[*Note 1*: This can be compared with `get_tzdb().version` to discover if the local and remote databases are equivalent. — *end note*]
