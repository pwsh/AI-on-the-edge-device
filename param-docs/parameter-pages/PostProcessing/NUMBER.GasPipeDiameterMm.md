# Parameter `GasPipeDiameterMm`
Default Value: `38.1` (1.5″)

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Inner diameter (in millimetres) of the **gas pipe**, used to compute the maximum flow when
[Meter Type](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-utility) is
`gas`.

**Assumptions:** residential delivery pressure (~7″ water column ≈ 0.25 psi) and a ~20 ft/s velocity.
With the default 1.5″ (38.1 mm) pipe this gives ≈ **15 ft³/min** (~880 ft³/hr) — a generous ceiling
that covers most households. Flow scales with the square of the diameter.

Changing this re-calculates and fills **Maximum Rate Value**. The result only sets the *physically
impossible* threshold used to reject misreads; it never rejects a real reading.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.GasPipeDiameterMm`).
