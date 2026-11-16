# 27.11.2.3 Time zone database access [time.zone.db.access]

[Contents](../README.md) · up: [27.11.2 Time zone database \[time.zone.db\]](time.zone.db.md) · prev: [27.11.2.2 Class `tzdb_list` \[time.zone.db.list\]](time.zone.db.list.md) · next: [27.11.2.4 Remote time zone database support \[time.zone.db.remote\]](time.zone.db.remote.md)

```cpp
tzdb_list& get_tzdb_list();
```

**1** *Effects:* If this is the first access to the time zone database, initializes the database. If this call initializes the database, the resulting database will be a `tzdb_list` holding a single initialized `tzdb`.

**2** *Synchronization:* It is safe to call this function from multiple threads at one time.

**3** *Returns:* A reference to the database.

**4** *Throws:* `runtime_error` if for any reason a reference cannot be returned to a valid `tzdb_list` containing one or more valid `tzdb`s.

```cpp
const tzdb& get_tzdb();
```

**5** *Returns:* `get_tzdb_list().front()`.

```cpp
const time_zone* locate_zone(string_view tz_name);
```

**6** *Returns:* `get_tzdb().locate_zone(tz_name)`.

**7** [*Note 1*: The time zone database will be initialized if this is the first reference to the database. — *end note*]

```cpp
const time_zone* current_zone();
```

**8** *Returns:* `get_tzdb().current_zone()`.
