#pragma once

#ifndef CLASSFLOWALIGNMENT_H
#define CLASSFLOWALIGNMENT_H

#include "ClassFlow.h"
#include "Helper.h"
#include "CAlignAndCutImage.h"
#include "CFindTemplate.h"

#include <string>
#include <vector>

using namespace std;

// A rectangle (full-frame capture coordinates) to blank out before analysis, reducing image
// complexity and spurious alignment matches in areas that are not part of the meter.
struct MaskRect {
    int x;
    int y;
    int w;
    int h;
};

class ClassFlowAlignment : public ClassFlow
{
protected:
    float initialrotate;
    bool initialflip;
    bool use_antialiasing;
    RefInfo References[2];
    int anz_ref;
    string namerawimage;
    bool SaveAllFiles;
    CAlignAndCutImage *AlignAndCutImage;
    std::string FileStoreRefAlignment;
    float SAD_criteria;

    // Periodic alignment: re-run the (expensive) reference-marker search only every Nth round and
    // reuse the cached transform in between. 1 = align every round (default, = legacy behaviour).
    int alignmentInterval;
    int alignmentCounter;
    bool haveCachedTransform;
    int cached_dx, cached_dy;
    float cached_winkel;

    // Crop: restrict analysis to a sub-rectangle of the capture. The raw frame is repacked to the
    // crop rectangle before alignment; all downstream coordinates (reference markers, ROIs) are
    // shifted by (-cropX, -cropY). Disabled when cropW/cropH are 0 (default = full frame).
    bool cropEnabled;
    int cropX, cropY, cropW, cropH;
    bool cropOffsetApplied;     // references are shifted into crop space exactly once
    // Mask: rectangles blanked (set to white) before alignment, in full-frame capture coordinates.
    std::vector<MaskRect> masks;

    void ApplyMaskAndCrop(void);

    void SetInitialParameter(void);
    bool LoadReferenceAlignmentValues(void);
    void SaveReferenceAlignmentValues();

public:
    CImageBasis *ImageBasis, *ImageTMP;
#ifdef ALGROI_LOAD_FROM_MEM_AS_JPG
    ImageData *AlgROI;
#endif

    ClassFlowAlignment(std::vector<ClassFlow *> *lfc);

    CAlignAndCutImage *GetAlignAndCutImage() { return AlignAndCutImage; };

    // Crop offset, consumed by the CNN flow to shift its ROI coordinates into crop space.
    bool IsCropEnabled() { return cropEnabled; };
    int GetCropOffsetX() { return cropEnabled ? cropX : 0; };
    int GetCropOffsetY() { return cropEnabled ? cropY : 0; };

    void DrawRef(CImageBasis *_zw);

    // True only when every configured reference-marker image exists and is large enough to be a real
    // JPEG. A truncated/half-written marker (e.g. corrupted by a collision with a running round)
    // decodes to garbage dimensions and crashes the template search, so the round must skip alignment
    // rather than panic when this returns false.
    bool referenceMarkersUsable();

    bool ReadParameter(FILE *pfile, string &aktparamgraph);
    bool doFlow(string time);
    string getHTMLSingleStep(string host);
    string name() { return "ClassFlowAlignment"; };
};

#endif // CLASSFLOWALIGNMENT_H
