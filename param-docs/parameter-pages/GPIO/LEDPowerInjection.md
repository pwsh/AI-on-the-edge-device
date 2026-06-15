# Parameter `LEDPowerInjection`
Default Value: `disabled`

Whether the **external** LED strip is powered from a separate 5V supply (power injection) instead of the
board's own 5V rail.

* **disabled** (default): the firmware caps the strip's estimated current draw at the board's safe 5V
  budget (`500 mA`) and dims the strip if `LEDColor` x `LEDNumbers` x `LEDBrightness` would exceed it.
  This protects the board from brown-outs.
* **enabled**: the cap is taken from `LEDMaxCurrent` (your injected supply's capacity) instead, allowing
  a larger / brighter strip. Only select this if you actually feed 5V into the strip from an adequate
  external supply with a common ground.
