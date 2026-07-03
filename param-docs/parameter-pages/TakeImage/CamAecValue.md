# Parameter `CamAecValue`

**Auto-Exposure-Value**

Range: `0` .. `1200` (OV2640) / `0` .. `1968` (OV3660, OV5640; the sensor additionally limits it to its current frame timing)

The configuration pages clamp the field to the range of the **detected** camera.

Default Value: `160`

See [here](../datasheets/Camera.ov2640_ds_1.8_.pdf) for the ov2640 camera datasheet.<br>
See [here](../datasheets/OV5640_datasheet.pdf) for the ov5640 camera datasheet.

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

	After changing this parameter you need to update your reference image and alignment markers!

!!! Note
    Access the exposure value of the camera, higher values produce brighter images.

!!! Note
    This parameter can also be set on the Reference Image configuration page!
