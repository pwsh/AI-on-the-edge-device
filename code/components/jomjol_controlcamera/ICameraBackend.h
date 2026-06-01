#pragma once

// Abstraction over the raw camera-capture primitives, so the higher-level CCamera logic (zoom,
// quality, JPEG handling, HTTP capture/stream) is independent of *how* frames are acquired.
//
// Today the only backend is the DVP / esp32-camera path (Esp32CameraDvpBackend below). The point of
// the seam is PLAN sec 9.4: a future ESP32-S3 USB-UVC backend - and eventually an IDF-native
// esp_cam_ctlr DVP backend - can implement this same interface without touching CCamera. This is an
// interface extraction only: behaviour is byte-identical to the direct esp_camera_* calls forwarded
// below (camera_fb_t / sensor_t / camera_config_t remain the esp32-camera lingua franca).

#include "esp_camera.h"

class ICameraBackend
{
public:
    virtual ~ICameraBackend() {}

    virtual esp_err_t   init(const camera_config_t *config) = 0; // bring up the sensor + capture path
    virtual esp_err_t   deinit() = 0;
    virtual camera_fb_t *fbGet() = 0;                            // acquire one frame buffer
    virtual void         fbReturn(camera_fb_t *fb) = 0;          // release a frame buffer
    virtual sensor_t    *sensorGet() = 0;                        // sensor handle for register/control access
};


// DVP backend: thin forwarder over the vendored espressif/esp32-camera driver (the validated default
// for the ESP32-CAM and the ESP32-S3 LCD_CAM DVP path).
class Esp32CameraDvpBackend : public ICameraBackend
{
public:
    esp_err_t   init(const camera_config_t *config) override { return esp_camera_init(config); }
    esp_err_t   deinit() override                            { return esp_camera_deinit(); }
    camera_fb_t *fbGet() override                            { return esp_camera_fb_get(); }
    void         fbReturn(camera_fb_t *fb) override          { esp_camera_fb_return(fb); }
    sensor_t    *sensorGet() override                        { return esp_camera_sensor_get(); }
};


// The default backend instance (function-local static -> no cross-TU static-init-order issues).
ICameraBackend *getDefaultCameraBackend();
