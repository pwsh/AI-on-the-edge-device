#include "server_cleanup.h"

#include "ClassLogFile.h"
#include "basic_auth.h"
#include "esp_log.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <set>
#include <vector>

static const char *TAG = "cleanup";

#define SDCARD_ROOT      "/sdcard"
#define DEPLOY_MANIFEST  "/sdcard/html/deployment.lst"

// ---- Classification ---------------------------------------------------------
// A path is "protected" (never listed, never deleted) when it is user data or runtime data that the
// backup preserves: the whole /config tree (config.ini, reference images, align.txt, prevalue, certs
// AND all CNN models incl. custom ones), /backup, /log, and /wlan.ini. Everything that is left and
// is NOT in the deployment manifest is a cleanup candidate (stale web-UI files from older versions,
// orphaned/renamed files, debug artifacts, ...).
static bool isProtectedRel(const std::string &rel)
{
    if (rel == "wlan.ini") return true;
    if (rel.rfind("config/", 0) == 0) return true;   // config + reference images + ALL models + certs
    if (rel.rfind("backup/", 0) == 0) return true;    // user backups
    if (rel.rfind("log/",    0) == 0) return true;    // logs (managed by the log viewer/retention)
    // The manifest itself and the version marker are always part of the deployment.
    if (rel == "html/deployment.lst") return true;
    if (rel == "html/version.txt")    return true;
    return false;
}

// Top-level directories under /sdcard that are protected entirely - skip walking them.
static bool isProtectedDir(const std::string &name)
{
    return name == "config" || name == "backup" || name == "log";
}

// Load the deployment manifest (relative paths from /sdcard, one per line) into a set.
static bool loadManifest(std::set<std::string> &out)
{
    FILE *f = fopen(DEPLOY_MANIFEST, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        // trim trailing CR/LF/space
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        if (!s.empty()) out.insert(s);
    }
    fclose(f);
    return true;
}

// Recursively collect cleanup-candidate files (relative paths + sizes) under fsDir / relPrefix.
static void collectCandidates(const std::string &fsDir, const std::string &relPrefix,
                              const std::set<std::string> &manifest,
                              std::vector<std::pair<std::string, long>> &out)
{
    DIR *d = opendir(fsDir.c_str());
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string fs  = fsDir + "/" + name;
        std::string rel = relPrefix.empty() ? name : (relPrefix + "/" + name);
        struct stat st;
        if (stat(fs.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (relPrefix.empty() && isProtectedDir(name)) continue;   // skip /config /backup /log
            collectCandidates(fs, rel, manifest, out);
        } else {
            if (isProtectedRel(rel)) continue;
            if (manifest.count(rel) > 0) continue;   // part of the current deployment
            out.push_back(std::make_pair(rel, (long)st.st_size));
        }
    }
    closedir(d);
}

// ---- GET /cleanup?task=list -------------------------------------------------
static esp_err_t handler_cleanup_list(httpd_req_t *req)
{
    std::set<std::string> manifest;
    bool haveManifest = loadManifest(manifest);

    std::vector<std::pair<std::string, long>> cand;
    if (haveManifest) {
        collectCandidates(SDCARD_ROOT, "", manifest, cand);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    std::string json = "{\"manifest\":" + std::string(haveManifest ? "true" : "false") +
                       ",\"count\":" + std::to_string(cand.size()) + ",\"files\":[";
    for (size_t i = 0; i < cand.size(); ++i) {
        json += "{\"path\":\"" + cand[i].first + "\",\"size\":" + std::to_string(cand[i].second) + "}";
        if (i + 1 < cand.size()) json += ",";
    }
    json += "]}";
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

// ---- POST /cleanup?task=delete  (body: relative paths, newline-separated) ----
static esp_err_t handler_cleanup_delete(httpd_req_t *req)
{
    // Read the body
    std::string body;
    char buf[512];
    int total = req->content_len, got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf, sizeof(buf) < (size_t)(total - got) ? sizeof(buf) : (total - got));
        if (r <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed"); return ESP_FAIL; }
        body.append(buf, r);
        got += r;
    }

    std::set<std::string> manifest;
    loadManifest(manifest);

    int deleted = 0, skipped = 0;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t nl = body.find('\n', pos);
        std::string rel = body.substr(pos, (nl == std::string::npos ? body.size() : nl) - pos);
        pos = (nl == std::string::npos) ? body.size() + 1 : nl + 1;
        while (!rel.empty() && (rel.back() == '\r' || rel.back() == ' ')) rel.pop_back();
        while (!rel.empty() && (rel.front() == ' ')) rel.erase(0, 1);
        if (rel.empty()) continue;

        // Server-side re-validation: refuse anything that is protected, in the deployment, escapes
        // /sdcard, or is an absolute path. Only genuine candidates can be deleted.
        if (rel.find("..") != std::string::npos || rel.front() == '/' ||
            isProtectedRel(rel) || manifest.count(rel) > 0) {
            skipped++;
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Cleanup refused (protected/invalid): " + rel);
            continue;
        }
        std::string fs = std::string(SDCARD_ROOT) + "/" + rel;
        if (remove(fs.c_str()) == 0) {
            deleted++;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Cleanup deleted: " + rel);
        } else {
            skipped++;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    std::string json = "{\"deleted\":" + std::to_string(deleted) + ",\"skipped\":" + std::to_string(skipped) + "}";
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

static esp_err_t handler_cleanup(httpd_req_t *req)
{
    char q[32] = {0};
    char val[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "task", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "delete") == 0) return handler_cleanup_delete(req);
        if (strcmp(val, "list")   == 0) return handler_cleanup_list(req);
    }
    return handler_cleanup_list(req);   // default
}

void register_server_cleanup_uri(httpd_handle_t server)
{
    httpd_uri_t u = { };
    u.uri      = "/cleanup";
    u.method   = HTTP_GET;
    u.handler  = APPLY_BASIC_AUTH_FILTER(handler_cleanup);
    u.user_ctx = (void *) "Cleanup";
    httpd_register_uri_handler(server, &u);

    u.method   = HTTP_POST;   // task=delete is POSTed (with the path list as body)
    httpd_register_uri_handler(server, &u);
}
