#pragma once

#ifndef HELPER_H
#define HELPER_H

#include <string>
#include <fstream>
#include <vector>

#include "sdmmc_cmd.h"

using namespace std;

std::string FormatFileName(std::string input);
std::size_t file_size(const std::string& file_name);
void FindReplace(std::string& line, std::string& oldString, std::string& newString);

bool CopyFile(string input, string output);
bool DeleteFile(const string& filename);
bool RenameFile(const string& from, const string& to);
bool RenameFolder(const string& from, const string& to);
bool MakeDir(const std::string& _what);
bool FileExists(const string& filename);
bool FolderExists(const string& foldername);

string RundeOutput(double _in, int _anzNachkomma);

size_t findDelimiterPos(const string& input, const string& delimiter);
//string trim(string istring);
string trim(string istring, string adddelimiter = "");
bool ctype_space(const char c, string adddelimiter);

string getFileType(const string& filename);
string getFileFullFileName(const string& filename);
string getDirectory(const string& filename);

int mkdir_r(const char *dir, const mode_t mode);
int removeFolder(const char* folderPath, const char* logTag);

string toLower(string in);
string toUpper(string in);

float temperatureRead();

std::string intToHexString(int _valueInt);
time_t addDays(time_t startTime, int days);

void memCopyGen(uint8_t* _source, uint8_t* _target, int _size);

std::vector<string> HelperZerlegeZeile(std::string input, std::string _delimiter);
std::vector<std::string> ZerlegeZeile(std::string input, std::string delimiter = " =, \t");

///////////////////////////
size_t getInternalESPHeapSize();
size_t getESPHeapSize();
string getESPHeapInfo();

/////////////////////////////
string getSDCardPartitionSize();
string getSDCardFreePartitionSpace();
string getSDCardPartitionAllocationSize();

void SaveSDCardInfo(sdmmc_card_t* card);
string SDCardParseManufacturerIDs(int);
string getSDCardManufacturer();
string getSDCardName();
string getSDCardCapacity();
string getSDCardSectorSize();

string getMac(void);

/* Error bit fields
   One bit per error
   Make sure it matches https://jomjol.github.io/AI-on-the-edge-device-docs/Error-Codes */
enum SystemStatusFlag_t {          // One bit per error
    // First Byte
    SYSTEM_STATUS_PSRAM_BAD         = 1 << 0, //  1, Critical Error
    SYSTEM_STATUS_HEAP_TOO_SMALL    = 1 << 1, //  2, Critical Error
    SYSTEM_STATUS_CAM_BAD           = 1 << 2, //  4, Critical Error
    SYSTEM_STATUS_SDCARD_CHECK_BAD  = 1 << 3, //  8, Critical Error
    SYSTEM_STATUS_FOLDER_CHECK_BAD  = 1 << 4, //  16, Critical Error

    // Second Byte
    SYSTEM_STATUS_CAM_FB_BAD        = 1 << (0+8), //  8, Flow still might work
    SYSTEM_STATUS_NTP_BAD           = 1 << (1+8), //  9, Flow will work but time will be wrong
};

void setSystemStatusFlag(SystemStatusFlag_t flag);
void clearSystemStatusFlag(SystemStatusFlag_t flag);
int getSystemStatus(void);
bool isSetSystemStatusFlag(SystemStatusFlag_t flag);

time_t getUpTime(void);
string getResetReason(void);
std::string getFormatedUptime(bool compact);

// Last full digitization round (loop) processing time in milliseconds - a performance diagnostic.
// Set by the flow task after each round; read by the MQTT / Home Assistant / InfluxDB / JSON layers.
void setFlowProcessingTime(long ms);
long getFlowProcessingTime(void);

// Per-round analysis diagnostics, shared so every output layer (MQTT / Home Assistant / InfluxDB /
// JSON / web overview) can read them without depending on the flow-control component.
// The "analysis type" describes what work a round actually did, combining the alignment decision
// (full marker search vs reuse of the cached transform) with the read decision (full re-read of every
// digit vs FastRead, where unchanged digits are skipped). Values: "full", "align+full", "fast-read",
// "align+fast-read".
void setRoundAlignType(bool fullSearch);                       // ClassFlowAlignment, per round
void setRoundReadType(bool fastRead, int digitsAnalyzed, int digitsTotal); // ClassFlowCNNGeneral, per round
std::string getLastAnalysisType(void);                         // e.g. "align+fast-read"
int getLastDigitsAnalyzed(void);                               // digits actually inferred last round
int getLastDigitsTotal(void);                                  // total digits last round
void setLastAnalysisCompleted(time_t epoch);                   // round end
time_t getLastAnalysisCompletedEpoch(void);                    // 0 if none yet
// "Next full" scheduling estimates. roundsUntil: 1 == the very next round is a full one.
// epoch: best-effort wall-clock estimate (0 when unknown, e.g. clock not set).
void setRoundsUntilNextFullAlignment(int rounds);
void setRoundsUntilNextFullRead(int rounds);
int getRoundsUntilNextFullAlignment(void);
int getRoundsUntilNextFullRead(void);
void setNextFullAlignmentEpoch(time_t epoch);
void setNextFullReadEpoch(time_t epoch);
time_t getNextFullAlignmentEpoch(void);
time_t getNextFullReadEpoch(void);

const char* get404(void);

std::string UrlDecode(const std::string& value);

void replaceAll(std::string& s, const std::string& toReplace, const std::string& replaceWith);
bool replaceString(std::string& s, std::string const& toReplace, std::string const& replaceWith);
bool replaceString(std::string& s, std::string const& toReplace, std::string const& replaceWith, bool logIt);
bool isInString(std::string& s, std::string const& toFind);

bool isStringNumeric(std::string &input);
bool isStringAlphabetic(std::string &input);
bool isStringAlphanumeric(std::string &input);
bool alphanumericToBoolean(std::string &input);

int clipInt(int input, int high, int low);
bool numericStrToBool(const std::string& input);
bool stringToBoolean(const std::string& input);

// ---------------------------------------------------------------------------
// Status-LED processing stages.
// Decoupled hook so the flow (jomjol_flowcontroll) can signal the current
// processing stage without depending on jomjol_controlGPIO (which would be a
// circular dependency). jomjol_controlGPIO registers a callback that drives
// the addressable (WS281x) status LED with the per-stage colour.
// ---------------------------------------------------------------------------
enum ProcessingStage {
    PROC_STAGE_IDLE      = 0,   // between rounds / flow finished
    PROC_STAGE_TAKEIMAGE = 1,   // capturing image
    PROC_STAGE_ALIGN     = 2,   // aligning
    PROC_STAGE_DIGITIZE  = 3,   // digit / analog CNN
    PROC_STAGE_POSTPROC  = 4,   // post-processing
    PROC_STAGE_TRANSMIT  = 5,   // sending (MQTT / InfluxDB / Webhook)
    PROC_STAGE_ERROR     = 6,   // error / retry
    PROC_STAGE_COUNT     = 7
};

typedef void (*tStatusLedStageCb)(int stage);
void registerStatusLedStageCallback(tStatusLedStageCb cb);
void setProcessingStage(int stage);   // no-op if no callback registered

// "Always-on LED" master flag (set from the [GPIO] LEDAlwaysOn config by the GPIO handler). When true the
// external LED is kept constantly lit (overriding the per-stage status colours) and the camera's
// pre-capture flash/AEC-settle wait is skipped (the scene is already lit). Lives here so both the GPIO
// handler (writer) and ClassFlowTakeImage (reader) can see it without a cross-component dependency.
extern bool gLedAlwaysOn;

#endif //HELPER_H
