# Parameter `CamXclk`

**Camera Clock (XCLK) in MHz**

Range (`6` .. `20`)

Default Value: `20`

The camera's master-clock frequency in MHz. It sets the sensor's frame period, which caps the maximum
exposure (integration) time.

**Lowering** the clock (e.g. to `10`) lengthens the frame period, giving the auto-exposure more headroom in
low light and letting the auto-gain back off (less noise) — at the cost of a slower frame rate, which is
irrelevant for a still meter. `20` is the historical default; values below ~`6` make the sensor timing
unreliable.

!!! Note
    Applied live on the OV2640 / OV3660 / OV5640. After a large change the auto-exposure needs a few frames
    to re-settle, so the very first capture may look dark or bright until it converges.
