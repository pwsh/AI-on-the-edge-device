#include "ClassFlowAlignment.h"
#include "ClassFlowTakeImage.h"
#include "ClassFlow.h"
#include "MainFlowControl.h"

#include "CRotateImage.h"
#include "esp_log.h"

#include "ClassLogFile.h"
#include "Helper.h"
#include "psram.h"

#include <sys/stat.h>
#include "../../include/defines.h"

#include <algorithm>    // std::min / std::max
#include <cstring>      // memmove
#include <cstdlib>      // strtol / strtof
#include <cerrno>       // errno / ERANGE
#include <cstdio>       // remove

static const char *TAG = "ALIGN";

// #define DEBUG_DETAIL_ON

void ClassFlowAlignment::SetInitialParameter(void)
{
    initialrotate = 0;
    anz_ref = 0;
    use_antialiasing = false;
    initialflip = false;
    SaveAllFiles = false;
    namerawimage = "/sdcard/img_tmp/raw.jpg";
    FileStoreRefAlignment = "/sdcard/config/align.txt";
    ListFlowControll = NULL;
    AlignAndCutImage = NULL;
    ImageBasis = NULL;
    ImageTMP = NULL;
#ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
    AlgROI = (ImageData *)malloc_psram_heap(std::string(TAG) + "->AlgROI", sizeof(ImageData), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
#endif
    previousElement = NULL;
    disabled = false;
    SAD_criteria = 0.05;
    alignmentInterval = 1;
    alignmentCounter = 0;
    haveCachedTransform = false;
    cached_dx = 0;
    cached_dy = 0;
    cached_winkel = 0.0f;
    cropEnabled = false;
    cropX = 0;
    cropY = 0;
    cropW = 0;
    cropH = 0;
    cropOffsetApplied = false;
    masks.clear();
}

ClassFlowAlignment::ClassFlowAlignment(std::vector<ClassFlow *> *lfc)
{
    SetInitialParameter();
    ListFlowControll = lfc;

    for (int i = 0; i < ListFlowControll->size(); ++i) {
        if (((*ListFlowControll)[i])->name().compare("ClassFlowTakeImage") == 0) {
            ImageBasis = ((ClassFlowTakeImage *)(*ListFlowControll)[i])->rawImage;
        }
    }

    // the function take pictures does not exist --> must be created first ONLY FOR TEST PURPOSES
    if (!ImageBasis)  {
        ESP_LOGD(TAG, "CImageBasis had to be created");
        ImageBasis = new CImageBasis("ImageBasis", namerawimage);
    }
}

bool ClassFlowAlignment::ReadParameter(FILE *pfile, string &aktparamgraph)
{
    std::vector<string> splitted;
    int suchex = 40;
    int suchey = 40;
    int alg_algo = 0; // default=0; 1 =HIGHACCURACY; 2= FAST; 3= OFF //add disable aligment algo |01.2023

    aktparamgraph = trim(aktparamgraph);

    if (aktparamgraph.size() == 0)
    {
        if (!this->GetNextParagraph(pfile, aktparamgraph)) {
            return false;
        }
    }

    if (aktparamgraph.compare("[Alignment]") != 0)
    {
        // Paragraph does not fit Alignment
        return false;
    }

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        splitted = ZerlegeZeile(aktparamgraph);

        if ((toUpper(splitted[0]) == "FLIPIMAGESIZE") && (splitted.size() > 1)) {
            initialflip = alphanumericToBoolean(splitted[1]);
        }
        else if (((toUpper(splitted[0]) == "initialrotate") || (toUpper(splitted[0]) == "INITIALROTATE")) && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                this->initialrotate = std::stod(splitted[1]);
            }
        }
        else if ((toUpper(splitted[0]) == "SEARCHFIELDX") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                suchex = std::stod(splitted[1]);
            }
        }
        else if ((toUpper(splitted[0]) == "SEARCHFIELDY") && (splitted.size() > 1)) {
            if (isStringNumeric(splitted[1])) {
                suchey = std::stod(splitted[1]);
            }
        }
        else if ((toUpper(splitted[0]) == "ANTIALIASING") && (splitted.size() > 1)) {
            use_antialiasing = alphanumericToBoolean(splitted[1]);
        }
        else if ((splitted.size() == 3) && (anz_ref < 2)) {
            if ((isStringNumeric(splitted[1])) && (isStringNumeric(splitted[2])))
            {
                References[anz_ref].image_file = FormatFileName("/sdcard" + splitted[0]);
                References[anz_ref].target_x = std::stod(splitted[1]);
                References[anz_ref].target_y = std::stod(splitted[2]);
                anz_ref++;
            }
            else
            {
                References[anz_ref].image_file = FormatFileName("/sdcard" + splitted[0]);
                References[anz_ref].target_x = 10;
                References[anz_ref].target_y = 10;
                anz_ref++;
            }
        }

        else if ((toUpper(splitted[0]) == "SAVEALLFILES") && (splitted.size() > 1)) {
            SaveAllFiles = alphanumericToBoolean(splitted[1]);
        }
        else if ((toUpper(splitted[0]) == "ALIGNMENTINTERVAL") && (splitted.size() > 1)) {
            // How many rounds between full reference-marker searches (>=1). Intermediate rounds
            // reuse the cached transform. 1 = align every round.
            if (isStringNumeric(splitted[1])) {
                alignmentInterval = std::stoi(splitted[1]);
                if (alignmentInterval < 1) {
                    alignmentInterval = 1;
                }
            }
        }
        else if ((toUpper(splitted[0]) == "CROP") && (splitted.size() > 4)) {
            // Crop <x> <y> <width> <height> (full-frame capture coordinates).
            if (isStringNumeric(splitted[1]) && isStringNumeric(splitted[2]) &&
                isStringNumeric(splitted[3]) && isStringNumeric(splitted[4])) {
                cropX = std::stoi(splitted[1]);
                cropY = std::stoi(splitted[2]);
                cropW = std::stoi(splitted[3]);
                cropH = std::stoi(splitted[4]);
                cropEnabled = (cropW > 0) && (cropH > 0);
            }
        }
        else if ((toUpper(splitted[0]) == "MASK") && (splitted.size() > 4)) {
            // Mask <x> <y> <width> <height> (full-frame capture coordinates); repeatable.
            if (isStringNumeric(splitted[1]) && isStringNumeric(splitted[2]) &&
                isStringNumeric(splitted[3]) && isStringNumeric(splitted[4])) {
                MaskRect m;
                m.x = std::stoi(splitted[1]);
                m.y = std::stoi(splitted[2]);
                m.w = std::stoi(splitted[3]);
                m.h = std::stoi(splitted[4]);
                if ((m.w > 0) && (m.h > 0)) {
                    masks.push_back(m);
                }
            }
        }
        else if ((toUpper(splitted[0]) == "ALIGNMENTALGO") && (splitted.size() > 1)) {
#ifdef DEBUG_DETAIL_ON
            std::string zw2 = "Alignment mode selected: " + splitted[1];
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, zw2);
#endif
            if (toUpper(splitted[1]) == "HIGHACCURACY") {
                alg_algo = 1;
            }
            if (toUpper(splitted[1]) == "FAST") {
                alg_algo = 2;
            }
            if (toUpper(splitted[1]) == "OFF") {
                // no align algo if set to 3 = off => no draw ref //add disable aligment algo |01.2023
                alg_algo = 3;
            }
        }
    }

    for (int i = 0; i < anz_ref; ++i) {
        References[i].search_x = suchex;
        References[i].search_y = suchey;
        References[i].fastalg_SAD_criteria = SAD_criteria;
        References[i].alignment_algo = alg_algo;
#ifdef DEBUG_DETAIL_ON
        std::string zw2 = "Alignment mode written: " + std::to_string(alg_algo);
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, zw2);
#endif
    }

    // no align algo if set to 3 = off => no draw ref //add disable aligment algo |01.2023
    if (References[0].alignment_algo != 3) {
        return LoadReferenceAlignmentValues();
    }

    return true;
}

string ClassFlowAlignment::getHTMLSingleStep(string host)
{
    string result;

    result = "<p>Rotated Image: </p> <p><img src=\"" + host + "/img_tmp/rot.jpg\"></p>\n";
    result = result + "<p>Found Alignment: </p> <p><img src=\"" + host + "/img_tmp/rot_roi.jpg\"></p>\n";
    result = result + "<p>Aligned Image: </p> <p><img src=\"" + host + "/img_tmp/alg.jpg\"></p>\n";
    return result;
}

void ClassFlowAlignment::ApplyMaskAndCrop(void)
{
    if (!ImageBasis || !ImageBasis->ImageOkay()) {
        return;
    }

    const int w = ImageBasis->width;
    const int h = ImageBasis->height;
    const int ch = ImageBasis->channels;
    uint8_t *img = ImageBasis->RGBImageLock();
    if (!img) {
        return;
    }

    // 1) Mask: blank configured rectangles (white, matching the alignment fill colour). Coordinates
    //    are full-frame, so this is applied before the crop repack.
    for (const MaskRect &m : masks) {
        int x0 = std::max(0, m.x);
        int y0 = std::max(0, m.y);
        int x1 = std::min(w, m.x + m.w);
        int y1 = std::min(h, m.y + m.h);
        for (int y = y0; y < y1; ++y) {
            uint8_t *row = img + (size_t)(y * w + x0) * ch;
            for (int x = x0; x < x1; ++x) {
                for (int c = 0; c < ch; ++c) {
                    *row++ = 255;
                }
            }
        }
    }

    // 2) Crop: repack the crop rectangle to the buffer origin and shrink the logical dimensions.
    //    The backing buffer stays full-size (re-filled by the next capture), so no realloc. Forward
    //    copy is safe: every destination offset is <= its source offset.
    if (cropEnabled) {
        int cx = std::max(0, cropX);
        int cy = std::max(0, cropY);
        int cw = std::min(cropW, w - cx);
        int chgt = std::min(cropH, h - cy);
        if ((cw > 0) && (chgt > 0)) {
            for (int y = 0; y < chgt; ++y) {
                uint8_t *dst = img + (size_t)(y * cw) * ch;
                uint8_t *src = img + (size_t)((y + cy) * w + cx) * ch;
                memmove(dst, src, (size_t)cw * ch);
            }
            ImageBasis->width = cw;
            ImageBasis->height = chgt;
        }
    }

    ImageBasis->RGBImageRelease();
}

bool ClassFlowAlignment::doFlow(string time)
{
    // Crop is incompatible with the initial flip (coordinate spaces differ); fall back to full frame.
    if (cropEnabled && initialflip) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Crop is not supported together with FlipImageSize - crop disabled");
        cropEnabled = false;
    }

    // Blank masked regions and repack the crop rectangle before any analysis of this frame.
    if (cropEnabled || !masks.empty()) {
        ApplyMaskAndCrop();
    }

    // Shift the reference-marker targets into crop space exactly once (downstream ROI coordinates
    // are shifted by the CNN flow via GetCropOffset*()).
    if (cropEnabled && !cropOffsetApplied) {
        for (int i = 0; i < anz_ref; ++i) {
            References[i].target_x -= cropX;
            References[i].target_y -= cropY;
        }
        cropOffsetApplied = true;
    }


#ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
    // AlgROI needs to be allocated before ImageTMP to avoid heap fragmentation
    if (!AlgROI)  {
        AlgROI = (ImageData *)heap_caps_realloc(AlgROI, sizeof(ImageData), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);

        if (!AlgROI) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't allocate AlgROI");
            LogFile.WriteHeapInfo("ClassFlowAlignment-doFlow");
        }
    }

    if (AlgROI) {
        ImageBasis->writeToMemoryAsJPG((ImageData *)AlgROI, 90);
    }
#endif

    if (!ImageTMP) {
        ImageTMP = new CImageBasis("tmpImage", ImageBasis); // Make sure the name does not get change, it is relevant for the PSRAM allocation!

        if (!ImageTMP) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't allocate tmpImage -> Exec this round aborted!");
            LogFile.WriteHeapInfo("ClassFlowAlignment-doFlow");
            return false;
        }
    }

    delete AlignAndCutImage;
    AlignAndCutImage = new CAlignAndCutImage("AlignAndCutImage", ImageBasis, ImageTMP);

    if (!AlignAndCutImage) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can't allocate AlignAndCutImage -> Exec this round aborted!");
        LogFile.WriteHeapInfo("ClassFlowAlignment-doFlow");
        return false;
    }

    CRotateImage rt("rawImage", AlignAndCutImage, ImageTMP, initialflip);

    if (initialflip) {
        int _zw = ImageBasis->height;
        ImageBasis->height = ImageBasis->width;
        ImageBasis->width = _zw;

        _zw = ImageTMP->width;
        ImageTMP->width = ImageTMP->height;
        ImageTMP->height = _zw;
    }

    if ((initialrotate != 0) || initialflip) {
        if (use_antialiasing) {
            rt.RotateAntiAliasing(initialrotate);
        }
        else {
            rt.Rotate(initialrotate);
        }

        if (SaveAllFiles) {
            AlignAndCutImage->SaveToFile(FormatFileName("/sdcard/img_tmp/rot.jpg"));
        }
    }

    // Guard against corrupt/missing reference-marker images: searching or drawing with a truncated
    // marker decodes garbage dimensions and panics. When unusable, skip alignment for this round
    // (best-effort: read ROIs off the rotated raw image) instead of crashing, and tell the user.
    bool refsUsable = (References[0].alignment_algo == 3) || referenceMarkersUsable();
    if (!refsUsable) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Alignment reference marker image(s) are missing or corrupt - "
            "skipping alignment this round. Re-create the reference image / alignment markers in the web UI.");
    }

    // no align algo if set to 3 = off //add disable aligment algo |01.2023
    if (refsUsable && References[0].alignment_algo != 3) {
        // Periodic alignment: run the full reference-marker search only every Nth round; on the
        // rounds in between, re-apply the cached transform (camera framing is stable between
        // captures). alignmentInterval == 1 keeps the legacy behaviour (search every round).
        bool doFullSearch = (!haveCachedTransform) || (alignmentInterval <= 1) ||
                            ((alignmentCounter % alignmentInterval) == 0);

        // Diagnostics: record whether this round ran a full marker search (the "align+" part of the
        // analysis type) and how many rounds until the next full search is due.
        setRoundAlignType(doFullSearch);

        if (doFullSearch) {
            // Align() returns whether both markers matched ABOVE the similarity threshold. A
            // sub-threshold match is NOT "markers absent" - FindTemplate still locates a best-fit
            // position, and the alignment is applied best-effort (this is the upstream behaviour).
            // So a marginal match (lighting / JPEG noise on a real installed meter) must not fail
            // the round; only persist the freshly found positions to the cache when it re-searched.
            if (!AlignAndCutImage->Align(&References[0], &References[1])) {
                SaveReferenceAlignmentValues();
            }
            cached_dx = AlignAndCutImage->out_dx;
            cached_dy = AlignAndCutImage->out_dy;
            cached_winkel = AlignAndCutImage->out_winkel;
            haveCachedTransform = true;
        }
        else {
            AlignAndCutImage->AlignByTransform(&References[0], cached_dx, cached_dy, cached_winkel);
        }
        alignmentCounter++;

        int aliMod = (alignmentInterval >= 1) ? (alignmentCounter % alignmentInterval) : 0;
        setRoundsUntilNextFullAlignment((aliMod == 0) ? 1 : (alignmentInterval - aliMod + 1));
    } // no align
    else {
        // Alignment disabled (algo == 3): never a full marker search.
        setRoundAlignType(false);
        setRoundsUntilNextFullAlignment(0);
    }

#ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
    if (AlgROI) {
        // no align algo if set to 3 = off => no draw ref //add disable aligment algo |01.2023
        // also skipped when the reference markers are unusable (DrawRef loads them -> would crash).
        if (refsUsable && References[0].alignment_algo != 3) {
            DrawRef(ImageTMP);
        }

        flowctrl.DigitDrawROI(ImageTMP);
        flowctrl.AnalogDrawROI(ImageTMP);
        ImageTMP->writeToMemoryAsJPG((ImageData *)AlgROI, 90);
    }
#endif

    if (SaveAllFiles) {
        AlignAndCutImage->SaveToFile(FormatFileName("/sdcard/img_tmp/alg.jpg"));
        ImageTMP->SaveToFile(FormatFileName("/sdcard/img_tmp/alg_roi.jpg"));
    }

    // must be deleted to have memory space for loading tflite
    delete ImageTMP;
    ImageTMP = NULL;

    // no align algo if set to 3 = off => no draw ref //add disable aligment algo |01.2023
    if (References[0].alignment_algo != 3) {
        // Best-effort alignment: the frame has already been aligned & cut above using the found
        // marker positions, and References[].fastalg_* are current in memory. Refreshing the
        // regenerable align.txt cache here is only an optimisation for the next round's fast-align,
        // so a missing/partial/corrupt cache must NOT fail the round - just leave the fast-align
        // un-primed (the next round falls back to a full marker search). Returning true keeps the
        // device reading the meter instead of skipping every round over a cache quirk.
        LoadReferenceAlignmentValues();
    }

    return true;
}

void ClassFlowAlignment::SaveReferenceAlignmentValues()
{
    FILE *pFile;
    std::string zwtime, zwvalue;

    pFile = fopen(FileStoreRefAlignment.c_str(), "w");

    if (strlen(zwtime.c_str()) == 0) {
        time_t rawtime;
        struct tm *timeinfo;
        char buffer[80];

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(buffer, 80, "%Y-%m-%dT%H:%M:%S", timeinfo);
        zwtime = std::string(buffer);
    }

    fputs(zwtime.c_str(), pFile);
    fputs("\n", pFile);

    zwvalue = std::to_string(References[0].fastalg_x) + "\t" + std::to_string(References[0].fastalg_y);
    zwvalue = zwvalue + "\t" + std::to_string(References[0].fastalg_SAD) + "\t" + std::to_string(References[0].fastalg_min);
    zwvalue = zwvalue + "\t" + std::to_string(References[0].fastalg_max) + "\t" + std::to_string(References[0].fastalg_avg);
    fputs(zwvalue.c_str(), pFile);
    fputs("\n", pFile);

    zwvalue = std::to_string(References[1].fastalg_x) + "\t" + std::to_string(References[1].fastalg_y);
    zwvalue = zwvalue + "\t" + std::to_string(References[1].fastalg_SAD) + "\t" + std::to_string(References[1].fastalg_min);
    zwvalue = zwvalue + "\t" + std::to_string(References[1].fastalg_max) + "\t" + std::to_string(References[1].fastalg_avg);
    fputs(zwvalue.c_str(), pFile);
    fputs("\n", pFile);

    fclose(pFile);
}

// Non-throwing replacements for stoi/stof on the align.txt cache. They mirror stoi/stof
// semantics exactly - parse a leading number and IGNORE any trailing characters - but never
// throw, so a corrupt token can't reach abort() (C++ exceptions are disabled). Only a token
// with no numeric prefix at all, or an out-of-range value, is treated as invalid. (An earlier
// version also rejected any trailing character, which was stricter than stoi/stof and wrongly
// flagged valid caches - including the firmware's own Save output - as "malformed".)
static bool alignParseInt(const std::string &s, int &out)
{
    if (s.empty()) return false;
    errno = 0;
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || errno == ERANGE) return false;   // no digits / overflow
    out = (int) v;
    return true;
}

static bool alignParseFloat(const std::string &s, float &out)
{
    if (s.empty()) return false;
    errno = 0;
    char *end = nullptr;
    float v = strtof(s.c_str(), &end);
    if (end == s.c_str() || errno == ERANGE) return false;   // no number / overflow
    out = v;
    return true;
}

bool ClassFlowAlignment::LoadReferenceAlignmentValues(void)
{
    FILE *pFile;
    char zw[1024];
    string zwvalue;
    std::vector<string> splitted;

    pFile = fopen(FileStoreRefAlignment.c_str(), "r");

    if (pFile == NULL) {
        return false;
    }

    // align.txt is a regenerable cache and may be stale, partially written or from an
    // older format. Parse it defensively: any missing / non-numeric / out-of-range field
    // discards the whole cache (delete + recompute) instead of letting stof/stoi throw,
    // which would abort() - C++ exceptions are disabled in this build (that was the cause
    // of a boot panic loop on a corrupt align.txt).
    bool ok = (fgets(zw, sizeof(zw), pFile) != NULL);   // 1st line: informational, skipped
    ESP_LOGD(TAG, "%s", zw);

    if (ok && fgets(zw, sizeof(zw), pFile)) {
        splitted = ZerlegeZeile(std::string(zw), " \t");
        ok = (splitted.size() >= 6) &&
             alignParseInt(splitted[0], References[0].fastalg_x) &&
             alignParseInt(splitted[1], References[0].fastalg_y) &&
             alignParseFloat(splitted[2], References[0].fastalg_SAD) &&
             alignParseInt(splitted[3], References[0].fastalg_min) &&
             alignParseInt(splitted[4], References[0].fastalg_max) &&
             alignParseFloat(splitted[5], References[0].fastalg_avg);
    } else {
        ok = false;
    }

    if (ok && fgets(zw, sizeof(zw), pFile)) {
        splitted = ZerlegeZeile(std::string(zw), " \t");
        ok = (splitted.size() >= 6) &&
             alignParseInt(splitted[0], References[1].fastalg_x) &&
             alignParseInt(splitted[1], References[1].fastalg_y) &&
             alignParseFloat(splitted[2], References[1].fastalg_SAD) &&
             alignParseInt(splitted[3], References[1].fastalg_min) &&
             alignParseInt(splitted[4], References[1].fastalg_max) &&
             alignParseFloat(splitted[5], References[1].fastalg_avg);
    } else {
        ok = false;
    }

    fclose(pFile);

    if (!ok) {
        // The regenerable fast-align cache is partial/corrupt. This is NOT fatal and NOT deleted:
        // the next full alignment's SaveReferenceAlignmentValues() overwrites it ("w"), and the
        // in-memory fastalg values set by Align() this round are already current. Just report the
        // cache as not-loaded so the caller knows the fast-align starting point isn't primed.
        ESP_LOGD(TAG, "align.txt incomplete/malformed - ignoring (will be rewritten by next alignment)");
        return false;
    }

    /*#ifdef DEBUG_DETAIL_ON
        std::string _zw = "\tLoadReferences[0]\tx,y:\t" + std::to_string(References[0].fastalg_x) + "\t" + std::to_string(References[0].fastalg_x);
        _zw = _zw + "\tSAD, min, max, avg:\t" + std::to_string(References[0].fastalg_SAD) + "\t" + std::to_string(References[0].fastalg_min);
        _zw = _zw + "\t" + std::to_string(References[0].fastalg_max) + "\t" + std::to_string(References[0].fastalg_avg);
        LogFile.WriteToDedicatedFile("/sdcard/alignment.txt", _zw);
        _zw = "\tLoadReferences[1]\tx,y:\t" + std::to_string(References[1].fastalg_x) + "\t" + std::to_string(References[1].fastalg_x);
        _zw = _zw + "\tSAD, min, max, avg:\t" + std::to_string(References[1].fastalg_SAD) + "\t" + std::to_string(References[1].fastalg_min);
        _zw = _zw + "\t" + std::to_string(References[1].fastalg_max) + "\t" + std::to_string(References[1].fastalg_avg);
        LogFile.WriteToDedicatedFile("/sdcard/alignment.txt", _zw);
    #endif*/

    return true;
}

bool ClassFlowAlignment::referenceMarkersUsable()
{
    // A real reference-marker JPEG is well over a few hundred bytes; a missing file or a truncated
    // one (e.g. corrupted by a write that collided with a running round) is far smaller and decodes
    // to garbage dimensions that crash the template search. Reject anything implausibly small.
    const long minBytes = 256;
    int count = (anz_ref > 0) ? anz_ref : 2;
    for (int i = 0; i < count && i < 2; ++i) {
        struct stat st;
        if (stat(References[i].image_file.c_str(), &st) != 0 || st.st_size < minBytes) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Reference marker '" + References[i].image_file +
                "' missing or too small (" + (stat(References[i].image_file.c_str(), &st) == 0 ?
                std::to_string((long)st.st_size) + " bytes" : "missing") + ")");
            return false;
        }
    }
    return true;
}

void ClassFlowAlignment::DrawRef(CImageBasis *_zw)
{
    if (_zw->ImageOkay()) {
        _zw->drawRect(References[0].target_x, References[0].target_y, References[0].width, References[0].height, 255, 0, 0, 2);
        _zw->drawRect(References[1].target_x, References[1].target_y, References[1].width, References[1].height, 255, 0, 0, 2);
    }
}
