#include "CAlignAndCutImage.h"
#include "CRotateImage.h"
#include "ClassLogFile.h"

#include <math.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include "psram.h"
#include "../../include/defines.h"

static const char* TAG = "c_align_and_cut_image";

CAlignAndCutImage::CAlignAndCutImage(std::string _name, CImageBasis *_org, CImageBasis *_temp) : CImageBasis(_name)
{
    name = _name;
    rgb_image = _org->rgb_image;
    channels = _org->channels;
    width = _org->width;
    height = _org->height;
    bpp = _org->bpp;
    externalImage = true;   

    islocked = false; 

    ImageTMP = _temp;
}

void CAlignAndCutImage::GetRefSize(int *ref_dx, int *ref_dy)
{
    ref_dx[0] = t0_dx;
    ref_dy[0] = t0_dy;
    ref_dx[1] = t1_dx;
    ref_dy[1] = t1_dy;
}

bool CAlignAndCutImage::Align(RefInfo *_temp1, RefInfo *_temp2)
{
    int dx, dy;
    bool isSimilar1, isSimilar2;

    CFindTemplate* ft = new CFindTemplate("align", rgb_image, channels, width, height, bpp);

    ESP_LOGD(TAG, "Before ft->FindTemplate(_temp1); %s", _temp1->image_file.c_str());
    isSimilar1 = ft->FindTemplate(_temp1);
    _temp1->width = ft->tpl_width;
    _temp1->height = ft->tpl_height;

    ESP_LOGD(TAG, "Before ft->FindTemplate(_temp2); %s", _temp2->image_file.c_str());
    isSimilar2 = ft->FindTemplate(_temp2);
    _temp2->width = ft->tpl_width;
    _temp2->height = ft->tpl_height;

    delete ft;

    // A marker pinned to the edge of its search window is an unreliable (clamped) match: the true
    // minimum lies outside the window - typically a false match on a low-detail feature (e.g. a
    // plain horizontal line, which is ambiguous in y) or one washed out by glare. Deriving the
    // transform from it yields a large bogus translation/rotation that skews the whole frame.
    bool rel1 = (abs(_temp1->found_x - _temp1->target_x) < _temp1->search_x) &&
                (abs(_temp1->found_y - _temp1->target_y) < _temp1->search_y);
    bool rel2 = (abs(_temp2->found_x - _temp2->target_x) < _temp2->search_x) &&
                (abs(_temp2->found_y - _temp2->target_y) < _temp2->search_y);

    // Translation: derive from a reliable marker (prefer the first). If neither is reliable, leave
    // the frame unshifted rather than jump to a bogus position.
    if (rel1) {
        dx = _temp1->target_x - _temp1->found_x;
        dy = _temp1->target_y - _temp1->found_y;
    }
    else if (rel2) {
        dx = _temp2->target_x - _temp2->found_x;
        dy = _temp2->target_y - _temp2->found_y;
    }
    else {
        dx = 0;
        dy = 0;
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Alignment: both reference markers matched at their search-window edge (unreliable) - leaving this frame unshifted/unrotated");
    }

    // Rotation needs both markers and is the destructive part when wrong; only trust it when BOTH
    // markers matched confidently (neither clamped to its search edge).
    float d_winkel = 0;
    if (rel1 && rel2) {
        float w_org = atan2(_temp2->found_y - _temp1->found_y, _temp2->found_x - _temp1->found_x);
        float w_ist = atan2(_temp2->target_y - _temp1->target_y, _temp2->target_x - _temp1->target_x);
        d_winkel = (w_ist - w_org) * 180 / M_PI;
    }
    else if (rel1 != rel2) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Alignment: one reference marker is an unreliable (edge-clamped) match - applying translation only, skipping rotation");
    }

    // Remember the computed transform so periodic alignment can re-apply it on rounds where the
    // marker search is skipped (see ClassFlowAlignment / AlignByTransform).
    out_dx = dx;
    out_dy = dy;
    out_winkel = d_winkel;

    CRotateImage rt("Align", this, ImageTMP);
    rt.Translate(dx, dy);
    // Skip the full-frame rotate when there is effectively no rotation (rigidly mounted camera):
    // it is a no-op that still touches every pixel. Translation alone then aligns the frame.
    if (fabs(d_winkel) >= ALIGNMENT_ROTATION_DEADBAND_DEG) {
        rt.Rotate(d_winkel, _temp1->target_x, _temp1->target_y);
    }
    ESP_LOGD(TAG, "Alignment: dx %d - dy %d - rot %f (rel1=%d rel2=%d)", dx, dy, d_winkel, rel1, rel2);

    return (isSimilar1 && isSimilar2);
}


void CAlignAndCutImage::AlignByTransform(RefInfo *_temp1, int dx, int dy, float winkel)
{
    // Re-apply a cached transform WITHOUT searching the reference markers (CFindTemplate). Used by
    // periodic alignment: the camera framing is stable between captures, so the offset/angle found
    // on the last full alignment still holds for the fresh raw frame.
    out_dx = dx;
    out_dy = dy;
    out_winkel = winkel;

    CRotateImage rt("Align", this, ImageTMP);
    rt.Translate(dx, dy);
    if (fabs(winkel) >= ALIGNMENT_ROTATION_DEADBAND_DEG) {
        rt.Rotate(winkel, _temp1->target_x, _temp1->target_y);
    }
    ESP_LOGD(TAG, "Alignment (cached): dx %d - dy %d - rot %f", dx, dy, winkel);
}





void CAlignAndCutImage::CutAndSave(std::string _template1, int x1, int y1, int dx, int dy)
{

    int x2, y2;

    x2 = x1 + dx;
    y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    // Guard against an unloaded/empty source image or a degenerate crop box. Without this, a NULL
    // rgb_image (e.g. the JPEG failed to decode) or a non-positive size makes the copy loop below
    // dereference out of bounds and panics the calling task (seen crashing the httpd task). Leaving
    // the output file unwritten lets callers detect the failure via the missing/unloadable result.
    if (rgb_image == NULL || width <= 0 || height <= 0 || x1 < 0 || y1 < 0 || dx <= 0 || dy <= 0) {
        return;
    }

    int memsize = dx * dy * channels;
    uint8_t* odata = (unsigned char*) malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);
    if (odata == NULL) {
        return;
    }

    stbi_uc* p_target;
    stbi_uc* p_source;

    RGBImageLock();

    for (int x = x1; x < x2; ++x)
        for (int y = y1; y < y2; ++y)
        {
            p_target = odata + (channels * ((y - y1) * dx + (x - x1)));
            p_source = rgb_image + (channels * (y * width + x));
            for (int _channels = 0; _channels < channels; ++_channels)
                p_target[_channels] = p_source[_channels];
        }

#ifdef STBI_ONLY_JPEG
    stbi_write_jpg(_template1.c_str(), dx, dy, channels, odata, 100);
#else
    stbi_write_bmp(_template1.c_str(), dx, dy, channels, odata);
#endif
    

    RGBImageRelease();

    stbi_image_free(odata);
}

void CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy, CImageBasis *_target)
{
    int x2, y2;

    x2 = x1 + dx;
    y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    if ((_target->height != dy) || (_target->width != dx) || (_target->channels != channels))
    {
        ESP_LOGD(TAG, "CAlignAndCutImage::CutAndSave - Image size does not match!");
        return;
    }

    uint8_t* odata = _target->RGBImageLock();
    RGBImageLock();

    // Each source row segment [x1..x2) is contiguous, as is the matching destination row, so copy
    // the ROI a row at a time instead of per byte.
    const size_t rowbytes = (size_t)dx * channels;
    for (int y = y1; y < y2; ++y)
    {
        memcpy(odata + (size_t)channels * ((y - y1) * dx),
               rgb_image + (size_t)channels * ((size_t)y * width + x1),
               rowbytes);
    }

    RGBImageRelease();
    _target->RGBImageRelease();
}


CImageBasis* CAlignAndCutImage::CutAndSave(int x1, int y1, int dx, int dy)
{
    int x2, y2;

    x2 = x1 + dx;
    y2 = y1 + dy;
    x2 = std::min(x2, width - 1);
    y2 = std::min(y2, height - 1);

    dx = x2 - x1;
    dy = y2 - y1;

    int memsize = dx * dy * channels;
    uint8_t* odata = (unsigned char*)malloc_psram_heap(std::string(TAG) + "->odata", memsize, MALLOC_CAP_SPIRAM);

    stbi_uc* p_target;
    stbi_uc* p_source;

    RGBImageLock();

    for (int x = x1; x < x2; ++x)
        for (int y = y1; y < y2; ++y)
        {
            p_target = odata + (channels * ((y - y1) * dx + (x - x1)));
            p_source = rgb_image + (channels * (y * width + x));
            for (int _channels = 0; _channels < channels; ++_channels)
                p_target[_channels] = p_source[_channels];
        }

    CImageBasis* rs = new CImageBasis("CutAndSave", odata, channels, dx, dy, bpp);
    RGBImageRelease();
    rs->SetIndepended();
    return rs;
}
