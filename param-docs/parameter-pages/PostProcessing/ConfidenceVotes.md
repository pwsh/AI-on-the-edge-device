# Parameter `ConfidenceVotes`
Default Value: `3`

Range: `0` (off) or any positive integer (number of confirming reads required).

Confidence vote to recover from a value that got **stuck high**.

The consistency checks normally reject any reading that is *lower* than the last accepted value (a
"negative rate"), because a meter only counts up. But the failure is asymmetric: if a single bad pass
reads a spuriously **high** value and that value is accepted, every following **correct (lower)** read
is then rejected as a negative rate — and the device stays stuck at the wrong high value until the
real meter catches up.

When `ConfidenceVotes` is greater than `0`, the device counts consecutive readings that agree on the
same lower value. Once that many readings agree (within a last-digit tolerance), it concludes the high
value was the outlier and **accepts the lower value**, overriding the stuck-high previous value. A
single isolated low read is still rejected as before; only a confirmed cluster overrides, and any
accepted reading resets the count.

- `3` = **default**: three consecutive confirming lower reads override the suspected-high value.
- `0` = disabled: the classic negative-rate rejection only (a stuck-high value never self-recovers).

When an override happens it is written to the log as
`value corrected from X to Y after N confirming reads`.

!!! Note
    This is a global `[PostProcessing]` parameter — it applies to all number sequences. It is on by
    default (`3`); raise it (e.g. `5`) to require more agreement before overriding, or set `0` to
    disable the recovery entirely.
