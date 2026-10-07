# Parameter `UnitsPerValue`
Default Value: `` (not set = derived automatically)

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

How many **physical (SI) units** one unit of the meter's displayed value represents. The physics model
behind [Utility](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-utility)
computes its maximum rate in SI units and divides by this factor to get the ceiling in the meter's own
units:

| Utility | SI unit | Example values |
|---------|---------|----------------|
| `water` | litres | m³ = `1000`, litres = `1`, US gallons = `3.78541`, ft³ = `28.3168` |
| `gas` | m³ | m³ = `1`, ft³ = `0.0283168` |
| `electricity` | kWh | kWh = `1`, Wh = `0.001`, MWh = `1000`, GJ = `277.778` |

**When not set**, the value is derived from the MQTT
[Meter Type](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-metertype)
when it matches the Utility (e.g. `water_gal_min` → `3.78541`). Without a matching Meter Type the
default is m³ for water (`1000`), m³ for gas (`1`) and kWh for electricity (`1`). An explicitly
configured value always wins.

A wrong factor makes the ceiling wrong by the same ratio — e.g. a gallon meter treated as m³ gets a
ceiling about 1000× too small, and every normal flow is flagged as exceeding the physical max. The
effective value and the resulting ceiling (in meter units per minute) are written to the log at startup.

!!! Note
    If you edit the config file manually, prefix this parameter with `<NUMBER>` and a dot (e.g.
    `main.UnitsPerValue`); it is specific to each number sequence defined in the ROIs. It is not shown
    on the configuration page.
