# Parameter `BackupInterval`
Default Value: `0`

Automatically save a backup zip of your configuration to the SD card on a fixed schedule.

The backup contains everything needed to restore onto a fresh card: the `config` folder
(config.ini, reference images, alignment data, prevalue) plus `wlan.ini`, and any **custom**
model file that is not part of the default installation. Default model files are skipped on
purpose, since a fresh install already provides them.

Backups are written to `/sdcard/backup/` and named `<hostname>_<YYYYMMDD-HHMMSS>.zip`.
Only the newest 5 scheduled backups are kept; older ones are pruned automatically.

You can also download a backup on demand at any time from `http://<device>/backup`.

!!! Note
    The interval is measured in **days** and the timer starts at boot. A value of `0`
    (or leaving this parameter disabled) turns scheduled backups off.

Possible values:

- `0` – disabled (default)
- any positive whole number of days (e.g. `7` for a weekly backup)
