# Parameter `CamSaturation`

**Image-Saturation**

Range: `-2` .. `2` (OV2640) / `-4` .. `4` (OV3660, OV5640)

The configuration pages clamp the field to the range of the **detected** camera.

Default Value: `0`

See [here](../datasheets/Camera.ov2640_ds_1.8_.pdf) for the ov2640 camera datasheet.<br>
See [here](../datasheets/OV5640_datasheet.pdf) for the ov5640 camera datasheet.

!!! Warning
    After changing this parameter you need to update your reference image and alignment markers!

!!! Note
    Positive values increase saturation (more vibrant colors), negative values lower it (more muted colors).

!!! Note
    This parameter can also be set on the Reference Image configuration page!
