# Parameter `RetainMessages`
Default Value: `enabled`

Enable or disable the [Retain Flag](https://www.hivemq.com/blog/mqtt-essentials-part-8-retained-messages/) for all MQTT entries.

!!! Warning
    Setting this to **disabled** does not clear the last retained value on the MQTT broker! This must be done manually, see [How to Delete Retained Messages in MQTT?](https://www.hivemq.com/blog/mqtt-essentials-part-8-retained-messages/#heading-how-to-delete-retained-messages-in-mqtt) and this [discussion](https://github.com/jomjol/AI-on-the-edge-device/discussions/3534#discussioncomment-12199543)!
