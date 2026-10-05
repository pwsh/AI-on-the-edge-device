/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "server_file.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <dirent.h>
#ifdef __cplusplus
}
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "esp_vfs.h"
#include <esp_spiffs.h>
#include "esp_http_server.h"

#include "../../include/defines.h"
#include "ClassLogFile.h"

#include "MainFlowControl.h"

#include "server_help.h"
#include "md5.h"
#ifdef ENABLE_MQTT
    #include "interface_mqtt.h"
#endif //ENABLE_MQTT
#include "server_GPIO.h"

#include "Helper.h"
#include "miniz.h"
#include "basic_auth.h"

static const char *TAG = "OTA FILE";

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SERVER_FILER_SCRATCH_BUFSIZE];
};

#include <iostream>
#include <sys/types.h>
#include <dirent.h>

using namespace std;

string SUFFIX_ZW = "_tmp";

static esp_err_t send_logfile(httpd_req_t *req, bool send_full_file);
static esp_err_t send_datafile(httpd_req_t *req, bool send_full_file);

esp_err_t get_numbers_file_handler(httpd_req_t *req)
{
    std::string ret = flowctrl.getNumbersName();

//    ESP_LOGI(TAG, "Result get_numbers_file_handler: %s", ret.c_str());

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    httpd_resp_sendstr_chunk(req, ret.c_str());
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

esp_err_t get_data_file_handler(httpd_req_t *req)
{
    struct dirent *entry;

    std::string _filename, _fileext;
    size_t pos = 0;
    
    const char verz_name[] = "/sdcard/log/data";
    ESP_LOGD(TAG, "Suche data files in /sdcard/log/data");

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);
    while ((entry = readdir(dir)) != NULL) 
    {
        _filename = std::string(entry->d_name);
        ESP_LOGD(TAG, "File: %s", _filename.c_str());

        // ignore all files with starting dot (hidden files)
        if (_filename.rfind(".", 0) == 0) {
            continue;
        }

        _fileext = _filename;
        pos = _fileext.find_last_of(".");
        if (pos != std::string::npos)
            _fileext = _fileext.erase(0, pos + 1);

        ESP_LOGD(TAG, " Extension: %s", _fileext.c_str());

        if (_fileext == "csv")
        {
            _filename = _filename + "\t";
            httpd_resp_sendstr_chunk(req, _filename.c_str());
        }
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t get_tflite_file_handler(httpd_req_t *req)
{
    struct dirent *entry;

    std::string _filename, _fileext;
    size_t pos = 0;
    
    const char verz_name[] = "/sdcard/config";
    ESP_LOGD(TAG, "Suche TFLITE in /sdcard/config/");

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");

    DIR *dir = opendir(verz_name);
    while ((entry = readdir(dir)) != NULL) 
    {
        _filename = std::string(entry->d_name);
        ESP_LOGD(TAG, "File: %s", _filename.c_str());

        // ignore all files with starting dot (hidden files)
        if (_filename.rfind(".", 0) == 0) {
            continue;
        }

        _fileext = _filename;
        pos = _fileext.find_last_of(".");
        if (pos != std::string::npos)
            _fileext = _fileext.erase(0, pos + 1);

        ESP_LOGD(TAG, " Extension: %s", _fileext.c_str());

        if ((_fileext == "tfl") || (_fileext == "tflite"))
        {
            _filename = "/config/" + _filename + "\t";
            httpd_resp_sendstr_chunk(req, _filename.c_str());
        }
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath, const char *uripath, bool readonly)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    char dirpath_corrected[FILE_PATH_MAX];
    strcpy(dirpath_corrected, dirpath);

    file_server_data *server_data = (file_server_data *)req->user_ctx;

    if ((strlen(dirpath_corrected) - 1) > strlen(server_data->base_path)) {
        // if dirpath is not mountpoint, the last "\" needs to be removed
        dirpath_corrected[strlen(dirpath_corrected) - 1] = '\0';
    }

    DIR *pdir = opendir(dirpath_corrected);

    const size_t dirpath_len = strlen(dirpath);
    ESP_LOGD(TAG, "Dirpath: <%s>, Pathlength: %d", dirpath, dirpath_len);

    // Retrieve the base path of file storage to construct the full path
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    ESP_LOGD(TAG, "entrypath: <%s>", entrypath);

    if (!pdir) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + std::string(dirpath) + "!");
        // Respond with 404 Not Found
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Send HTML file header
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html lang=\"en\" xml:lang=\"en\"><head>");
    httpd_resp_sendstr_chunk(req, "<link href=\"/file_server.css\" rel=\"stylesheet\">");
    httpd_resp_sendstr_chunk(req, "<link href=\"/firework.css\" rel=\"stylesheet\">");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/jquery-3.7.1.min.js\"></script>");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/firework.js\"></script></head>");

    httpd_resp_sendstr_chunk(req, "<body>");

    httpd_resp_sendstr_chunk(req, "<table class=\"fixed\" border=\"0\" width=100% style=\"font-family: arial\">");
    httpd_resp_sendstr_chunk(req, "<tr><td style=\"vertical-align: top;width: 300px;\"><h2>Fileserver</h2></td>"
                                  "<td rowspan=\"2\"><table border=\"0\" style=\"width:100%\"><tr><td style=\"width:80px\">"
                                  "<label for=\"newfile\">Source</label></td><td colspan=\"2\">"
                                  "<input id=\"newfile\" type=\"file\" onchange=\"setpath()\" style=\"width:100%;\"></td></tr>"
                                  "<tr><td><label for=\"filepath\">Destination</label></td><td>"
                                  "<input id=\"filepath\" type=\"text\" style=\"width:94%;\"></td><td>"
                                  "<button id=\"upload\" type=\"button\" class=\"button\" onclick=\"upload()\">Upload</button></td></tr>"
                                  "</table></td></tr><tr></tr><tr><td colspan=\"2\">"
                                  "<button style=\"font-size:16px; padding: 5px 10px\" id=\"dirup\" type=\"button\" onclick=\"dirup()\""
                                  "disabled>&#129145; Directory up</button>"
                                  "<button style=\"font-size:16px; padding: 5px 10px; margin-left:8px\" type=\"button\" "
                                  "title=\"Download this folder (incl. subfolders) as a ZIP\" "
                                  "onclick=\"window.location.href=window.location.pathname+'?zip=1'\">&#128229; Download folder as ZIP</button>"
                                  "<span style=\"padding-left:15px\" id=\"currentpath\">"
                                  "</span></td></tr>");
    httpd_resp_sendstr_chunk(req, "</table>");

    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\" src=\"/file_server.js\"></script>");
    httpd_resp_sendstr_chunk(req, "<script type=\"text/javascript\">initFileServer();</script>");

    std::string _zw = std::string(dirpath);
    _zw = _zw.substr(8, _zw.length() - 8);
    _zw = "/delete/" + _zw + "?task=deldircontent";

    // Send file-list table definition and column labels
    httpd_resp_sendstr_chunk(req, "<table id=\"files_table\">"
                                  "<col width=\"800px\"><col width=\"300px\"><col width=\"300px\"><col width=\"100px\">"
                                  "<thead><tr><th>Name</th><th>Type</th><th>Size</th>");

    if (!readonly) {
        httpd_resp_sendstr_chunk(req, "<th><form method=\"post\" action=\"");
        httpd_resp_sendstr_chunk(req, _zw.c_str());
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">DELETE ALL!</button></form></th></tr>");
    }

    httpd_resp_sendstr_chunk(req, "</thead><tbody>\n");

    // Iterate over all files / folders and fetch their names and sizes
    while ((entry = readdir(pdir)) != NULL) {
        // wlan.ini should not be displayed!
        if (strcmp("wlan.ini", entry->d_name) != 0) {
            entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

            strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
            ESP_LOGD(TAG, "Entrypath: %s", entrypath);

            if (stat(entrypath, &entry_stat) == -1) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat " + std::string(entrytype) + ": " + std::string(entry->d_name));
                continue;
            }

            if (entry->d_type == DT_DIR) {
                strcpy(entrysize, "-\0");
            }
            else {
                if (entry_stat.st_size >= 1024) {
                    sprintf(entrysize, "%ld KiB", entry_stat.st_size / 1024); // kBytes
                }
                else {
                    sprintf(entrysize, "%ld B", entry_stat.st_size); // Bytes
                }
            }

            ESP_LOGD(TAG, "Found %s: %s (%s bytes)", entrytype, entry->d_name, entrysize);

            // Send chunk of HTML file containing table entries with file name and size
            httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
            httpd_resp_sendstr_chunk(req, "/fileserver");
            httpd_resp_sendstr_chunk(req, uripath);
            httpd_resp_sendstr_chunk(req, entry->d_name);

            if (entry->d_type == DT_DIR) {
                httpd_resp_sendstr_chunk(req, "/");
            }

            httpd_resp_sendstr_chunk(req, "\">");
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "</a>");

            // For sub-directories, offer a one-click "download as ZIP" link (recursive).
            if (entry->d_type == DT_DIR) {
                httpd_resp_sendstr_chunk(req, " <a style=\"text-decoration:none\" title=\"Download this folder (incl. subfolders) as a ZIP\" href=\"/fileserver");
                httpd_resp_sendstr_chunk(req, uripath);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "/?zip=1\">&#128229;</a>");
            }

            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrytype);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);

            if (!readonly) {
                httpd_resp_sendstr_chunk(req, "</td><td>");
                httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
                httpd_resp_sendstr_chunk(req, uripath);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
            }

            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        }
    }

    closedir(pdir);

    // Finish the file list table
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    // Send remaining chunk of HTML file to complete it
    httpd_resp_sendstr_chunk(req, "</body></html>");

    // Send empty chunk to signal HTTP response completion
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// ---- Download a directory (recursively) as a streamed ZIP ------------------
// Triggered from the file-server directory listing ("Download folder as ZIP" button / per-folder
// link -> GET /fileserver/<dir>/?zip=1). The archive is generated on the fly and streamed with chunked
// transfer while the tree is walked: no temp file, no compression (method 0 "store" - the content is
// mostly JPEGs anyway), no free SD space needed, and the client gets bytes from the first file on.
// Each entry uses general-purpose flag bit 3 (sizes/CRC follow the data in a data descriptor), so the
// file only has to be read once. The central directory records are collected in PSRAM in fixed-size
// blocks (~46 bytes + name length per entry) and emitted at the end; ZIP64 records are used
// automatically when an entry count / size / offset no longer fits the classic 16/32-bit fields.

// ZIPSTREAM_CORE_BEGIN - pure, platform-independent store-only ZIP writer (also compiled by the host
// test harness, so keep it free of ESP-IDF dependencies). The caller streams each entry's data bytes
// itself (through the same output, in order) between zs_begin_entry() and zs_end_entry().
struct ZipStreamWriter {
    typedef bool (*WriteFn)(void *ctx, const uint8_t *data, size_t len);
    typedef void *(*AllocFn)(size_t size);
    typedef void (*FreeFn)(void *ptr);
    struct CdBlock { uint8_t *data; uint32_t used; uint32_t cap; };

    WriteFn write;
    void *ctx;
    AllocFn alloc;
    FreeFn dealloc;
    uint64_t offset;            // bytes emitted so far (header + data + descriptors)
    uint64_t entries;           // completed entries (= central directory records)
    uint64_t cdBytes;           // total size of the collected central directory
    std::vector<CdBlock> cd;    // central directory records, in blocks from AllocFn (PSRAM on the device)

    // Entry in progress
    bool inEntry;
    bool curZip64;              // local header carries a ZIP64 extra -> 8-byte data descriptor sizes
    uint16_t curFlags, curTime, curDate;
    uint64_t curLocalOffset;
    std::string curName;

    // ZIP64 thresholds; only lowered by the test harness to exercise the ZIP64 paths
    uint64_t zip64SizeLimit;    // entry size  >= this -> ZIP64 (default 0xFFFFFFFF)
    uint64_t zip64OffsetLimit;  // offset      >= this -> ZIP64 (default 0xFFFFFFFF)
    uint64_t zip64CountLimit;   // entry count >= this -> ZIP64 EOCD (default 0xFFFF)
};

#define ZS_CD_BLOCK_SIZE  32768

static inline void zs_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void zs_put32(uint8_t *p, uint32_t v) { zs_put16(p, (uint16_t)v); zs_put16(p + 2, (uint16_t)(v >> 16)); }
static inline void zs_put64(uint8_t *p, uint64_t v) { zs_put32(p, (uint32_t)v); zs_put32(p + 4, (uint32_t)(v >> 32)); }

static void zs_init(ZipStreamWriter *z, ZipStreamWriter::WriteFn write, void *ctx,
                    ZipStreamWriter::AllocFn alloc, ZipStreamWriter::FreeFn dealloc)
{
    z->write = write;
    z->ctx = ctx;
    z->alloc = alloc;
    z->dealloc = dealloc;
    z->offset = 0;
    z->entries = 0;
    z->cdBytes = 0;
    z->cd.clear();
    z->inEntry = false;
    z->curZip64 = false;
    z->curFlags = z->curTime = z->curDate = 0;
    z->curLocalOffset = 0;
    z->curName.clear();
    z->zip64SizeLimit = 0xFFFFFFFFull;
    z->zip64OffsetLimit = 0xFFFFFFFFull;
    z->zip64CountLimit = 0xFFFFull;
}

// Releases the central directory blocks (safe to call more than once / after a failure).
static void zs_free(ZipStreamWriter *z)
{
    for (size_t i = 0; i < z->cd.size(); i++) {
        z->dealloc(z->cd[i].data);
    }
    z->cd.clear();
    std::vector<ZipStreamWriter::CdBlock>().swap(z->cd);
    std::string().swap(z->curName);
}

static bool zs_emit(ZipStreamWriter *z, const uint8_t *data, size_t len)
{
    if (!z->write(z->ctx, data, len)) {
        return false;
    }
    z->offset += len;
    return true;
}

// Reserve len bytes at the end of the central directory (new block when the current one is full).
static uint8_t *zs_cd_reserve(ZipStreamWriter *z, size_t len)
{
    if (z->cd.empty() || (z->cd.back().cap - z->cd.back().used) < len) {
        size_t cap = (len > ZS_CD_BLOCK_SIZE) ? len : ZS_CD_BLOCK_SIZE;
        uint8_t *p = (uint8_t *)z->alloc(cap);
        if (!p) {
            return NULL;
        }
        ZipStreamWriter::CdBlock b = { p, 0, (uint32_t)cap };
        z->cd.push_back(b);
    }
    ZipStreamWriter::CdBlock &b = z->cd.back();
    uint8_t *p = b.data + b.used;
    b.used += (uint32_t)len;
    z->cdBytes += len;
    return p;
}

// Emit the local file header. sizeHint is the expected entry size (stat), only used to decide
// whether the entry needs ZIP64 (8-byte sizes in the data descriptor).
static bool zs_begin_entry(ZipStreamWriter *z, const char *name, uint64_t sizeHint, uint16_t dosTime, uint16_t dosDate)
{
    size_t nameLen = strlen(name);
    if (z->inEntry || nameLen == 0 || nameLen > 0xFFFF) {
        return false;
    }

    uint16_t flags = 0x0008;                                    // bit 3: CRC/sizes in data descriptor
    for (size_t i = 0; i < nameLen; i++) {
        if ((uint8_t)name[i] >= 0x80) { flags |= 0x0800; break; }   // bit 11: name is UTF-8
    }
    z->curZip64 = (sizeHint >= z->zip64SizeLimit);
    z->curFlags = flags;
    z->curTime = dosTime;
    z->curDate = dosDate;
    z->curLocalOffset = z->offset;
    z->curName.assign(name, nameLen);

    uint8_t h[30 + 20];
    zs_put32(h + 0, 0x04034b50);                                // local file header signature
    zs_put16(h + 4, z->curZip64 ? 45 : 20);                     // version needed to extract
    zs_put16(h + 6, flags);
    zs_put16(h + 8, 0);                                         // method 0 = store
    zs_put16(h + 10, dosTime);
    zs_put16(h + 12, dosDate);
    zs_put32(h + 14, 0);                                        // CRC-32 (in data descriptor)
    zs_put32(h + 18, z->curZip64 ? 0xFFFFFFFF : 0);             // compressed size
    zs_put32(h + 22, z->curZip64 ? 0xFFFFFFFF : 0);             // uncompressed size
    zs_put16(h + 26, (uint16_t)nameLen);
    zs_put16(h + 28, z->curZip64 ? 20 : 0);                     // extra field length
    size_t extraLen = 0;
    if (z->curZip64) {                                          // ZIP64 extra: sizes (0, real ones follow)
        zs_put16(h + 30, 0x0001);
        zs_put16(h + 32, 16);
        zs_put64(h + 34, 0);
        zs_put64(h + 42, 0);
        extraLen = 20;
    }
    if (!zs_emit(z, h, 30) || !zs_emit(z, (const uint8_t *)name, nameLen) ||
        (extraLen && !zs_emit(z, h + 30, extraLen))) {
        return false;
    }
    z->inEntry = true;
    return true;
}

// Finish the entry: the caller has already sent exactly `size` data bytes (CRC-32 `crc`) through the
// writer's output. Emits the data descriptor and records the central directory entry.
static bool zs_end_entry(ZipStreamWriter *z, uint32_t crc, uint64_t size)
{
    if (!z->inEntry) {
        return false;
    }
    z->inEntry = false;
    z->offset += size;                                          // data bytes went out via the caller

    uint8_t d[24];
    size_t dLen;
    zs_put32(d + 0, 0x08074b50);                                // data descriptor signature
    zs_put32(d + 4, crc);
    if (z->curZip64) {
        zs_put64(d + 8, size);                                  // compressed size (store: = size)
        zs_put64(d + 16, size);
        dLen = 24;
    } else {
        zs_put32(d + 8, (uint32_t)size);
        zs_put32(d + 12, (uint32_t)size);
        dLen = 16;
    }
    if (!zs_emit(z, d, dLen)) {
        return false;
    }

    // Central directory record (+ ZIP64 extra holding only the fields that overflow)
    bool bigSize = z->curZip64 || (size >= z->zip64SizeLimit) || (size >= 0xFFFFFFFFull);
    bool bigOffs = (z->curLocalOffset >= z->zip64OffsetLimit) || (z->curLocalOffset >= 0xFFFFFFFFull);
    uint16_t extraLen = (bigSize || bigOffs) ? (uint16_t)(4 + (bigSize ? 16 : 0) + (bigOffs ? 8 : 0)) : 0;
    size_t nameLen = z->curName.size();
    uint8_t *r = zs_cd_reserve(z, 46 + nameLen + extraLen);
    if (!r) {
        return false;
    }
    uint16_t ver = extraLen ? 45 : 20;
    zs_put32(r + 0, 0x02014b50);                                // central file header signature
    zs_put16(r + 4, ver);                                       // version made by (MS-DOS host)
    zs_put16(r + 6, ver);                                       // version needed to extract
    zs_put16(r + 8, z->curFlags);
    zs_put16(r + 10, 0);                                        // method 0 = store
    zs_put16(r + 12, z->curTime);
    zs_put16(r + 14, z->curDate);
    zs_put32(r + 16, crc);
    zs_put32(r + 20, bigSize ? 0xFFFFFFFF : (uint32_t)size);    // compressed size
    zs_put32(r + 24, bigSize ? 0xFFFFFFFF : (uint32_t)size);    // uncompressed size
    zs_put16(r + 28, (uint16_t)nameLen);
    zs_put16(r + 30, extraLen);
    zs_put16(r + 32, 0);                                        // comment length
    zs_put16(r + 34, 0);                                        // disk number start
    zs_put16(r + 36, 0);                                        // internal attributes
    zs_put32(r + 38, 0);                                        // external attributes
    zs_put32(r + 42, bigOffs ? 0xFFFFFFFF : (uint32_t)z->curLocalOffset);
    memcpy(r + 46, z->curName.data(), nameLen);
    if (extraLen) {
        uint8_t *x = r + 46 + nameLen;
        zs_put16(x, 0x0001);                                    // ZIP64 extended information
        zs_put16(x + 2, (uint16_t)(extraLen - 4));
        x += 4;
        if (bigSize) { zs_put64(x, size); zs_put64(x + 8, size); x += 16; }
        if (bigOffs) { zs_put64(x, z->curLocalOffset); }
    }
    z->entries++;
    return true;
}

// Emit the central directory, the ZIP64 end records (when needed) and the end of central directory.
static bool zs_finish(ZipStreamWriter *z)
{
    if (z->inEntry) {
        return false;
    }
    uint64_t cdOffset = z->offset;
    for (size_t i = 0; i < z->cd.size(); i++) {
        if (!zs_emit(z, z->cd[i].data, z->cd[i].used)) {
            return false;
        }
    }

    bool zip64 = (z->entries >= z->zip64CountLimit) || (z->entries >= 0xFFFFull) ||
                 (cdOffset >= z->zip64OffsetLimit) || (cdOffset >= 0xFFFFFFFFull) ||
                 (z->cdBytes >= 0xFFFFFFFFull);
    if (zip64) {
        uint64_t eocd64Offset = z->offset;
        uint8_t e[56 + 20];
        zs_put32(e + 0, 0x06064b50);                            // ZIP64 end of central directory record
        zs_put64(e + 4, 44);                                    // size of the remaining record
        zs_put16(e + 12, 45);                                   // version made by
        zs_put16(e + 14, 45);                                   // version needed
        zs_put32(e + 16, 0);                                    // this disk
        zs_put32(e + 20, 0);                                    // disk with central directory
        zs_put64(e + 24, z->entries);                           // entries on this disk
        zs_put64(e + 32, z->entries);                           // total entries
        zs_put64(e + 40, z->cdBytes);
        zs_put64(e + 48, cdOffset);
        zs_put32(e + 56, 0x07064b50);                           // ZIP64 end of central directory locator
        zs_put32(e + 60, 0);
        zs_put64(e + 64, eocd64Offset);
        zs_put32(e + 72, 1);                                    // total number of disks
        if (!zs_emit(z, e, sizeof(e))) {
            return false;
        }
    }

    uint8_t e[22];
    uint16_t cnt = (z->entries >= 0xFFFFull) ? 0xFFFF : (uint16_t)z->entries;
    zs_put32(e + 0, 0x06054b50);                                // end of central directory record
    zs_put16(e + 4, 0);
    zs_put16(e + 6, 0);
    zs_put16(e + 8, cnt);
    zs_put16(e + 10, cnt);
    zs_put32(e + 12, (z->cdBytes >= 0xFFFFFFFFull) ? 0xFFFFFFFF : (uint32_t)z->cdBytes);
    zs_put32(e + 16, (cdOffset >= 0xFFFFFFFFull) ? 0xFFFFFFFF : (uint32_t)cdOffset);
    zs_put16(e + 20, 0);                                        // comment length
    return zs_emit(z, e, sizeof(e));
}
// ZIPSTREAM_CORE_END

// ---- ESP side: buffered chunked output + directory walk --------------------
#define ZIPDIR_OUT_BUFSIZE  8192        // one HTTP chunk; headers/descriptors are coalesced with file data

struct ZipHttpOut {
    httpd_req_t *req;
    uint8_t *buf;
    size_t len;
    bool failed;
};

static bool zipout_flush(ZipHttpOut *o)
{
    if (o->failed) {
        return false;
    }
    if (o->len > 0) {
        if (httpd_resp_send_chunk(o->req, (const char *)o->buf, o->len) != ESP_OK) {
            o->failed = true;
            return false;
        }
        o->len = 0;
    }
    return true;
}

static bool zipout_write(void *ctx, const uint8_t *data, size_t len)
{
    ZipHttpOut *o = (ZipHttpOut *)ctx;
    while (len > 0) {
        if (o->failed) {
            return false;
        }
        size_t n = MIN(len, ZIPDIR_OUT_BUFSIZE - o->len);
        memcpy(o->buf + o->len, data, n);
        o->len += n;
        data += n;
        len -= n;
        if (o->len == ZIPDIR_OUT_BUFSIZE && !zipout_flush(o)) {
            return false;
        }
    }
    return true;
}

// Central directory blocks: PSRAM (8k entries ~ 700 KB), internal RAM only as a fallback.
static void *zipdir_cd_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}

static void zipdir_cd_free(void *ptr)
{
    free(ptr);
}

struct ZipDirJob {
    ZipStreamWriter zw;
    ZipHttpOut out;
    uint32_t files;
    uint64_t dataBytes;
};

// DOS date/time from a file's mtime; 1980-01-01 00:00 when unknown / out of range.
static void zipdir_dos_datetime(time_t t, uint16_t *dosTime, uint16_t *dosDate)
{
    struct tm tm;
    if (localtime_r(&t, &tm) != NULL && tm.tm_year >= 80 && tm.tm_year <= 207) {
        *dosTime = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
        *dosDate = (uint16_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    } else {
        *dosTime = 0;
        *dosDate = (uint16_t)((1 << 5) | 1);
    }
}

// Stream one file as a stored entry. Returns false only when the response is dead (send/ memory
// failure); an unreadable file is skipped with a warning.
static bool zipdir_add_file(ZipDirJob *job, const std::string &src, const std::string &arc, const struct stat &st)
{
    FILE *f = fopen(src.c_str(), "rb");
    if (!f) {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "zipdir: cannot open " + src + ", skipped");
        return true;
    }
    uint16_t dosTime, dosDate;
    zipdir_dos_datetime(st.st_mtime, &dosTime, &dosDate);
    uint64_t expected = (uint64_t)st.st_size;
    if (!zs_begin_entry(&job->zw, arc.c_str(), expected, dosTime, dosDate)) {
        fclose(f);
        return false;
    }

    // Read straight into the free tail of the output buffer, so file data and the surrounding
    // headers leave in full-size chunks. Send at most the stat() size (snapshot of a growing log).
    ZipHttpOut *o = &job->out;
    uint32_t crc = MZ_CRC32_INIT;
    uint64_t sent = 0;
    while (sent < expected) {
        if (o->len == ZIPDIR_OUT_BUFSIZE && !zipout_flush(o)) {
            break;
        }
        size_t want = (size_t)MIN((uint64_t)(ZIPDIR_OUT_BUFSIZE - o->len), expected - sent);
        size_t n = fread(o->buf + o->len, 1, want, f);
        if (n == 0) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "zipdir: short read on " + src);
            break;
        }
        crc = (uint32_t)mz_crc32(crc, o->buf + o->len, n);
        o->len += n;
        sent += n;
    }
    fclose(f);
    if (o->failed || !zs_end_entry(&job->zw, crc, sent)) {
        return false;
    }
    job->files++;
    job->dataBytes += sent;
    if ((job->files % 16) == 0) {
        vTaskDelay(1);   // let other tasks (and the idle task / watchdog) run
    }
    return true;
}

// Recursively add every file under fsDir into the archive under arcPrefix. wlan.ini (Wi-Fi
// credentials) is skipped; a missing dir adds nothing (not an error). Returns false when the
// stream has failed (client gone / out of memory).
static bool zipdir_add_recursive(ZipDirJob *job, const std::string &fsDir, const std::string &arcPrefix)
{
    DIR *d = opendir(fsDir.c_str());
    if (!d) return true;
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (toUpper(name) == "WLAN.INI") continue;              // never expose Wi-Fi credentials (any case)
        std::string src = fsDir + "/" + name;
        struct stat st;
        if (stat(src.c_str(), &st) != 0) continue;
        std::string arc = arcPrefix + name;
        if (S_ISDIR(st.st_mode)) {
            if (!zipdir_add_recursive(job, src, arc + "/")) { ok = false; break; }
        } else {
            if (!zipdir_add_file(job, src, arc, st)) { ok = false; break; }
        }
    }
    closedir(d);
    return ok;
}

// Stream a zip of dirpath (recursively, incl. subfolders) as "<foldername>.zip".
static esp_err_t zip_dir_and_stream(httpd_req_t *req, const char *dirpath, const char *uripath)
{
    // Download name from the last path segment: "/log/data/" -> "data.zip"; root "/" -> "sdcard.zip".
    std::string up = uripath ? uripath : "/";
    while (up.size() > 1 && up.back() == '/') up.pop_back();
    size_t slash = up.find_last_of('/');
    std::string base = (slash == std::string::npos) ? up : up.substr(slash + 1);
    if (base.empty()) base = "sdcard";
    std::string zipname = base + ".zip";

    // dirpath has a trailing '/'; strip it for the walk root.
    std::string root = dirpath;
    while (root.size() > 1 && root.back() == '/') root.pop_back();

    // Allocate everything BEFORE staging any response header, so a failure here returns a real 500
    // instead of a 200 with zip headers and an empty body.
    ZipDirJob *job = new (std::nothrow) ZipDirJob();
    uint8_t *buf = (uint8_t *) malloc(ZIPDIR_OUT_BUFSIZE);
    if (!job || !buf) {
        delete job;
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    job->out.req = req;
    job->out.buf = buf;
    job->out.len = 0;
    job->out.failed = false;
    job->files = 0;
    job->dataBytes = 0;
    zs_init(&job->zw, zipout_write, &job->out, zipdir_cd_alloc, zipdir_cd_free);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "zipdir: streaming " + zipname + " from " + root);
    int64_t t0 = esp_timer_get_time();

    char dispo[160];
    snprintf(dispo, sizeof(dispo), "attachment; filename=\"%s\"", zipname.c_str());
    httpd_resp_set_type(req, "application/zip");
    httpd_resp_set_hdr(req, "Content-Disposition", dispo);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    bool ok = zipdir_add_recursive(job, root, "");
    if (ok) ok = zs_finish(&job->zw) && zipout_flush(&job->out);

    uint32_t files = job->files;
    uint64_t total = job->zw.offset;
    zs_free(&job->zw);
    free(buf);
    delete job;

    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    char secs[16];
    snprintf(secs, sizeof(secs), "%lld.%01lld", (long long)(ms / 1000), (long long)((ms % 1000) / 100));
    if (!ok) {
        // Headers (and part of the body) are already out: no error page possible. Returning ESP_FAIL
        // makes httpd close the socket, so the client sees a truncated download instead of a "valid" zip.
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "zipdir: " + zipname + " aborted after " + std::to_string(files) +
                            " files, " + std::to_string(total) + " bytes, " + secs + " s (client gone or out of memory)");
        return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, NULL, 0);   // signal end of response
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "zipdir: sent " + zipname + ": " + std::to_string(files) + " files, " +
                        std::to_string(total) + " bytes in " + secs + " s");
    return ESP_OK;
}

static esp_err_t logfileact_get_full_handler(httpd_req_t *req) {
    return send_logfile(req, true);
}

static esp_err_t logfileact_get_last_part_handler(httpd_req_t *req) {
    return send_logfile(req, false);
}

static esp_err_t datafileact_get_full_handler(httpd_req_t *req) {
    return send_datafile(req, true);
}

static esp_err_t datafileact_get_last_part_handler(httpd_req_t *req) {
    return send_datafile(req, false);
}

static esp_err_t send_datafile(httpd_req_t *req, bool send_full_file)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "data_get_last_part_handler");
    FILE *fd = NULL;
    //struct stat file_stat;
    ESP_LOGD(TAG, "uri: %s", req->uri);

    std::string currentfilename = LogFile.GetCurrentFileNameData();

    ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, currentfilename.c_str(), currentfilename.c_str());

    fd = fopen(currentfilename.c_str(), "r");
    if (!fd) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + currentfilename + "!");
        /* Respond with 404 Error */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

//    ESP_LOGI(TAG, "Sending file: %s (%ld bytes)...", &filename, file_stat.st_size);
    set_content_type_from_file(req, currentfilename.c_str());

    if (!send_full_file) { // Send only last part of file
        ESP_LOGD(TAG, "Sending last %d bytes of the actual datafile!", LOGFILE_LAST_PART_BYTES);

        /* Adapted from https://www.geeksforgeeks.org/implement-your-own-tail-read-last-n-lines-of-a-huge-file/ */
        if (fseek(fd, 0, SEEK_END)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to get to end of file!");
            return ESP_FAIL;
        }
        else {
            long pos = ftell(fd); // Number of bytes in the file
            ESP_LOGI(TAG, "File contains %ld bytes", pos);

            if (fseek(fd, pos - std::min((long)LOGFILE_LAST_PART_BYTES, pos), SEEK_SET)) { // Go LOGFILE_LAST_PART_BYTES bytes back from EOF
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to go back " + to_string(std::min((long)LOGFILE_LAST_PART_BYTES, pos)) + " bytes within the file!");
                return ESP_FAIL;
            }
        }

        /* Find end of line */
        while (1) {
            if (fgetc(fd) == '\n') {
                break;
            }
        }
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SERVER_FILER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File sending failed!");
            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t send_logfile(httpd_req_t *req, bool send_full_file)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "log_get_last_part_handler");
    FILE *fd = NULL;
    //struct stat file_stat;
    ESP_LOGI(TAG, "uri: %s", req->uri);

    const char* filename = ""; 

    std::string currentfilename = LogFile.GetCurrentFileName();

    ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, filename, currentfilename.c_str());

    // Since the log file is still could open for writing, we need to close it first
    LogFile.CloseLogFileAppendHandle();
    // Persist any buffered log lines so the reader sees the most recent entries.
    LogFile.FlushLogBuffer();

    fd = fopen(currentfilename.c_str(), "r");
    if (!fd) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + currentfilename + "!");
        /* Respond with 404 Error */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

//    ESP_LOGI(TAG, "Sending file: %s (%ld bytes)...", &filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    if (!send_full_file) { // Send only last part of file
        ESP_LOGD(TAG, "Sending last %d bytes of the actual logfile!", LOGFILE_LAST_PART_BYTES);

        /* Adapted from https://www.geeksforgeeks.org/implement-your-own-tail-read-last-n-lines-of-a-huge-file/ */
        if (fseek(fd, 0, SEEK_END)) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to get to end of file!");
            return ESP_FAIL;
        }
        else {
            long pos = ftell(fd); // Number of bytes in the file
            ESP_LOGI(TAG, "File contains %ld bytes", pos);

            if (fseek(fd, pos - std::min((long)LOGFILE_LAST_PART_BYTES, pos), SEEK_SET)) { // Go LOGFILE_LAST_PART_BYTES bytes back from EOF
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to go back " + to_string(std::min((long)LOGFILE_LAST_PART_BYTES, pos)) + " bytes within the file!");
                return ESP_FAIL;
            }
        }

        /* Find end of line */
        while (1) {
            if (fgetc(fd) == '\n') {
                break;
            }
        }
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SERVER_FILER_SCRATCH_BUFSIZE, fd);

        /* Send the buffer contents as HTTP response chunk */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File sending failed!");
            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "download_get_handler");
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;
    ESP_LOGD(TAG, "uri: %s", req->uri);

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri  + sizeof("/fileserver") - 1, sizeof(filepath));    

    ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

//    filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
//                                             req->uri, sizeof(filepath));

    if (!filename) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Filename is too long");
        /* Respond with 414 Error */
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        bool readonly = false;
        size_t buf_len = httpd_req_get_url_query_len(req) + 1;
        if (buf_len > 1) {
            char buf[buf_len];
            if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query => %s", buf);
                char param[32];
                /* Get value of expected key from query string */
                if (httpd_query_key_value(buf, "readonly", param, sizeof(param)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => readonly=%s", param);
                    readonly = (strcmp(param,"true") == 0);
                }
                // ?zip[=1] -> stream this directory (recursively) as a downloadable ZIP instead of listing it
                if (httpd_query_key_value(buf, "zip", param, sizeof(param)) == ESP_OK) {
                    return zip_dir_and_stream(req, filepath, filename);
                }
            }
        }

        ESP_LOGD(TAG, "uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);
        return http_resp_dir_html(req, filepath, filename, readonly);
    }

    std::string testwlan = toUpper(std::string(filename));

    if ((stat(filepath, &file_stat) == -1) || (testwlan.compare("/WLAN.INI") == 0 )) {  // wlan.ini should not be displayed!

        /* The file isn't there (stat failed) or it's a restricted file (wlan.ini) -> 404. This is
         * routine, not an error: e.g. the config / publishing page fetches config/publishing.cfg, which
         * doesn't exist until the data-publishing quick control is first saved (a missing file just means
         * "use defaults"). Log at DEBUG so it doesn't clutter the log as an ERROR. */
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "File not found or restricted -> returning 404: " +
                            std::string(filepath) + " (normal for optional files, e.g. config/publishing.cfg before first save)");
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    /* HEAD request: return the headers - including the real Content-Length - with no body. The GET
     * path streams with chunked encoding (no Content-Length); without this, a HEAD fell through to
     * the catch-all handler and reported a bogus fixed size. httpd_resp_send() always writes its own
     * Content-Length from the body length, so emit the header block directly. */
    if (req->method == HTTP_HEAD) {
        char head[320];
        int n = snprintf(head, sizeof(head),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "\r\n",
            get_content_type_from_file(filename), (long) file_stat.st_size);
        httpd_send(req, head, n);
        return ESP_OK;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + std::string(filepath) + "!");
        /* Respond with 404 Error */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, get404());
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    ESP_LOGD(TAG, "Sending file: %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SERVER_FILER_SCRATCH_BUFSIZE, fd);

        /* Send buffer contents as HTTP chunk. If empty this functions as a
         * last-chunk message, signaling end-of-response, to the HTTP client.
         * See RFC 2616, section 3.6.1 for details on Chunked Transfer Encoding. */
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File sending failed!");
            /* Abort sending file */
            httpd_resp_sendstr_chunk(req, NULL);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file!");
            return ESP_FAIL;
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGD(TAG, "File successfully sent");

    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "upload_post_handler");
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    ESP_LOGI(TAG, "uri: %s", req->uri);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 413 Error */
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Invalid filename: " + string(filename));
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File already exists: " + string(filepath));
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File too large: " + to_string(req->content_len) + " bytes");
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "File size must be less than "
                            MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    fd = fopen(filepath, "w");
    if (!fd) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to create file: " + string(filepath));
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file: %s...", filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size: %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SERVER_FILER_SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "File saved: " + string(filename));
    ESP_LOGI(TAG, "File reception completed");

    string s = req->uri;
    if (isInString(s, "?md5")) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Calculate and return MD5 sum...");
        
        fd = fopen(filepath, "r");
        if (!fd) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to open file for reading: " + string(filepath));
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file for reading");
            return ESP_FAIL;
        }

        uint8_t result[16];
        string md5hex = "";
        string response = "{\"md5\":";
        char hex[3];

        md5File(fd, result);
        fclose(fd);

        for (int i = 0; i < sizeof(result); i++) {
            snprintf(hex, sizeof(hex), "%02x", result[i]);
            md5hex.append(hex);
        }

        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "MD5 of " + string(filepath) + ": " + md5hex);
        response.append("\"" + md5hex + "\"");
        response.append("}");

        httpd_resp_sendstr(req, response.c_str());
    }
    else {  // Return file server page
        std::string directory = std::string(filepath);
        size_t zw = directory.find("/");
        size_t found = zw;
        while (zw != std::string::npos)
        {
            zw = directory.find("/", found+1);  
            if (zw != std::string::npos)
                found = zw;
        }

        int start_fn = strlen(((struct file_server_data *)req->user_ctx)->base_path);
        ESP_LOGD(TAG, "Directory: %s, start_fn: %d, found: %d", directory.c_str(), start_fn, found);
        directory = directory.substr(start_fn, found - start_fn + 1);
        directory = "/fileserver" + directory;
    //    ESP_LOGD(TAG, "Directory danach 2: %s", directory.c_str());

        /* Redirect onto root to see the updated file list */
        if (strcmp(filename, "/config/config.ini") == 0 ||
            strcmp(filename, "/config/ref0.jpg") == 0 ||
            strcmp(filename, "/config/ref0_org.jpg") == 0 ||
            strcmp(filename, "/config/ref1.jpg") == 0 ||
            strcmp(filename, "/config/ref1_org.jpg") == 0 ||
            strcmp(filename, "/config/reference.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/reference.jpg") == 0 ) 
        { 
            httpd_resp_set_status(req, HTTPD_200); // Avoid reloading of folder content
        }
        else {
            httpd_resp_set_status(req, "303 See Other"); // Reload folder content after upload
        }

        httpd_resp_set_hdr(req, "Location", directory.c_str());
        httpd_resp_sendstr(req, "File uploaded successfully");
    }

    return ESP_OK;
}

/* Directories that must survive a "delete" from the file browser: the SD root and the folders the firmware itself
 * depends on. They are only ever cleared flat (files, no subfolders) and are never removed. */
static bool is_protected_tree(const std::string& _path)
{
    std::string p = _path;
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return (p == "/sdcard") || (p == "/sdcard/config") || (p == "/sdcard/html") || (p == "/sdcard/firmware");
}

/* Recursively delete everything below _directory (and the directory itself if _removeSelf). Files are removed in
 * small batches so memory use stays bounded for folders with thousands of files (e.g. the raw image log), and the
 * HTTP task yields between batches. wlan.ini is never touched. Returns the number of files deleted. */
static int delete_directory_recursive(const std::string& _directory, bool _removeSelf)
{
    const int BATCH = 64;
    int deleted = 0;
    struct dirent *entry;

    // Pass 1..n: delete files in batches until a pass makes no progress
    while (true) {
        std::vector<std::string> batch;
        DIR *dir = opendir(_directory.c_str());
        if (!dir) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to open dir: " + _directory);
            return deleted;
        }
        while (((entry = readdir(dir)) != NULL) && ((int)batch.size() < BATCH)) {
            if ((entry->d_type != DT_DIR) && (strcmp("wlan.ini", entry->d_name) != 0)) {
                batch.push_back(_directory + "/" + std::string(entry->d_name));
            }
        }
        closedir(dir);

        int done = 0;
        for (const std::string& f : batch) {
            if (unlink(f.c_str()) == 0) {
                done++;
            }
        }
        deleted += done;
        if (done == 0) {
            break;
        }
        vTaskDelay(1);   // let other tasks (and the idle task / watchdog) run
    }

    // Sub-directories (typically few): collect, then recurse
    std::vector<std::string> subdirs;
    DIR *dir = opendir(_directory.c_str());
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if ((entry->d_type == DT_DIR) && (strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
                subdirs.push_back(_directory + "/" + std::string(entry->d_name));
            }
        }
        closedir(dir);
    }
    for (const std::string& sd : subdirs) {
        deleted += delete_directory_recursive(sd, true);
    }

    if (_removeSelf) {
        if (rmdir(_directory.c_str()) != 0) {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Could not remove directory (not empty?): " + _directory);
        }
    }
    return deleted;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "delete_post_handler");
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

//////////////////////////////////////////////////////////////
    char _query[200];
    char _valuechar[30];    
    std::string fn = "/sdcard/firmware/";
    std::string _task;
    std::string directory;
    std::string zw; 

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (httpd_req_get_url_query_str(req, _query, 200) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);
        
        if (httpd_query_key_value(_query, "task", _valuechar, 30) == ESP_OK)
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "task is found: " + string(_valuechar));
            _task = std::string(_valuechar);
        }
    }

    if (_task.compare("deldircontent") == 0)
    {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                                req->uri  + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename) {
            /* Respond with 414 Error */
            httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
            return ESP_FAIL;
        }
        zw = std::string(filename);
        zw = zw.substr(0, zw.length()-1);
        directory = "/fileserver" + zw + "/";
        zw = "/sdcard" + zw;
        ESP_LOGD(TAG, "Directory to delete: %s", zw.c_str());

        if (is_protected_tree(zw)) {
            delete_all_in_directory(zw);   // legacy flat behaviour: files only, keep system sub-folders
        }
        else {
            int n = delete_directory_recursive(zw, false);   // contents incl. sub-folders, keep the folder itself
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleted contents of " + zw + " (" + std::to_string(n) + " files)");
        }
//        directory = std::string(filepath);
//        directory = "/fileserver" + directory;
        ESP_LOGD(TAG, "Location after delete directory content: %s", directory.c_str());
        /* Redirect onto root to see the updated file list */
//        httpd_resp_set_status(req, "303 See Other");
//        httpd_resp_set_hdr(req, "Location", directory.c_str());
//        httpd_resp_sendstr(req, "File deleted successfully");
//        return ESP_OK;        
    }
    else
    {
        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                                req->uri  + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
            return ESP_FAIL;
        }

        /* Filename cannot have a trailing '/' */
        if (filename[strlen(filename) - 1] == '/') {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Invalid filename: " + string(filename));
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        if (strcmp(filename, "wlan.ini") == 0) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to delete protected file : " + string(filename));
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Not allowed to delete wlan.ini");
            return ESP_FAIL;
        }

        if (stat(filepath, &file_stat) == -1) { // File does not exist
            /* This is ok, we would delete it anyway */
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "File does not exist: " + string(filename));
        }

        if ((stat(filepath, &file_stat) == 0) && S_ISDIR(file_stat.st_mode)) {
            /* A folder: remove it together with its contents (unlink() cannot delete directories) */
            if (is_protected_tree(std::string(filepath))) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to delete protected directory : " + string(filename));
                httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Not allowed to delete this directory");
                return ESP_FAIL;
            }
            int n = delete_directory_recursive(std::string(filepath), true);
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Directory deleted: " + string(filename) + " (" + std::to_string(n) + " files)");
        }
        else {
            /* Delete file */
            unlink(filepath);
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "File deleted: " + string(filename));
        }
        ESP_LOGI(TAG, "File deletion completed");

        directory = std::string(filepath);
        size_t zw = directory.find("/");
        size_t found = zw;
        while (zw != std::string::npos)
        {
            zw = directory.find("/", found+1);  
            if (zw != std::string::npos)
                found = zw;
        }

        int start_fn = strlen(((struct file_server_data *)req->user_ctx)->base_path);
        ESP_LOGD(TAG, "Directory: %s, start_fn: %d, found: %d", directory.c_str(), start_fn, found);
        directory = directory.substr(start_fn, found - start_fn + 1);
        directory = "/fileserver" + directory;
        ESP_LOGD(TAG, "Directory danach 4: %s", directory.c_str());
    
        //////////////////////////////////////////////////////////////

        /* Redirect onto root to see the updated file list */
        if (strcmp(filename, "/config/config.ini") == 0 ||
            strcmp(filename, "/config/ref0.jpg") == 0 ||
            strcmp(filename, "/config/ref0_org.jpg") == 0 ||
            strcmp(filename, "/config/ref1.jpg") == 0 ||
            strcmp(filename, "/config/ref1_org.jpg") == 0 ||
            strcmp(filename, "/config/reference.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref0_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1.jpg") == 0 ||
            strcmp(filename, "/img_tmp/ref1_org.jpg") == 0 ||
            strcmp(filename, "/img_tmp/reference.jpg") == 0 ) 
        { 
            httpd_resp_set_status(req, HTTPD_200); // Avoid reloading of folder content
        }
        else {
            httpd_resp_set_status(req, "303 See Other"); // Reload folder content after upload
        }
    }

    httpd_resp_set_hdr(req, "Location", directory.c_str());
    httpd_resp_sendstr(req, "File successfully deleted");
    return ESP_OK;
}

void delete_all_in_directory(const std::string& _directory)
{
    struct dirent *entry;
    DIR *dir = opendir(_directory.c_str());
    std::string filename;

    if (!dir) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to stat dir: " + _directory);
        return;
    }

    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL) {
        if (!(entry->d_type == DT_DIR)){
            if (strcmp("wlan.ini", entry->d_name) != 0){                    // wlan.ini should not be accessed !!!
                filename = _directory + "/" + std::string(entry->d_name);
                LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleting file: " + filename);
                /* Delete file */
                unlink(filename.c_str());    
            }
        };
    }
    closedir(dir);
}

std::string unzip_new(std::string _in_zip_file, std::string _html_tmp, std::string _html_final, std::string _target_bin, std::string _main, bool _initial_setup)
{
    int i, sort_iter;
    mz_bool status;
    size_t uncomp_size;
    mz_zip_archive zip_archive;
    void* p;
    std::string zw, ret = "";
    std::string directory = "";

    ESP_LOGD(TAG, "miniz.c version: %s", MZ_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s", _in_zip_file.c_str());

    // Now try to open the archive.
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mz_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0);
    if (!status)
    {
        ESP_LOGD(TAG, "mz_zip_reader_init_file() failed!");
        return ret;
    }

    // Get and print information about each file in the archive.
    int numberoffiles = (int)mz_zip_reader_get_num_files(&zip_archive);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Files to be extracted: " + to_string(numberoffiles));

    sort_iter = 0;
    {
        memset(&zip_archive, 0, sizeof(zip_archive));
        status = mz_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), sort_iter ? MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
        if (!status)
        {
            ESP_LOGD(TAG, "mz_zip_reader_init_file() failed!");
            return ret;
        }

        for (i = 0; i < numberoffiles; i++)
        {
            mz_zip_archive_file_stat file_stat;
            mz_zip_reader_file_stat(&zip_archive, i, &file_stat);

            if (!file_stat.m_is_directory) {
            // Extract by index (the previous code copied m_filename into a fixed char[64] via
            // sprintf-as-format-string: any name >= 64 chars or containing '%' overflowed/garbled).
            p = mz_zip_reader_extract_to_heap(&zip_archive, i, &uncomp_size, 0);
                if (!p)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "mz_zip_reader_extract_to_heap() failed on file " + string(file_stat.m_filename));
                    mz_zip_reader_end(&zip_archive);
                    return ret;
                }

                // Save to File.
                zw = std::string(file_stat.m_filename);
                ESP_LOGD(TAG, "Rohfilename: %s", zw.c_str());

                if (toUpper(zw) == "FIRMWARE.BIN")
                {
                    zw = _target_bin + zw;
                    ret = zw;
                }
                else
                {
                    std::string _dir = getDirectory(zw);
                    if ((_dir == "config-initial") && !_initial_setup)
                    {
                        continue;
                    }
                    else
                    {
                        _dir = "config";
                        std::string _s1 = "config-initial";
                        FindReplace(zw, _s1, _dir);
                    }

                    if (_dir.length() > 0)
                    {
                        zw = _main + zw;
                    }
                    else
                    {
                        zw = _html_tmp + zw;
                    }

                }

                // files in the html folder shall be redirected to the temporary html folder
                if (zw.find(_html_final) == 0) {
                    FindReplace(zw, _html_final, _html_tmp);
                }
            
                string filename_zw = zw + SUFFIX_ZW;

                ESP_LOGI(TAG, "File to extract: %s, Temp. Filename: %s", zw.c_str(), filename_zw.c_str());

                std::string folder = filename_zw.substr(0, filename_zw.find_last_of('/'));
                MakeDir(folder);

                // extrahieren in zwischendatei
                DeleteFile(filename_zw);

                FILE* fpTargetFile = fopen(filename_zw.c_str(), "wb");
                uint writtenbytes = fwrite(p, 1, (uint)uncomp_size, fpTargetFile);
                fclose(fpTargetFile);
                
                bool isokay = true;

                if (writtenbytes == (uint)uncomp_size)
                {
                    isokay = true;
                }
                else
                {
                    isokay = false;
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in writting extracted file (function fwrite) extracted file \"" +
                            string(file_stat.m_filename) + "\", size " + to_string(uncomp_size));
                }

                DeleteFile(zw);
                if (!isokay)
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in fwrite \"" + string(file_stat.m_filename) + "\", size " + to_string(uncomp_size));
                isokay = isokay && RenameFile(filename_zw, zw);
                if (!isokay)
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in Rename \"" + filename_zw + "\" to \"" + zw);

                if (isokay)
                    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Successfully extracted file \"" + string(file_stat.m_filename) + "\", size " + to_string(uncomp_size));
                else
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ERROR in extracting file \"" + string(file_stat.m_filename) + "\", size " + to_string(uncomp_size));
                    ret = "ERROR";
                }
                mz_free(p);
            }
        }

        // Close the archive, freeing any resources it was using
        mz_zip_reader_end(&zip_archive);
    }

    ESP_LOGD(TAG, "Success.");
    return ret;
}

void unzip(std::string _in_zip_file, std::string _target_directory){
    mz_zip_archive zip_archive;

    ESP_LOGD(TAG, "miniz.c version: %s", MZ_VERSION);
    ESP_LOGD(TAG, "Zipfile: %s -> %s", _in_zip_file.c_str(), _target_directory.c_str());

    memset(&zip_archive, 0, sizeof(zip_archive));
    if (!mz_zip_reader_init_file(&zip_archive, _in_zip_file.c_str(), 0))
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "unzip: mz_zip_reader_init_file() failed for " + _in_zip_file);
        return;
    }

    int numberoffiles = (int)mz_zip_reader_get_num_files(&zip_archive);
    int extracted = 0, failed = 0;

    for (int i = 0; i < numberoffiles; i++)
    {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat))
            continue;

        // Skip directory entries: extracting them to heap returns NULL, and the old code treated
        // that as fatal and aborted the whole archive (so any zip with sub-folders only extracted
        // the files listed before the first folder entry). The parent dirs are created on demand below.
        if (file_stat.m_is_directory)
            continue;

        // Extract by index (avoids the previous fixed 64-byte buffer + sprintf-as-format-string bug,
        // which truncated/garbled any name >= 64 chars).
        size_t uncomp_size = 0;
        void *p = mz_zip_reader_extract_to_heap(&zip_archive, i, &uncomp_size, 0);
        if (!p)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "unzip: extract failed for '" + std::string(file_stat.m_filename) + "' - skipping");
            failed++;
            continue;   // skip this entry, keep extracting the rest
        }

        std::string target = _target_directory + std::string(file_stat.m_filename);
        // Make sure the parent directory exists (e.g. img/, param-tooltips/).
        size_t slash = target.find_last_of('/');
        if (slash != std::string::npos)
            MakeDir(target.substr(0, slash));

        FILE *fpTargetFile = fopen(target.c_str(), "wb");
        if (fpTargetFile)
        {
            fwrite(p, 1, (uint)uncomp_size, fpTargetFile);
            fclose(fpTargetFile);
            extracted++;
        }
        else
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "unzip: cannot open '" + target + "' for writing");
            failed++;
        }
        mz_free(p);
    }

    mz_zip_reader_end(&zip_archive);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "unzip: extracted " + std::to_string(extracted) + " file(s)" +
            (failed ? (", " + std::to_string(failed) + " failed") : std::string("")) + " from " + _in_zip_file);
}

// ---- Config backups ---------------------------------------------------------------------------
// Timestamped copies of config.ini under /sdcard/config/backup/, so a configuration change can be
// rolled back. config_<epoch>.ini names sort lexically = chronologically. Keep only the latest 10.
#define CONFIG_INI_PATH    "/sdcard/config/config.ini"
#define CONFIG_BACKUP_DIR  "/sdcard/config/backup"
#define CONFIG_BACKUP_KEEP 10

static void pruneConfigBackups(int keep)
{
    std::vector<std::string> files;
    DIR *dir = opendir(CONFIG_BACKUP_DIR);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        std::string n = e->d_name;
        if (n.rfind("config_", 0) == 0 && n.size() >= 12 && n.substr(n.size() - 4) == ".ini")
            files.push_back(n);
    }
    closedir(dir);
    std::sort(files.begin(), files.end());   // epoch-padded names -> oldest first
    for (int i = 0; i + keep < (int)files.size(); ++i)
        DeleteFile(std::string(CONFIG_BACKUP_DIR) + "/" + files[i]);
}

// Snapshot the current config.ini into the backup dir, then prune to the latest CONFIG_BACKUP_KEEP.
static bool createConfigBackup()
{
    if (!FileExists(CONFIG_INI_PATH)) return false;
    MakeDir(CONFIG_BACKUP_DIR);
    char path[96];
    time_t now; time(&now);
    snprintf(path, sizeof(path), "%s/config_%010ld.ini", CONFIG_BACKUP_DIR, (long)now);
    bool ok = CopyFile(CONFIG_INI_PATH, path);
    if (ok) pruneConfigBackups(CONFIG_BACKUP_KEEP);
    return ok;
}

// GET /config_backup -> snapshot the current config.ini (called by the editor before saving when the
// "create backup" option is on).
esp_err_t config_backup_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    bool ok = createConfigBackup();
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, ok ? "Config backup created" : "Config backup: nothing to back up");
    httpd_resp_sendstr(req, ok ? "backup created" : "no config to back up");
    return ESP_OK;
}

// GET /config_backups -> newline-separated list of backup filenames, newest first.
esp_err_t config_backups_list_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    std::vector<std::string> files;
    DIR *dir = opendir(CONFIG_BACKUP_DIR);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            std::string n = e->d_name;
            if (n.rfind("config_", 0) == 0 && n.size() >= 12 && n.substr(n.size() - 4) == ".ini")
                files.push_back(n);
        }
        closedir(dir);
    }
    std::sort(files.rbegin(), files.rend());   // newest first
    std::string out;
    for (auto &f : files) out += f + "\n";
    httpd_resp_sendstr(req, out.c_str());
    return ESP_OK;
}

// GET /config_restore?file=config_<epoch>.ini -> snapshot the current config (so the restore is
// itself reversible), then overwrite config.ini with the chosen backup. Caller reboots to apply.
esp_err_t config_restore_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char query[160], fnbuf[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", fnbuf, sizeof(fnbuf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing 'file' parameter");
        return ESP_FAIL;
    }
    std::string fn = fnbuf;
    // sanitize: a plain backup basename only (no path traversal)
    if (fn.find('/') != std::string::npos || fn.find("..") != std::string::npos ||
        fn.rfind("config_", 0) != 0 || fn.size() < 12 || fn.substr(fn.size() - 4) != ".ini") {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid backup filename");
        return ESP_FAIL;
    }
    std::string src = std::string(CONFIG_BACKUP_DIR) + "/" + fn;
    if (!FileExists(src)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "backup not found");
        return ESP_FAIL;
    }
    createConfigBackup();   // snapshot the current config first so the restore can be undone
    bool ok = CopyFile(src, CONFIG_INI_PATH);
    LogFile.WriteToFile(ESP_LOG_WARN, TAG, ok ? ("Config restored from " + fn + " - reboot to apply")
                                              : ("Config restore from " + fn + " failed"));
    httpd_resp_sendstr(req, ok ? "restored - reboot to apply" : "restore failed");
    return ESP_OK;
}

void register_server_file_uri(httpd_handle_t server, const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    /* Validate file storage base path */
    if (!base_path) {
//    if (!base_path || strcmp(base_path, "/spiffs") != 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File server base_path not set");
//        return ESP_ERR_INVALID_ARG;
    }

    if (server_data) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File server already started");
//        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = (file_server_data *) calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate memory for server data");
//        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    /* URI handler for getting uploaded files */
//    char zw[sizeof(serverprefix)+1];
//    strcpy(zw, serverprefix);
//    zw[strlen(serverprefix)] = '*';
//    zw[strlen(serverprefix)+1] = '\0';    
//    ESP_LOGD(TAG, "zw: %s", zw);
    httpd_uri_t file_download = {
        .uri       = "/fileserver*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(download_get_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    /* Same handler for HEAD requests, so they report the real file size (Content-Length) instead of
     * falling through to the catch-all. */
    httpd_uri_t file_download_head = {
        .uri       = "/fileserver*",
        .method    = HTTP_HEAD,
        .handler   = APPLY_BASIC_AUTH_FILTER(download_get_handler),
        .user_ctx  = server_data
    };
    httpd_register_uri_handler(server, &file_download_head);

    httpd_uri_t file_datafileact = {
        .uri       = "/datafileact",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(datafileact_get_full_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_datafileact);

    httpd_uri_t file_datafile_last_part_handle = {
        .uri       = "/data",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(datafileact_get_last_part_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_datafile_last_part_handle);

    httpd_uri_t file_logfileact = {
        .uri       = "/logfileact",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(logfileact_get_full_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_logfileact);

    httpd_uri_t file_logfile_last_part_handle = {
        .uri       = "/log",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(logfileact_get_last_part_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_logfile_last_part_handle);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(upload_post_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method    = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(delete_post_handler),
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    /* Config backup / restore (timestamped config.ini copies, keep latest 10) */
    httpd_uri_t cfg_backup = { .uri = "/config_backup", .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(config_backup_handler), .user_ctx = server_data };
    httpd_register_uri_handler(server, &cfg_backup);
    httpd_uri_t cfg_backups = { .uri = "/config_backups", .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(config_backups_list_handler), .user_ctx = server_data };
    httpd_register_uri_handler(server, &cfg_backups);
    httpd_uri_t cfg_restore = { .uri = "/config_restore", .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(config_restore_handler), .user_ctx = server_data };
    httpd_register_uri_handler(server, &cfg_restore);
}
