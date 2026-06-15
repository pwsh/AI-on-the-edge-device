# Parameter `ValidateServerCert`

Default Value: `enabled`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

Enable or disable the validation of the server certificate CN field.<br>

If **enabled**, the certificate sent by the server is validated against the configured [Root CA Certificate file](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-cacert) — or, if none is configured, against the firmware's built-in Mozilla CA bundle. This means a public broker with a publicly-trusted certificate (for example HiveMQ Cloud) can be verified **without uploading a certificate**.<br>
The server name in the [uri](https://jomjol.github.io/AI-on-the-edge-device-docs/Parameters/#parameter-uri) is also compared with the CN field of the server certificate, and the connection is only established if they match. This confirms the identity of the server.

If **disabled**, the ESP32 skips all validation of the server certificate.<br>
This reduces the security of TLS and makes the *MQTT* client susceptible to MITM attacks.

!!! Note
    This also means you may need to change the protocol and port in the **uri** to `mqtts://example.com:8883`.

    If you use a public broker, it is recommended to set this parameter to **enabled**.
