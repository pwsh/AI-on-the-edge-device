# Parameter `ErrorMessage`
Default Value: `true`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Controls what is transmitted (MQTT, InfluxDB, REST, ...) when a reading is rejected by the
consistency checks (negative rate or rate-too-high).

- `true` (default): **Skip messages on error.** No value is transmitted for that reading
  (the value field is left empty), so consumers see a gap rather than a wrong number. The
  internal value still falls back to the last valid value for the next round.
- `false`: On an error, transmit the **last valid value** instead of skipping, so dashboards
  stay continuous. (The rate is still omitted, since the rate itself was rejected.)
