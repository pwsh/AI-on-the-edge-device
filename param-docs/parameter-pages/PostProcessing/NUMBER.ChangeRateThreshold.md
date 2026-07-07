# Parameter `ChangeRateThreshold`
Default Value: `2` (in the shipped configuration; **unchecked / not configured = disabled**)

Range: `0` .. `9`. `0` disables the hold.

Suppresses tiny flicker in the **last digit** so a stationary meter doesn't drift. If a new reading
differs from the last accepted value by no more than this many counts of the **smallest digit**, the
reading is treated as unchanged and the previous Value/PreValue is kept (no further calculation is done).

**Checkbox semantics:** unchecking this parameter (or removing it from the config) disables the hold
completely — the value then follows every accepted reading exactly. Enable it with a value of 1-9 to
absorb that many counts of last-digit jitter.

This compensates for small recognition fluctuations that happen when the meter sits still for a long time
(e.g. overnight) or the last pointer/digit wobbles slightly backwards — common on water meters. It is
applied **only to the last digit** of the read value (see the example below): if the value is within
`PreValue ± Threshold`, the old value is held.

When the band absorbs a reading that actually differed, the status reports
`no error - held by change-rate threshold (read X)` (data log, MQTT, REST), so a held value is
distinguishable from a genuinely unchanged meter. Note this also means a slowly creeping meter is
reported **up to `Threshold` counts behind** until it moves past the band — set `0` to disable if you
prefer the value to track every accepted reading exactly.

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.ChangeRateThreshold`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).

## Example

- Smallest ROI provides value for `0.000'x` (Eg. a water meter with 4 pointers behind the decimal point)
- ChangeRateThreshold = 2
  
#### With `ExtendedResolution` **disabled**
PreValue: `123.456'7` -> Threshold = `+/-0.000'2`.<br>
All changes between `123.456'5` and `123.456'9` get ignored
	
#### With `ExtendedResolution` **enabled**
PreValue: `123.456'78` -> Threshold = `+/-0.000'02`.<br>
All changes between `123.456'76` and `123.456'80` get ignored.

![](img/ChangeRateThreshold.png)
