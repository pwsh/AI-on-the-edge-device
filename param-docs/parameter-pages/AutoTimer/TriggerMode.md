# Parameter `TriggerMode`
Default Value: `interval`

Chooses **how** automatic rounds are triggered:

- **`interval`** (default) — run a round on a fixed cadence, every `Interval` (the classic behaviour).
- **`schedule`** — run a round only at the specific daily times listed in `Schedule`. Multiple time
  slots are supported, so you can capture e.g. only at 08:00, 12:30 and 18:00 instead of continuously.

!!! Note
    In `schedule` mode the `Interval` value is ignored, and the device waits (idle) between the
    scheduled times. Scheduling needs the clock to be set, so configure an NTP `TimeServer` /
    `TimeZone`; until the time syncs the device waits. A manual **Start Round** still works any time.

Possible values:

- `interval`
- `schedule`
