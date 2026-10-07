#pragma once

#ifndef CLASSFFLOWPOSTPROCESSING_H
#define CLASSFFLOWPOSTPROCESSING_H

#include "ClassFlow.h"
#include "ClassFlowTakeImage.h"
#include "ClassFlowCNNGeneral.h"
#include "ClassFlowDefineTypes.h"

#include <string>

class ClassFlowPostProcessing :
    public ClassFlow
{
protected:
    bool UpdatePreValueINI;

    int PreValueAgeStartup; 
    bool ErrorMessage;
	
    ClassFlowCNNGeneral* flowAnalog;
    ClassFlowCNNGeneral* flowDigit;    

    string FilePreValue;

    ClassFlowTakeImage *flowTakeImage;

    bool LoadPreValue(void);
    string ShiftDecimal(string in, int _decShift);

    string ErsetzteN(string, double _prevalue);
    double checkDigitConsistency(double input, int _decilamshift, bool _isanalog, double _preValue);

    void InitNUMBERS();
	
    void handleDecimalSeparator(string _decsep, string _value);
    void handleMaxRateValue(string _decsep, string _value);
    void handleDecimalExtendedResolution(string _decsep, string _value); 
    void handleMaxRateType(string _decsep, string _value);
    void handleAnalogToDigitTransitionStart(string _decsep, string _value);
    void handleAllowNegativeRate(string _decsep, string _value);
    void handleIgnoreLeadingNaN(string _decsep, string _value);
    void handleChangeRateThreshold(string _decsep, string _value);
    void handlecheckDigitIncreaseConsistency(std::string _decsep, std::string _value);
    // Physics-bounded predictive reading: set one field of a sequence's PhysicalLimits. `_key` is the
    // already-upper-cased parameter name; `_decsep` carries the optional `<NUMBER>.` prefix.
    void handlePredictiveLimit(const std::string& _key, const std::string& _decsep, const std::string& _value);
    void handleLeakDetection(std::string _decsep, std::string _value);
    void handleLeakThreshold(std::string _decsep, std::string _value);
    // Compute the per-digit-ROI read plan for sequence j and mark roi::predictiveSkipNext so the
    // next round's digit CNN can skip provably-static digits. No-op unless PredictiveRead is enabled
    // for the digit flow and a Utility model is configured for the sequence.
    void UpdatePredictiveReadPlan(int j);
    // Resolve each sequence's PhysicalLimits.unitsPerValue (SI units per displayed unit) from the
    // [MQTT] MeterType when no explicit UnitsPerValue was configured, and log the effective physics
    // ceiling. Runs once after ALL config sections are parsed (first doFlow after a (re)load).
    void ResolvePhysicsUnits();
    bool PhysUnitsResolved;

    void WriteDataLog(int _index);

public:
    bool PreValueUse;
    int ConfidenceVotes;        // §10 confidence vote: N consecutive confirming lower reads override a
                                // suspected-high PreValue. 0 = off (legacy negative-rate rejection only).
    std::vector<NumberPost*> NUMBERS;

    ClassFlowPostProcessing(std::vector<ClassFlow*>* lfc, ClassFlowCNNGeneral *_analog, ClassFlowCNNGeneral *_digit);
    virtual ~ClassFlowPostProcessing(){};
    bool ReadParameter(FILE* pfile, string& aktparamgraph);
    bool doFlow(string time);
    string getReadout(int _number);
    string getReadoutParam(bool _rawValue, bool _noerror, int _number = 0);
    string getReadoutError(int _number = 0);
    string getReadoutRate(int _number = 0);
    string getReadoutTimeStamp(int _number = 0);
    void SavePreValue();
    string getJsonFromNumber(int i, std::string _lineend);
    string GetPreValue(std::string _number = "");
    bool SetPreValue(double zw, string _numbers, bool _extern = false);

    std::string GetJSON(std::string _lineend = "\n");
    std::string getNumbersName();

    void UpdateNachkommaDecimalShift();

    std::vector<NumberPost*>* GetNumbers(){return &NUMBERS;};

    string name(){return "ClassFlowPostProcessing";};
};

#endif //CLASSFFLOWPOSTPROCESSING_H
