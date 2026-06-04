# Parameter `LeakDetection`
Default Value: `false` (enabled automatically when Meter Type is water or gas)

Flags a **potential leak** when the meter advances *continuously* — i.e. no two consecutive readings
are ever the same — for longer than the
[Leak Threshold](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-leakthreshold).
A real leak causes flow that never stops, whereas normal use is intermittent (the value pauses between
uses).

Selecting a **Meter Type** of `water` or `gas` turns this on by default; `electricity` turns it off
(standby loads make a power meter advance continuously by design). You can override either way.

Two values are published to MQTT, InfluxDB, the REST `/json`, and Home Assistant when this is enabled:

* **`leak`** — a binary "potential leak detected" flag (Home Assistant `moisture` binary sensor).
* **`continuous_usage`** — seconds since the value last held steady (Home Assistant `duration` sensor).

!!! Note
    Use this on the **cumulative total** sequence (where an unchanged reading means no flow), not on an
    instantaneous rate sequence. If you edit the config file manually, prefix with `<NUMBER>.` (e.g.
    `main.LeakDetection`).
