# Parameter `LEDBrightness`
Default Value: `100`

Output level of the **external** LED strip (the `WS281x` / NeoPixel on the `LEDPin` GPIO), in percent
`0` .. `100`. It scales the configured `LEDColor` down before it is sent to the strip, so you can dim
the illumination without changing the colour. The scale is perceptual (gamma-corrected), so `20` looks
roughly a fifth as bright rather than a raw 20 % duty.

The result is additionally limited by the board's 5V current budget: if the chosen colour, LED count and
brightness would draw more than the budget (≈ `60 mA` per LED at full white), the firmware automatically
dims the strip to stay within it. Raise the budget by setting `LEDPowerInjection` to **enabled**.
