# Parameter `LeakThreshold`
Default Value: `2` (hours)

How long the meter may advance **continuously** (without the value ever holding steady between two
readings) before
[Leak Detection](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-leakdetection)
raises a potential-leak flag. In hours.

The default of **2 hours** leaves room for legitimately long continuous use — watering the lawn, filling
a pool, a long shower sequence — without false alarms, while still catching a leak that runs for hours.
Lower it for a tighter watch, raise it if you regularly run water/gas continuously for longer.

The elapsed continuous-usage time is published as `continuous_usage` (seconds) so you can see how close
you are to the threshold at any moment.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.LeakThreshold`).
