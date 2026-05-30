# Parameter `FastRead`
Default Value: `false`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Enables incremental reading of the digit ROIs. When enabled, the CNN only runs an
inference on a digit whose cropped image actually changed since the last reading; an
unchanged digit reuses its previous result. On a typical meter only the lowest one or
two digits move between two readings taken a few seconds apart, so this drastically
reduces the number of inferences per cycle and allows much shorter intervals (target
5–10 s).

Correctness safeguards:

- A digit is only reused while its pixels are stable; as soon as it starts to roll, its
  pixels change and it is re-read. A carry into a higher digit is therefore detected by
  that digit's own pixel change.
- A full re-read of every digit is forced periodically (see
  [FastReadFullInterval](FastReadFullInterval.md)) and whenever post-processing requests
  a full validation (e.g. on a consistency failure).
- With FastRead the tflite model is kept loaded between cycles, which uses more heap but
  removes the per-cycle model load/allocate overhead.

!!! Note
    This only affects digit ROIs (`Digit` and `dig-class100` models). Analog ROIs are
    always read in full.
