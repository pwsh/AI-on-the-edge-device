# Parameter `FastReadFullInterval`
Default Value: `20`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Only relevant when [FastRead](FastRead.md) is enabled.

Forces a full re-read of every digit ROI (bypassing the change cache) every `N` cycles,
as a backstop against accumulated error or a digit that drifted while it was being
skipped. `1` means "never skip" (equivalent to FastRead off for accuracy, but the model
stays resident). Larger values do more skipping and are faster but rely more on the
per-digit change detection.
