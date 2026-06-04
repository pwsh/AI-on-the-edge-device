# Parameter `ServiceAmps`
Default Value: `200`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Electrical **service capacity** in amps, used to compute the maximum energy rate when
[Meter Type](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-utility) is
`electricity`.

**Assumption:** the service runs at 240 V. A 200 A service is therefore 200 × 240 = 48 kW maximum,
which is ≈ **0.8 kWh per minute** — a generous ceiling that covers most households. The rate scales
linearly with the amp rating (e.g. 100 A → 0.4 kWh/min, 400 A → 1.6 kWh/min).

Changing this re-calculates and fills **Maximum Rate Value**. The result only sets the *physically
impossible* threshold used to reject misreads; it never rejects a real reading.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.ServiceAmps`).
