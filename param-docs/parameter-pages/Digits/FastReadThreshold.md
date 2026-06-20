# Parameter `FastReadThreshold`
Default Value: `5` (High sensitivity)

Only relevant when [FastRead](FastRead.md) is enabled.

**What it does:** FastRead skips the neural-network inference for digits that have not changed since
the last real reading, reusing the previous result to save time. To decide whether a digit
"changed", it measures the **mean absolute per-pixel difference** between the freshly cropped digit
image and the cached one from the last actual inference. This setting (range `0`–`255`) is the
threshold for that comparison:

- If the measured difference is **below** the threshold, the digit is treated as **unchanged** and
  its previous value is reused (no inference — this is the time saving).
- If it is **at or above** the threshold, the digit is **re-read** with the CNN.

So the value is really a **sensitivity** control:

| Setting | Value | Behaviour |
| --- | --- | --- |
| Very high sensitivity | 3 | Re-reads on the slightest pixel change. Safest, fewest skips. |
| High sensitivity | 5 | Re-reads on small changes. **Default** — errs toward never missing a change. |
| Medium sensitivity | 8 | More skipping; tolerates minor noise. |
| Lower sensitivity | 12 | Tolerates more change before re-reading. |
| Low sensitivity | 20 | Skips unless the digit clearly changes. |
| Very low sensitivity | 32 | Maximum skipping; only obvious changes trigger a re-read. |

**Trade-off:**

- **Higher sensitivity (lower value)** → more digits get re-read: more accurate (catches subtle or
  partial transitions), but less time saved.
- **Lower sensitivity (higher value)** → fewer inferences (faster, less load), but a slowly changing
  or partially-rolling digit may be missed until the periodic full re-read
  ([FastReadFullInterval](FastReadFullInterval.md)) forces a complete pass.

**Choosing a value:** start with the **High** default. If image noise (flickering lighting, JPEG
artefacts) causes unnecessary re-reads, move toward lower sensitivity. If a digit occasionally
changes without being picked up, move toward higher sensitivity. A wrong "changed" decision only
ever costs one extra inference, so erring on the sensitive side is safe — which is why the default
is High.
