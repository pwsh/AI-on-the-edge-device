# Parameter `ROIImagesMode`
Default Value: `all`

Selects **which** analog ROI images are written when [Save ROI Images](ROIImages.md) is enabled.
Useful to turn ROI image logging into a low-effort collector of training data.

- **`all`** (default): every analog ROI is saved every round (the classic behaviour).
- **`changed`**: a ROI image is only saved when its reading (rounded to one decimal, as used in the file name)
  differs from the last reading *saved* for that ROI. The first round after boot always saves.

Image file names start with the reading, followed by the model confidence in percent (if known), e.g.
`3.7_c96_ana1_20260105-142301.jpg` (`<reading>_c<confidence>_<roi>_<timestamp>.jpg`; with the 100-class
analog models the number name precedes the ROI name, e.g. `3.7_c88_main_ana1_...`).

!!! Note
    Images are saved in folders per day and hour below [ROI Images Location](ROIImagesLocation.md) and are
    deleted after [ROI Images Retention](ROIImagesRetention.md) days.
