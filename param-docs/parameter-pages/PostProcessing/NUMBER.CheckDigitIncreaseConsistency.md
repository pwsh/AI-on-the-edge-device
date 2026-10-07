# Parameter `CheckDigitIncreaseConsistency`
Default Value: `false`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

An additional consistency check.
It especially improves the zero crossing check between digits.

!!! Warning
    Only use this for **mechanical rolling-digit counters** (number wheels that turn and can show a
    digit half-way between two values). It **must be OFF for LCD, 7-segment and other electronic
    displays**: there it rewrites correct readings from the previous value, so the reported value
    drifts away from the meter (often in growing jumps) even though the raw reading is right.
    The firmware refuses corrections that change more than the lowest digit it checks and logs a
    warning recommending to switch this option off.

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.CheckDigitIncreaseConsistency`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).
