# 27.1 General [time.general]

[Contents](../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27 Time library \[time\]](time.md) · next: [27.2 Header `<chrono>` synopsis \[time.syn\]](time.syn.md)

**1** This Clause describes the chrono library ([[time.syn]](time.syn.md)) and various C functions ([[ctime.syn]](ctime.syn.md)) that provide generally useful time utilities, as summarized in [Table 96](time.general.md#tab:time.summary).

<a id="tab:time.summary"></a>
**Table 96: Time library summary** [tab:time.summary]

|  |  |  |
| --- | --- | --- |
| [[time.clock.req]](time.clock.req.md) | *Cpp17Clock* requirements |  |
| [[time.traits]](time.traits.md) | Time-related traits | `<chrono>` |
| [[time.duration]](time.duration.md) | Class template `duration` |  |
| [[time.point]](time.point.md) | Class template `time_point` |  |
| [[time.clock]](time.clock.md) | Clocks |  |
| [[time.cal]](time.cal.md) | Civil calendar |  |
| [[time.hms]](time.hms.md) | Class template `hh_mm_ss` |  |
| [[time.12]](time.12.md) | 12/24 hour functions |  |
| [[time.zone]](time.zone.md) | Time zones |  |
| [[time.format]](time.format.md) | Formatting |  |
| [[time.parse]](time.parse.md) | Parsing |  |
| [[ctime.syn]](ctime.syn.md) | C library time utilities | `<ctime>` |

**2** Let `STATICALLY-WIDEN``<charT>("...")` be `"..."` if `charT` is `char` and `L"..."` if `charT` is `wchar_t`.
