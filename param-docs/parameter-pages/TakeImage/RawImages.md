# Parameter `RawImages`
Default Value: `disabled`

Master on/off switch for **saving the raw camera images** to the SD card (one full-frame JPG per round).

* **disabled** (default): no raw images are written — recommended for normal operation, since frequent
  SD writes wear the card and can slow down or stall rounds on a slow/failing card.
* **enabled**: each round's raw image is saved under [Raw Images Location](RawImagesLocation.md)
  (defaults to `/log/source` if no location is set) and pruned after
  [Raw Images Retention](RawImagesRetention.md) days.

This is the clear, explicit toggle for raw-image logging. The separate **Raw Images Location** / **Raw
Images Retention** settings only control *where* the images go and *how long* they are kept; they no longer
silently turn saving on. (For backward compatibility, an older config that set a Raw Images Location but
has no `RawImages` line is still treated as enabled.)

!!! Warning
    An SD-Card has limited write cycles. Enabling this writes an image every round — leave it **disabled**
    unless you are actively debugging recognition, and turn it back off afterwards.
