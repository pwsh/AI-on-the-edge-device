#include <iostream>
#include <string>
#include <vector>
#include <regex>

#include "ClassFlowTakeImage.h"
#include "Helper.h"
#include "ClassLogFile.h"

#include "CImageBasis.h"
#include "ClassControllCamera.h"
#include "MainFlowControl.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "../../include/defines.h"
#include "psram.h"

#include <time.h>

// #define DEBUG_DETAIL_ON
// #define WIFITURNOFF

static const char *TAG = "TAKEIMAGE";

// Per-sensor clamp limits live in camSensorClampLimit() (ClassControllCamera.h), shared with the
// live-stream / reference-editor query parser so every path accepts the same per-sensor ranges.


esp_err_t ClassFlowTakeImage::camera_capture(void)
{
    string nm = namerawimage;
    Camera.CaptureToFile(nm);
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return ESP_OK;
}

esp_err_t ClassFlowTakeImage::takePictureWithFlash(int flash_duration)
{
    // in case the image is flipped, it must be reset here //
    rawImage->width = CCstatus.ImageWidth;
    rawImage->height = CCstatus.ImageHeight;

    ESP_LOGD(TAG, "flash_duration: %d", flash_duration);

    esp_err_t result = Camera.CaptureToBasisImage(rawImage, flash_duration);

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    // Only persist the raw image when we actually have one (a failed decode leaves rawImage NULL/black).
    if ((result == ESP_OK) && CCstatus.SaveAllFiles)
    {
        rawImage->SaveToFile(namerawimage);
    }

    return result;
}

void ClassFlowTakeImage::SetInitialParameter(void)
{
    TimeImageTaken = 0;
    rawImage = NULL;
    disabled = false;
    namerawimage = "/sdcard/img_tmp/raw.jpg";
}

// read the camera settings from config.ini
// called at startup
bool ClassFlowTakeImage::ReadParameter(FILE *pfile, string &aktparamgraph)
{
    Camera.getSensorDatenToCCstatus(); // Kamera >>> CCstatus

    std::vector<string> splitted;

    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0)
    {
        if (!this->GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if (aktparamgraph.compare("[TakeImage]") != 0)
    {
        // Paragraph does not fit TakeImage
        return false;
    }

    // Tracks an explicit "RawImages = true/false" toggle so it wins regardless of line order; -1 = unset.
    int rawImagesExplicit = -1;

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        splitted = ZerlegeZeile(aktparamgraph);

        if ((toUpper(splitted[0]) == "RAWIMAGES") && (splitted.size() > 1))
        {
            // Explicit master on/off for saving raw images. Wins over the legacy "a location implies on"
            // behaviour below, regardless of line order. With no location set, images go to the default.
            rawImagesExplicit = alphanumericToBoolean(splitted[1]) ? 1 : 0;
            isLogImage = (rawImagesExplicit == 1);
        }

        else if ((toUpper(splitted[0]) == "RAWIMAGESLOCATION") && (splitted.size() > 1))
        {
            imagesLocation = "/sdcard" + splitted[1];
            if (rawImagesExplicit == -1) isLogImage = true;   // back-compat: a configured location enables saving unless RawImages overrides
        }

        else if ((toUpper(splitted[0]) == "RAWIMAGESRETENTION") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                this->imagesRetention = std::stod(splitted[1]);
            }          
        }

        else if ((toUpper(splitted[0]) == "SAVEALLFILES") && (splitted.size() > 1))
        {
            CCstatus.SaveAllFiles = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "WAITBEFORETAKINGPICTURE") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _WaitBeforePicture = std::stoi(splitted[1]);
                // Allow 0 = no pre-capture/flash delay (previously 0 was forced back to the default 2,
                // so the delay could never be disabled). A negative value still falls back to the default.
                CCstatus.WaitBeforePicture = (_WaitBeforePicture >= 0) ? _WaitBeforePicture : 2;
            }
        }

        else if ((toUpper(splitted[0]) == "CAMGAINCEILING") && (splitted.size() > 1))
        {
            std::string _ImageGainceiling = toUpper(splitted[1]);

            if (isStringNumeric(_ImageGainceiling))
            {
                int _ImageGainceiling_ = std::stoi(_ImageGainceiling);
                switch (_ImageGainceiling_)
                {
                case 1:
                    CCstatus.ImageGainceiling = GAINCEILING_4X;
                    break;
                case 2:
                    CCstatus.ImageGainceiling = GAINCEILING_8X;
                    break;
                case 3:
                    CCstatus.ImageGainceiling = GAINCEILING_16X;
                    break;
                case 4:
                    CCstatus.ImageGainceiling = GAINCEILING_32X;
                    break;
                case 5:
                    CCstatus.ImageGainceiling = GAINCEILING_64X;
                    break;
                case 6:
                    CCstatus.ImageGainceiling = GAINCEILING_128X;
                    break;
                default:
                    CCstatus.ImageGainceiling = GAINCEILING_2X;
                }
            }
            else
            {
                if (_ImageGainceiling == "X4")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_4X;
                }
                else if (_ImageGainceiling == "X8")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_8X;
                }
                else if (_ImageGainceiling == "X16")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_16X;
                }
                else if (_ImageGainceiling == "X32")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_32X;
                }
                else if (_ImageGainceiling == "X64")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_64X;
                }
                else if (_ImageGainceiling == "X128")
                {
                    CCstatus.ImageGainceiling = GAINCEILING_128X;
                }
                else
                {
                    CCstatus.ImageGainceiling = GAINCEILING_2X;
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMQUALITY") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageQuality = std::stoi(splitted[1]);
                CCstatus.ImageQuality = clipInt(_ImageQuality, 63, 6);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMXCLK") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageXclk = std::stoi(splitted[1]);
                // Camera master clock in MHz. 20 is the historical default; lower lengthens the frame
                // period (more exposure headroom). Clamp 6..20 - below ~6 the sensor timing gets flaky.
                CCstatus.ImageXclk = clipInt(_ImageXclk, 20, 6);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMBRIGHTNESS") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageBrightness = std::stoi(splitted[1]);
                int lim = camSensorClampLimit(CCstatus.CamSensor_id, 2, 3, 3);
                CCstatus.ImageBrightness = clipInt(_ImageBrightness, lim, -lim);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMCONTRAST") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageContrast = std::stoi(splitted[1]);
                int lim = camSensorClampLimit(CCstatus.CamSensor_id, 2, 3, 3);
                CCstatus.ImageContrast = clipInt(_ImageContrast, lim, -lim);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMSATURATION") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageSaturation = std::stoi(splitted[1]);
                int lim = camSensorClampLimit(CCstatus.CamSensor_id, 2, 4, 4);
                CCstatus.ImageSaturation = clipInt(_ImageSaturation, lim, -lim);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMSHARPNESS") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageSharpness = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageSharpness = clipInt(_ImageSharpness, 2, -2);
                }
                else
                {
                    CCstatus.ImageSharpness = clipInt(_ImageSharpness, 3, -3);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMAUTOSHARPNESS") && (splitted.size() > 1))
        {
            CCstatus.ImageAutoSharpness = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMSPECIALEFFECT") && (splitted.size() > 1))
        {
            std::string _ImageSpecialEffect = toUpper(splitted[1]);

            if (isStringNumeric(_ImageSpecialEffect))
            {
                int _ImageSpecialEffect_ = std::stoi(_ImageSpecialEffect);
                CCstatus.ImageSpecialEffect = clipInt(_ImageSpecialEffect_, 6, 0);
            }
            else
            {
                if (_ImageSpecialEffect == "NEGATIVE")
                {
                    CCstatus.ImageSpecialEffect = 1;
                }
                else if (_ImageSpecialEffect == "GRAYSCALE")
                {
                    CCstatus.ImageSpecialEffect = 2;
                }
                else if (_ImageSpecialEffect == "RED")
                {
                    CCstatus.ImageSpecialEffect = 3;
                }
                else if (_ImageSpecialEffect == "GREEN")
                {
                    CCstatus.ImageSpecialEffect = 4;
                }
                else if (_ImageSpecialEffect == "BLUE")
                {
                    CCstatus.ImageSpecialEffect = 5;
                }
                else if (_ImageSpecialEffect == "RETRO")
                {
                    CCstatus.ImageSpecialEffect = 6;
                }
                else
                {
                    CCstatus.ImageSpecialEffect = 0;
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMWBMODE") && (splitted.size() > 1))
        {
            std::string _ImageWbMode = toUpper(splitted[1]);

            if (isStringNumeric(_ImageWbMode))
            {
                int _ImageWbMode_ = std::stoi(_ImageWbMode);
                CCstatus.ImageWbMode = clipInt(_ImageWbMode_, 4, 0);
            }
            else
            {
                if (_ImageWbMode == "SUNNY")
                {
                    CCstatus.ImageWbMode = 1;
                }
                else if (_ImageWbMode == "CLOUDY")
                {
                    CCstatus.ImageWbMode = 2;
                }
                else if (_ImageWbMode == "OFFICE")
                {
                    CCstatus.ImageWbMode = 3;
                }
                else if (_ImageWbMode == "HOME")
                {
                    CCstatus.ImageWbMode = 4;
                }
                else
                {
                    CCstatus.ImageWbMode = 0;
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMAWB") && (splitted.size() > 1))
        {
            CCstatus.ImageAwb = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMAWBGAIN") && (splitted.size() > 1))
        {
            CCstatus.ImageAwbGain = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMAEC") && (splitted.size() > 1))
        {
            CCstatus.ImageAec = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMAEC2") && (splitted.size() > 1))
        {
            CCstatus.ImageAec2 = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMAELEVEL") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageAeLevel = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageAeLevel = clipInt(_ImageAeLevel, 2, -2);
                }
                else
                {
                    CCstatus.ImageAeLevel = clipInt(_ImageAeLevel, 5, -5);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMAECVALUE") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageAecValue = std::stoi(splitted[1]);
                // OV3660/OV5640 accept longer manual exposures; the driver additionally clamps to
                // the sensor's current frame timing (VTS), so an over-ask is safe.
                CCstatus.ImageAecValue = clipInt(_ImageAecValue, camSensorClampLimit(CCstatus.CamSensor_id, 1200, 1968, 1968), 0);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMAGC") && (splitted.size() > 1))
        {
            CCstatus.ImageAgc = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMAGCGAIN") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageAgcGain = std::stoi(splitted[1]);
                // OV2640 gain tops out at 30; the OV3660/OV5640 drivers accept 0..64.
                CCstatus.ImageAgcGain = clipInt(_ImageAgcGain, camSensorClampLimit(CCstatus.CamSensor_id, 30, 64, 64), 0);
            }
        }

        else if ((toUpper(splitted[0]) == "CAMBPC") && (splitted.size() > 1))
        {
            CCstatus.ImageBpc = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMWPC") && (splitted.size() > 1))
        {
            CCstatus.ImageWpc = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMCOLORBAR") && (splitted.size() > 1))
        {
            // Sensor test pattern (colour bars). Diagnostic only - leave off for normal reading.
            CCstatus.ImageColorbar = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMNIGHTMODE") && (splitted.size() > 1))
        {
            // OV3660/OV5640 native night mode (auto frame-rate for longer low-light exposure).
            CCstatus.ImageNightMode = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMRAWGMA") && (splitted.size() > 1))
        {
            CCstatus.ImageRawGma = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMLENC") && (splitted.size() > 1))
        {
            CCstatus.ImageLenc = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMHMIRROR") && (splitted.size() > 1))
        {
            CCstatus.ImageHmirror = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMVFLIP") && (splitted.size() > 1))
        {
            CCstatus.ImageVflip = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMDCW") && (splitted.size() > 1))
        {
            CCstatus.ImageDcw = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMDENOISE") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageDenoiseLevel = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageDenoiseLevel = 0;
                }
                else
                {
                    CCstatus.ImageDenoiseLevel = clipInt(_ImageDenoiseLevel, 8, 0);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMZOOM") && (splitted.size() > 1))
        {
            CCstatus.ImageZoomEnabled = alphanumericToBoolean(splitted[1]);
        }

        else if ((toUpper(splitted[0]) == "CAMZOOMOFFSETX") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageZoomOffsetX = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 480, -480);
                }
                else if (CCstatus.CamSensor_id == OV3660_PID)
                {
                    CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 704, -704);
                }
                else if (CCstatus.CamSensor_id == OV5640_PID)
                {
                    CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 960, -960);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMZOOMOFFSETY") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageZoomOffsetY = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 360, -360);
                }
                else if (CCstatus.CamSensor_id == OV3660_PID)
                {
                    CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 528, -528);
                }
                else if (CCstatus.CamSensor_id == OV5640_PID)
                {
                    CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 720, -720);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "CAMZOOMSIZE") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int _ImageZoomSize = std::stoi(splitted[1]);
                if (CCstatus.CamSensor_id == OV2640_PID)
                {
                    CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 29, 0);
                }
                else if (CCstatus.CamSensor_id == OV3660_PID)
                {
                    CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 43, 0);
                }
                else if (CCstatus.CamSensor_id == OV5640_PID)
                {
                    CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 59, 0);
                }
            }
        }

        else if ((toUpper(splitted[0]) == "LEDINTENSITY") && (splitted.size() > 1))
        {
            if (isStringNumeric(splitted[1]))
            {
                int ledintensity = std::stoi(splitted[1]);
                CCstatus.ImageLedIntensity = Camera.SetLEDIntensity(ledintensity);
            }
        }

        else if ((toUpper(splitted[0]) == "DEMO") && (splitted.size() > 1))
        {
            CCstatus.DemoMode = alphanumericToBoolean(splitted[1]);
            if (CCstatus.DemoMode == true)
            {
                Camera.useDemoMode();
            }
        }
    }

    Camera.setSensorDatenFromCCstatus(); // CCstatus >>> Kamera
    Camera.SetQualityZoomSize(CCstatus.ImageQuality, CCstatus.ImageFrameSize, CCstatus.ImageZoomEnabled, CCstatus.ImageZoomOffsetX, CCstatus.ImageZoomOffsetY, CCstatus.ImageZoomSize, CCstatus.ImageVflip);

    rawImage = new CImageBasis("rawImage");
    rawImage->CreateEmptyImage(CCstatus.ImageWidth, CCstatus.ImageHeight, 3);

    return true;
}

ClassFlowTakeImage::ClassFlowTakeImage(std::vector<ClassFlow *> *lfc) : ClassFlowImage(lfc, TAG)
{
    imagesLocation = "/log/source";
    imagesRetention = 5;
    SetInitialParameter();
}

string ClassFlowTakeImage::getHTMLSingleStep(string host)
{
    string result;
    result = "Raw Image: <br>\n<img src=\"" + host + "/img_tmp/raw.jpg\">\n";
    return result;
}

// called on every evaluation round
bool ClassFlowTakeImage::doFlow(string zwtime)
{
    psram_init_shared_memory_for_take_image_step();

    string logPath = CreateLogFolder(zwtime);

    int flash_duration = gLedAlwaysOn ? 0 : (int)(CCstatus.WaitBeforePicture * 1000);   // always-on LED: scene already lit, skip the settle wait

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("ClassFlowTakeImage::doFlow - Before takePictureWithFlash");
#endif

#ifdef WIFITURNOFF
    esp_wifi_stop(); // to save power usage and
#endif

    // if the camera settings were changed by creating a new reference image, they must be set again
    if (CFstatus.changedCameraSettings)
    {
        Camera.setSensorDatenFromCCstatus(); // CCstatus >>> Kamera
        Camera.SetQualityZoomSize(CCstatus.ImageQuality, CCstatus.ImageFrameSize, CCstatus.ImageZoomEnabled, CCstatus.ImageZoomOffsetX, CCstatus.ImageZoomOffsetY, CCstatus.ImageZoomSize, CCstatus.ImageVflip);
        Camera.LedIntensity = CCstatus.ImageLedIntensity;
        CFstatus.changedCameraSettings = false;
    }

    esp_err_t takeResult = takePictureWithFlash(flash_duration);

#ifdef WIFITURNOFF
    esp_wifi_start();
#endif

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("ClassFlowTakeImage::doFlow - After takePictureWithFlash");
#endif

    if (takeResult != ESP_OK)
    {
        // No usable image this round (e.g. the decode failed because the shared PSRAM region was too
        // small for this frame). The specific cause was already logged by CaptureToBasisImage /
        // LoadFromMemory. Release the shared region and report a failed round so the flow controller
        // skips the downstream steps and retries next round instead of working on a NULL image.
        // (A wedged camera, fb == NULL, already triggered a reboot inside CaptureToBasisImage.)
        psram_deinit_shared_memory_for_take_image_step();
        return false;
    }

    LogImage(logPath, "raw", NULL, NULL, zwtime, rawImage);

    RemoveOldLogs();

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("ClassFlowTakeImage::doFlow - After RemoveOldLogs");
#endif

    psram_deinit_shared_memory_for_take_image_step();

    return true;
}

esp_err_t ClassFlowTakeImage::SendRawJPG(httpd_req_t *req)
{
    int flash_duration = gLedAlwaysOn ? 0 : (int)(CCstatus.WaitBeforePicture * 1000);   // always-on LED: scene already lit, skip the settle wait
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return Camera.CaptureToHTTP(req, flash_duration);
}

ImageData *ClassFlowTakeImage::SendRawImage(void)
{
    CImageBasis *zw = new CImageBasis("SendRawImage", rawImage);
    ImageData *id;
    int flash_duration = gLedAlwaysOn ? 0 : (int)(CCstatus.WaitBeforePicture * 1000);   // always-on LED: scene already lit, skip the settle wait
    Camera.CaptureToBasisImage(zw, flash_duration);
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    id = zw->writeToMemoryAsJPG();
    delete zw;
    return id;
}

time_t ClassFlowTakeImage::getTimeImageTaken(void)
{
    return TimeImageTaken;
}

ClassFlowTakeImage::~ClassFlowTakeImage(void)
{
    delete rawImage;
}
