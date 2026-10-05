# Parameter `ROIImagesMode`
Default Value: `all`

Selects **which** digit ROI images are written when [Save ROI Images](ROIImages.md) is enabled.
Useful to turn ROI image logging into a low-effort collector of training data.

- **`all`** (default): every inferred digit ROI is saved every round (the classic behaviour).
- **`changed`**: a ROI image is only saved when
    - its label differs from the last label *saved* for that ROI (the first round after boot always saves), or
    - the read was **unsure**: confidence below the internal history floor (70 %), rejected by
      [Digit Confidence Threshold](DigitConfidenceThreshold.md), replaced by the temporal vote, or an `N` read
      (for the `DoubleHyprid10` models: rejected by [CNN Good Threshold](CNNGoodThreshold.md)).

    A meter that does not move then writes (almost) nothing, while every change and every doubtful read is still captured.

Image file names start with the label, followed by the model confidence in percent (if known), e.g.
`7_c93_main_dig3_20260105-142301.jpg` (`<label>_c<confidence>_<number>_<roi>_<timestamp>.jpg`).
Digits that are reused by Fast Read / Predictive Read (no inference this round) are not saved.

!!! Note
    Images are saved in folders per day and hour below [ROI Images Location](ROIImagesLocation.md) and are
    deleted after [ROI Images Retention](ROIImagesRetention.md) days.
