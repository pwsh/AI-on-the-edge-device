#pragma once

#ifndef CTFLITECLASS_H
#define CTFLITECLASS_H

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "esp_err.h"
#include "esp_log.h"

#include "CImageBasis.h"


class CTfLiteClass
{
    protected:
        tflite::MicroMutableOpResolver<10> resolver;  
        const tflite::Model* model;
        tflite::MicroInterpreter* interpreter;
        TfLiteTensor* output = nullptr;     

        int kTensorArenaSize;
        uint8_t *tensor_arena;

        unsigned char *modelfile = NULL;


        float* input;
        int input_i;
        int im_height, im_width, im_channel;

        long GetFileSize(const std::string& filename);
        bool ReadFileToModel(std::string _fn);
        void MakeStaticResolver();

    public:
        CTfLiteClass();
        ~CTfLiteClass();        
        bool LoadModel(std::string _fn);
        bool MakeAllocate();
        void GetInputTensorSize();
        bool LoadInputImageBasis(CImageBasis *rs);
        void Invoke();
        int GetAnzOutPut(bool silent = true);        
        int GetOutClassification(int _von = -1, int _bis = -1);

        int GetClassFromImageBasis(CImageBasis *rs);
        // As above, but also returns the winning class's confidence (its output-neuron value,
        // normalised by the output sum -> ~softmax probability in [0,1]) via outConfidence.
        int GetClassFromImageBasis(CImageBasis *rs, float *outConfidence);
        // Argmax over the current output tensor + winning-class confidence (no Invoke).
        int GetClassAndConfidence(float *outConfidence);
        // As above, plus the log-ratio of winner to runner-up (~logit margin). The margin keeps
        // ranking candidates after the softmax confidence saturates at 1.0 (ROI auto-tune).
        int GetClassAndConfidence(float *outConfidence, float *outMargin);
        std::string GetStatusFlow();

        float GetOutputValue(int nr);
        void GetInputDimension(bool silent);
        int ReadInputDimenstion(int _dim);
};

#endif //CTFLITECLASS_H