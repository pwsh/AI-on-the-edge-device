# Parameter `DigitConfidenceThreshold`
Default Value: `0` (disabled)

Range: `0` .. `1`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Minimum confidence a **digit** read must reach to be committed. When a digit's CNN confidence is **below**
this value, the read is treated as **`N`** (unknown) instead of a possibly-wrong guess, and the value is
then resolved from the digit's confident-read history and the carry physics (the same path as a genuine
CNN "N"). It is the class-based digit equivalent of
[CNNGoodThreshold](CNNGoodThreshold.md), which only guards the continuous (`dig-cont`) models.

- **Disabled / `0`** (default): every read is committed regardless of confidence — original behaviour.
- **Enabled**: a typical value is **`0.5`–`0.6`** — high enough to reject genuine "coin-flip" reads
  (glare, blur, a digit mid-roll), low enough not to throw away normal `0.8`–`0.95` reads. Setting it
  very high (e.g. `0.95`+) rejects almost everything and forces near-constant history resolution.

Works together with the per-digit confidence floor used by Fast Read's cache and the temporal-vote
smoothing: a rejected read here is never cached or voted, so the next round re-reads it from the CNN.

!!! Note
    Only the **`Digit`** (class-based, e.g. `dig-class11`) model path applies this. The continuous
    `dig-cont` models use [CNNGoodThreshold](CNNGoodThreshold.md) instead.
