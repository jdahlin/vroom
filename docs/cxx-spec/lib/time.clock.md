# 27.7 Clocks [time.clock]

[Contents](../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.6.7 Conversions \[time.point.cast\]](time.point.cast.md) · next: [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md)

**1** The types defined in this subclause meet the *Cpp17TrivialClock* requirements ([[time.clock.req]](time.clock.req.md)) unless otherwise specified.

## Subclauses

- [27.7.1 Class `system_clock` \[time.clock.system\]](time.clock.system.md)
  - [27.7.1.1 Overview \[time.clock.system.overview\]](time.clock.system.overview.md)
  - [27.7.1.2 Members \[time.clock.system.members\]](time.clock.system.members.md)
  - [27.7.1.3 Non-member functions \[time.clock.system.nonmembers\]](time.clock.system.nonmembers.md)
- [27.7.2 Class `utc_clock` \[time.clock.utc\]](time.clock.utc.md)
  - [27.7.2.1 Overview \[time.clock.utc.overview\]](time.clock.utc.overview.md)
  - [27.7.2.2 Member functions \[time.clock.utc.members\]](time.clock.utc.members.md)
  - [27.7.2.3 Non-member functions \[time.clock.utc.nonmembers\]](time.clock.utc.nonmembers.md)
- [27.7.3 Class `tai_clock` \[time.clock.tai\]](time.clock.tai.md)
  - [27.7.3.1 Overview \[time.clock.tai.overview\]](time.clock.tai.overview.md)
  - [27.7.3.2 Member functions \[time.clock.tai.members\]](time.clock.tai.members.md)
  - [27.7.3.3 Non-member functions \[time.clock.tai.nonmembers\]](time.clock.tai.nonmembers.md)
- [27.7.4 Class `gps_clock` \[time.clock.gps\]](time.clock.gps.md)
  - [27.7.4.1 Overview \[time.clock.gps.overview\]](time.clock.gps.overview.md)
  - [27.7.4.2 Member functions \[time.clock.gps.members\]](time.clock.gps.members.md)
  - [27.7.4.3 Non-member functions \[time.clock.gps.nonmembers\]](time.clock.gps.nonmembers.md)
- [27.7.5 Type `file_clock` \[time.clock.file\]](time.clock.file.md)
  - [27.7.5.1 Overview \[time.clock.file.overview\]](time.clock.file.overview.md)
  - [27.7.5.2 Member functions \[time.clock.file.members\]](time.clock.file.members.md)
  - [27.7.5.3 Non-member functions \[time.clock.file.nonmembers\]](time.clock.file.nonmembers.md)
- [27.7.6 Class `steady_clock` \[time.clock.steady\]](time.clock.steady.md)
- [27.7.7 Class `high_resolution_clock` \[time.clock.hires\]](time.clock.hires.md)
- [27.7.8 Local time \[time.clock.local\]](time.clock.local.md)
- [27.7.9 `time_point` conversions \[time.clock.cast\]](time.clock.cast.md)
  - [27.7.9.1 Class template `clock_time_conversion` \[time.clock.conv\]](time.clock.conv.md)
  - [27.7.9.2 Identity conversions \[time.clock.cast.id\]](time.clock.cast.id.md)
  - [27.7.9.3 Conversions between `system_clock` and `utc_clock` \[time.clock.cast.sys.utc\]](time.clock.cast.sys.utc.md)
  - [27.7.9.4 Conversions between `system_clock` and other clocks \[time.clock.cast.sys\]](time.clock.cast.sys.md)
  - [27.7.9.5 Conversions between `utc_clock` and other clocks \[time.clock.cast.utc\]](time.clock.cast.utc.md)
  - [27.7.9.6 Function template `clock_cast` \[time.clock.cast.fn\]](time.clock.cast.fn.md)
