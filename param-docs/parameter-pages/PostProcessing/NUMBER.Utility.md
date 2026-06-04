# Parameter `Utility`
Default Value: `` (empty — feature off)

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Selects a physics model for this number sequence so the firmware can work out the **maximum rate the
meter could physically advance** between two readings. Accepted values: `water`, `electricity`, `gas`
(empty = generic, the feature is off and behaviour is unchanged).

When set, an automatically-derived **physical ceiling** rejects readings that jump faster than the
supply could possibly deliver — so you no longer need to hand-tune
[MaxRateValue](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-maxratevalue)
just to catch gross misreads. `MaxRateValue`, if set, still applies as a tighter manual override.

The supply model defaults to typical **residential** values and can be fine-tuned with the companion
keys `PipeDiameterMm`, `SupplyPressureKPa`, `ServiceAmps`, `ServiceVolts` and `UnitsPerValue`. The last
one tells the firmware how many SI units (litres for water, kWh for electricity, m³ for gas) equal
`1.0` of the displayed value.

See [docs/PREDICTIVE-READING.md](https://github.com/jomjol/AI-on-the-edge-device/blob/master/docs/PREDICTIVE-READING.md)
for the full logic.

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a
    dot (e.g. `main.Utility`). The reason is that this parameter is specific for each `<NUMBER>`
    (`<NUMBER>` is the name of the number sequence defined in the ROI's).
