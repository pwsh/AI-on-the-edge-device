# Parameter `WaterPipeDiameterMm`
Default Value: `25.4` (1″)

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Inner diameter (in millimetres) of the **water supply pipe**, used to compute the maximum flow when
[Meter Type](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-utility) is
`water`.

**Assumptions:** ~60 psi residential supply pressure and a ~8 ft/s design velocity. With the default
1″ (25.4 mm) pipe this gives ≈ **20 gallons/min** — a generous ceiling that covers most households.
Flow scales with the square of the diameter (`gpm ≈ 2.448 · d_inch² · 8`).

Changing this re-calculates and fills **Maximum Rate Value**. The result only sets the *physically
impossible* threshold used to reject misreads; it never rejects a real reading.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.WaterPipeDiameterMm`).
