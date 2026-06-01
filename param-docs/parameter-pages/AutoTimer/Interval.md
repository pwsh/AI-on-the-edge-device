# Parameter `Interval`
Default Value: `5 minutes`

Choose a number and a unit (**seconds / minutes / hours / days**). Sub-minute intervals
(seconds) are supported and pair well with FastRead. A bare number with no unit (e.g. a legacy
`Interval = 5`) is interpreted as **minutes** for backward compatibility.

Interval in which the Flow (Digitization Round) is run.
It will run immediately on startup and then the next time after the given interval.
If a round takes longer than this interval, the next round gets postponed until the current round completes.

If the flow gets started by a MQTT message or the REST API call, the interval automatically gets reset.

A changed interval can be applied **without a reboot**: after saving the config, call `/reload_config`
(the running flow re-reads the interval and uses the new value on the next round).

!!! Note
    Very short intervals run rounds nearly back-to-back (a round won't be cut short). If you want
    the flow effectively disabled, set a long interval (e.g. `1 days`).
