# Parameter `CamNightMode`

**Night Mode (OV3660 / OV5640)**

Values: `enabled` / `disabled`

Default Value: `enabled`

Enables the sensor's **native night mode** (auto frame-rate). In low light the auto-exposure is allowed to
**drop the frame rate** so it can integrate for longer, gathering more light and letting the auto-gain back
off (less noise). Sets bit 2 of the OV3660/OV5640 `AEC_CTRL00` (0x3A00) register, which the driver leaves
off by default; the banding-filter / max-exposure limits it uses are already configured by the sensor init.

For a still water meter the slower frame rate in the dark is harmless, so this is worth leaving on. It has
**no effect on the OV2640** (different sensor).

!!! Note
    A brighter frame in low light comes at the cost of a longer capture in the dark; the pre-capture wait
    (`WaitBeforeTakingPicture`) still applies.
