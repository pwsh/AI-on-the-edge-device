#pragma once

#ifndef CLASSLOGFILE_H
#define CLASSLOGFILE_H


#include <string>
#include "esp_log.h"


class ClassLogFile
{
private:
    std::string logroot;
    std::string logfile;
    std::string dataroot;
    std::string datafile;
    unsigned short logFileRetentionInDays;
    unsigned short dataLogRetentionInDays;
    bool doDataLogToSD;
    esp_log_level_t loglevel;
public:
    ClassLogFile(std::string _logpath, std::string _logfile, std::string _logdatapath, std::string _datafile);

    void WriteHeapInfo(std::string _id);

    void setLogLevel(esp_log_level_t _logLevel);
    void SetLogFileRetention(unsigned short _LogFileRetentionInDays);
    void SetDataLogRetention(unsigned short _DataLogRetentionInDays);
    void SetDataLogToSD(bool _doDataLogToSD);
    bool GetDataLogToSD();

    void WriteToFile(esp_log_level_t level, const std::string& tag, const std::string& message, bool _time);
    void WriteToFile(esp_log_level_t level, const std::string& tag, const std::string& message);
    esp_log_level_t getLogLevel() { return loglevel; };

    void CloseLogFileAppendHandle();

    // Flush any buffered log lines to the SD card. Safe to call from any task.
    void FlushLogBuffer();

    bool CreateLogDirectories();
    void RemoveOldLogFile();
    void RemoveOldDataLog();

//    void WriteToData(std::string _ReturnRawValue, std::string _ReturnValue, std::string _ReturnPreValue, std::string _ErrorMessageText, std::string _digit, std::string _analog);
    void WriteToData(std::string _timestamp, std::string _name, std::string  _ReturnRawValue, std::string  _ReturnValue, std::string  _ReturnPreValue, std::string  _ReturnRateValue, std::string  _ReturnChangeAbsolute, std::string  _ErrorMessageText, std::string  _digit, std::string  _analog);


    std::string GetCurrentFileName();
    std::string GetCurrentFileNameData();
};

extern ClassLogFile LogFile;

// Guarded DEBUG logging for hot paths (e.g. the CNN inference loop). The message expression is
// only evaluated when the file log level is DEBUG+, so `"..." + std::to_string(x) + ...` argument
// strings are not built and thrown away on every cycle in production (default INFO level). When
// DEBUG is active it behaves exactly like LogFile.WriteToFile(ESP_LOG_DEBUG, tag, msg).
#define LOGD(tag, msg) do { if (LogFile.getLogLevel() >= ESP_LOG_DEBUG) { LogFile.WriteToFile(ESP_LOG_DEBUG, (tag), (msg)); } } while (0)

#endif //CLASSLOGFILE_H