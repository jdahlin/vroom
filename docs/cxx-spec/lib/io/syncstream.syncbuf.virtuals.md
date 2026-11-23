# 29.10.2.5 Overridden virtual functions [syncstream.syncbuf.virtuals]

[Contents](../../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2.4 Member functions \[syncstream.syncbuf.members\]](syncstream.syncbuf.members.md) · next: [29.10.2.6 Specialized algorithms \[syncstream.syncbuf.special\]](syncstream.syncbuf.special.md)

```cpp
int sync() override;
```

**1** *Effects:* Records that the wrapped stream buffer is to be flushed. Then, if `emit_on_sync` is `true`, calls `emit()`. [*Note 1*: If `emit_on_sync` is `false`, the actual flush is delayed until a call to `emit()`. — *end note*]

**2** *Returns:* If `emit()` was called and returned `false`, returns `-1`; otherwise `0`.
