#pragma once

#ifndef JOMJOL_CONTROLCAMERA_H
#define JOMJOL_CONTROLCAMERA_H

#include <esp_log.h>

#include <esp_http_server.h>

//#include "ClassControllCamera.h"

void register_server_camera_uri(httpd_handle_t server);
// Pulse the camera power-down line to power-cycle a (possibly stuck) sensor.
// downMs = how long to hold it powered down before powering back up.
void PowerResetCamera(int downMs = 1000);

#endif