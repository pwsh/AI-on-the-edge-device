# Parameter `LEDColor`
Default Value: `150 150 150`

Colour of the **external** LED strip in **R**ed, **G**reen, **B**lue, each `0` (off) .. `255` (full on).
It is scaled down by `LEDBrightness` and clamped to the 5V current budget before being sent to the strip
on the `LEDPin` GPIO.
