#pragma once

#ifndef CLASSFLOWCNNGENERAL_H
#define CLASSFLOWCNNGENERAL_H

#include"ClassFlowDefineTypes.h"
#include "ClassFlowAlignment.h"

class CTfLiteClass;

enum t_CNNType {
    AutoDetect,
    Analogue,
    Analogue100,
    Digit,
    DigitHyprid10,
    DoubleHyprid10,
    Digit100,
    None
 };

class ClassFlowCNNGeneral :
    public ClassFlowImage
{
protected:
    t_CNNType CNNType;
    std::vector<general*> GENERAL;
    float CNNGoodThreshold;

    string cnnmodelfile;
    int modelxsize, modelysize, modelchannel;
    bool isLogImageSelect;
    string LogImageSelect;
    ClassFlowAlignment* flowpostalignment;

    bool SaveAllFiles;

    // --- FastRead: only re-run CNN inference on digit ROIs whose image changed ---
    bool FastReadEnabled;        // master switch (config "FastRead"); default off -> original behavior
    int FastReadDiffThreshold;   // mean abs per-pixel diff (0-255) below which a ROI counts as "unchanged"
    int FastReadFullInterval;    // force a full inference of all digits every N cycles (drift backstop)
    int fastReadCycle;           // cycle counter for the interval backstop
    bool forceFullEval;          // one-shot external trigger (e.g. carry / consistency failure)
    CTfLiteClass *residentTflite;// model kept loaded across cycles while FastRead is on

    // --- PredictiveRead: proactively skip digit ROIs that physics + the carry chain prove cannot
    //     have changed (PostProcessing sets roi::predictiveSkipNext each round). Opt-in; relies on
    //     the FastRead cache to supply the reused value. ---
    bool PredictiveReadEnabled = false; // master switch (config "PredictiveRead"); default off

    // --- Unknown-digit resolution + per-digit confident-read matrix ---
    bool  ResolveUnknownEnabled = false;          // config "ResolveUnknownDigits"; default off
    float DigitHistoryConfidenceFloor = 0.70f;    // min normalised confidence to enter the matrix
    // True if the less-significant neighbour of digit roi (index i+1) looks unchanged vs its history.
    bool  digitLowerNeighborStable(int _seq, int i);

public:
    // Append this flow's per-digit confident-read matrix as JSON object members ("seqname":[ ... ]).
    // Only emits for digit-class flows (Digit). Caller wraps the members in { }.
    void AppendDigitMatrixJson(std::string &json);

    // On-demand single-ROI examine: load the already-cut ROI image at cutOrgPath, resize it to this
    // flow's model input (saving that analysed image to displayPath), run the CNN, and return a JSON
    // fragment: "reading":"<digit/value/N>","confidence":<percent|null>  (no surrounding braces).
    std::string ExamineCut(const std::string &cutOrgPath, const std::string &displayPath, bool ccw);
protected:

    bool isDigitalCNN();                       // true for Digit / Digit100
    int  fastReadMeanDiff(roi *r);             // mean abs diff of current cut vs cached buffer
    void fastReadUpdateCache(roi *r, int klasse, float value); // copy current cut + store result

    int PointerEvalAnalogNew(float zahl, int numeral_preceder);
    int PointerEvalAnalogToDigitNew(float zahl, float numeral_preceder,  int eval_predecessors, float AnalogToDigitTransitionStart);
    int PointerEvalHybridNew(float zahl, float number_of_predecessors, int eval_predecessors, bool Analog_Predecessors = false, float AnalogToDigitTransitionStart=9.2);



    bool doNeuralNetwork(string time); 
    bool doAlignAndCut(string time);

    bool getNetworkParameter();

public:
    ClassFlowCNNGeneral(ClassFlowAlignment *_flowalign, t_CNNType _cnntype = AutoDetect);
    ~ClassFlowCNNGeneral();

    // Force the next inference pass to re-read every digit (skip the FastRead cache).
    // Intended to be called by post-processing on carry / consistency failure.
    void TriggerFullEval() { forceFullEval = true; };

    // True when the opt-in predictive-read gate is enabled (PostProcessing checks this before
    // computing a per-ROI read plan).
    bool IsPredictiveReadEnabled() const { return PredictiveReadEnabled; };

    bool ReadParameter(FILE* pfile, string& aktparamgraph);
    bool doFlow(string time);

    string getHTMLSingleStep(string host);
    string getReadout(int _analog, bool _extendedResolution = false, int prev = -1, float _before_narrow_Analog = -1, float AnalogToDigitTransitionStart=9.2); 

    string getReadoutRawString(int _analog);  

    void DrawROI(CImageBasis *_zw);

    // Shift every ROI position into crop space. Called once after config load when the alignment
    // crop is enabled, so cut + draw coordinates match the repacked (cropped) frame.
    void ShiftROIs(int dx, int dy);

   	std::vector<HTMLInfo*> GetHTMLInfo();   

    int getNumberGENERAL();
    general* GetGENERAL(int _analog);
    general* GetGENERAL(string _name, bool _create);
    general* FindGENERAL(string _name_number);    
    string getNameGENERAL(int _analog);    

    bool isExtendedResolution(int _number = 0);

    void UpdateNameNumbers(std::vector<std::string> *_name_numbers);

    t_CNNType getCNNType(){return CNNType;};

    string name(){return "ClassFlowCNNGeneral";}; 
};

#endif

