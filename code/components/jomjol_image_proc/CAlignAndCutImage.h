#pragma once

#ifndef CALIGNANDCUTIMAGE_H
#define CALIGNANDCUTIMAGE_H

#include "CImageBasis.h"
#include "CFindTemplate.h"


class CAlignAndCutImage : public CImageBasis
{
    public:
        int t0_dx, t0_dy, t1_dx, t1_dy;
        // Last computed alignment transform (set by Align()); reused by periodic alignment to
        // skip the (expensive) CFindTemplate marker search on intermediate rounds.
        int out_dx = 0, out_dy = 0;
        float out_winkel = 0.0f;
        CImageBasis *ImageTMP;
        CAlignAndCutImage(std::string name, std::string _image) : CImageBasis(name, _image) {ImageTMP = NULL;};
        CAlignAndCutImage(std::string name, uint8_t* _rgb_image, int _channels, int _width, int _height, int _bpp) : CImageBasis(name, _rgb_image, _channels, _width, _height, _bpp) {ImageTMP = NULL;};
        CAlignAndCutImage(std::string name, CImageBasis *_org, CImageBasis *_temp);

        bool Align(RefInfo *_temp1, RefInfo *_temp2);
        // Apply a previously computed transform without re-searching the reference markers.
        // If |winkel| is ~0 the full-frame rotate is skipped (translation only).
        void AlignByTransform(RefInfo *_temp1, int dx, int dy, float winkel);
//        void Align(std::string _template1, int x1, int y1, std::string _template2, int x2, int y2, int deltax = 40, int deltay = 40, std::string imageROI = "");
        void CutAndSave(std::string _template1, int x1, int y1, int dx, int dy);
        CImageBasis* CutAndSave(int x1, int y1, int dx, int dy);
        void CutAndSave(int x1, int y1, int dx, int dy, CImageBasis *_target);
        void GetRefSize(int *ref_dx, int *ref_dy);
};

#endif //CALIGNANDCUTIMAGE_H