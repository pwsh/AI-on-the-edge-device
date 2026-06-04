# Parameter `Utility`
Default Value: `` (not set)

Selects the **meter type** so the firmware knows the maximum rate the meter can physically advance.
Accepted values: `water`, `electricity`, `gas` (or *not set* = generic, feature off).

When you pick a type, the **Maximum Rate Value** is auto-populated from typical residential assumptions
(you can override it). These cover the majority of households:

| Type | Assumption | Resulting maximum rate |
|------|-----------|------------------------|
| **Water** | 1″ supply pipe at ~60 psi, ~8 ft/s design velocity | ≈ **20 gallons/min** |
| **Gas** | 1.5″ pipe at residential delivery pressure (~7″ w.c. / 0.25 psi), ~20 ft/s | ≈ **15 ft³/min** (~880 ft³/hr) |
| **Electricity** | 200 A service at 240 V (48 kW) | ≈ **0.8 kWh/min** |

The pipe diameters and electrical service capacity behind these numbers can be changed in the **expert**
options (Water Pipe Diameter, Gas Pipe Diameter, Electrical Service). The auto-populated value is a
*generous ceiling* — its job is to reject readings that are physically impossible (a misread), never to
reject a real household reading. If your meter reads in different units (e.g. m³, CCF), edit
[Maximum Rate Value](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-maxratevalue)
to match.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.Utility`); it is specific to each number sequence defined in the ROIs.
