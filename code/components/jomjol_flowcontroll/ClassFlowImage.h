#pragma once

#ifndef CLASSFLOWIMAGE_H
#define CLASSFLOWIMAGE_H

#include "ClassFlow.h"

using namespace std;

class ClassFlowImage : public ClassFlow
{
protected:
	string imagesLocation;
    bool isLogImage;
    unsigned short imagesRetention;
	const char* logTag;

	string CreateLogFolder(string time);
	// conf: model confidence 0..1 of this sample, or < 0 = unknown. When known it is written into the
	// filename as "_cNN" (integer percent, clamped to 00..99) right after the label.
	void LogImage(string logPath, string name, float *resultFloat, int *resultInt, string time, CImageBasis *_img, float conf = -1);
	// The label token LogImage puts first in the filename ("3", "7.4", "N.N"; "" if no result).
	string FormatImageLabel(float *resultFloat, int *resultInt);


public:
	ClassFlowImage(const char* logTag);
	ClassFlowImage(std::vector<ClassFlow*> * lfc, const char* logTag);
	ClassFlowImage(std::vector<ClassFlow*> * lfc, ClassFlow *_prev, const char* logTag);
	
	void RemoveOldLogs();
};

#endif //CLASSFLOWIMAGE_H