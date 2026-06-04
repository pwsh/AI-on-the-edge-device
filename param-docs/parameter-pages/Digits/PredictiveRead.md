# Parameter `PredictiveRead`
Default Value: `false`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Adds a **physics-bounded predictive gate** on top of [FastRead](FastRead.md). Where FastRead skips a
digit only after cutting it and finding its pixels unchanged, PredictiveRead skips a digit *before*
doing any work when physics and the carry chain prove it cannot have changed since the last reading.

How it decides (per digit, least-significant first):

- From the configured supply model (see
  [Utility](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-utility) in
  `[PostProcessing]`) and the observed round cadence, the firmware computes the maximum the value
  could have advanced. Digits whose place is out of reach of that maximum **cannot** have changed
  directly.
- Such a digit is read only if a **carry** can reach it from the digit below (the lower digit is close
  enough to its 9→0 wrap), or if the lower digit was read with **low confidence**, or on the periodic
  full-audit round.
- The least-significant digit and the analog dials are always read.

Requirements and safeguards:

- A **`Utility`** model must be set for the number sequence in `[PostProcessing]`; without it this
  gate does nothing (it needs the physical rate bound).
- It relies on the FastRead cache to supply the reused value, and respects
  [FastReadFullInterval](FastReadFullInterval.md) as a drift backstop. The post-processing physical
  plausibility check still rejects any impossible result and forces a full re-read.
- A per-digit confidence is taken from the classification model and a confident lower digit is
  required before the digit above it is skipped.

!!! Note
    This only affects digit ROIs (`Digit` / `dig-class100`). See
    [docs/PREDICTIVE-READING.md](https://github.com/jomjol/AI-on-the-edge-device/blob/master/docs/PREDICTIVE-READING.md)
    for the full algorithm.
