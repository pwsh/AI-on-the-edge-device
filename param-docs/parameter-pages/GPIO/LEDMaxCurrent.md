# Parameter `LEDMaxCurrent`
Default Value: `500`

Maximum current (in **mA**) available to the **external** LED strip from your injected 5V supply. Only
used when `LEDPowerInjection` is **enabled**; ignored otherwise (the board default of `500 mA` applies).

Set this to the rating of the supply (and wiring) that feeds the strip. The firmware estimates the
strip's draw (≈ `60 mA` per LED at full white, scaled by `LEDColor` and `LEDBrightness`) and dims the
strip if it would exceed this value, so a higher number allows a brighter/longer strip.
