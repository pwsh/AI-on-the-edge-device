# Parameter `ConfidenceVotes`
Default Value: `0`

Range: `0` (off) .. `10`.

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

- `0` = disabled (default): the classic negative-rate rejection only.
- e.g. `3` = three consecutive confirming lower reads override the suspected-high value.

When an override happens it is written to the log as
`value corrected from X to Y after N confirming reads`.

!!! Note
    This is a global `[PostProcessing]` parameter — it applies to all number sequences. Set a small
    value (e.g. `3`–`5`) only if you see your meter occasionally latch onto a wrong high reading; leave
    it at `0` otherwise.
