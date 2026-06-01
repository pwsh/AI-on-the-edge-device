#pragma once

// Extract a panic coredump from the `coredump` flash partition to /sdcard/log/crash/ on the next
// boot, so a backtrace can be retrieved remotely via the fileserver (no USB needed). Writes a
// human-readable summary (.txt: faulting task, exception PC, backtrace) plus the raw ELF image
// (.elf, for offline `espcoredump.py`), then erases the stored dump so it is not re-saved on every
// subsequent boot. Safe to call unconditionally every boot: it is a no-op when no dump is stored.
//
// Requires the SD card to be mounted and the log directories to exist before it is called.
void saveCoreDumpToSD(void);
