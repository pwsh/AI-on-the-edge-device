# Parameter `ResolveUnknownDigits`
Default Value: `false`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

When a digit cannot be classified (it would normally read as `N`), resolve it to a best-guess integer
from that digit's **matrix of recent confident reads** instead of leaving it unknown, so the sequence
still produces a complete number. A digit that has never been read confidently stays `N`.

The resolution uses the predictive analysis:

- **Cannot increment** (physics + the carry chain show it could not have changed) → use the digit's
  majority value, otherwise its most recent confident read.
- **Could increment, but the digit below looks unchanged** → assume it did not change → use the most
  recent confident read.
- **Could increment and the digit below changed** → genuinely uncertain → best effort: the most recent
  confident read (so every digit still yields a valid integer).

Only values the CNN actually read with confidence enter the matrix — resolved/inferred values never do.
The matrix is shown on the overview page under each number sequence.

!!! Note
    This works best together with
    [PredictiveRead](https://github.com/jomjol/AI-on-the-edge-device/blob/master/param-docs/parameter-pages/Digits/PredictiveRead.md)
    (which supplies the "can this digit increment?" signal) but is independent of it. Only affects
    digit ROIs (`Digit`). See
    [docs/PREDICTIVE-READING.md](https://github.com/jomjol/AI-on-the-edge-device/blob/master/docs/PREDICTIVE-READING.md).
