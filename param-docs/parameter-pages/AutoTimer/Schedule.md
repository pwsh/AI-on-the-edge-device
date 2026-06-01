# Parameter `Schedule`
Default Value: *(empty)*

The daily times at which a round runs when **`TriggerMode = schedule`**. Enter one or more
`HH:MM` times (24-hour clock) separated by commas — **multiple time slots are supported**:

```
Schedule = 08:00,12:30,18:00
```

That example reads the meter three times a day, at 08:00, 12:30 and 18:00, and stays idle in between.
Times are interpreted in the device's local time (`TimeZone`). Order does not matter and duplicates
are ignored.

!!! Note
    Only used when `TriggerMode` is `schedule` (in `interval` mode this field is ignored). The device
    needs a synced clock (NTP) for scheduling; until the time is set it waits. You can always trigger
    an extra round on demand with **Start Round**.

Possible values:

- A comma-separated list of `HH:MM` times, e.g. `06:00,18:00` or `08:00,12:30,18:00,22:15`
