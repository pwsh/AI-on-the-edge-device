#include "basic_auth.h"
#include "read_wlanini.h"
#include "ClassLogFile.h"
#include <esp_tls_crypto.h>
#include <esp_log.h>
#include <string>


#define HTTPD_401 "401 UNAUTHORIZED"

static const char *TAG = "HTTPAUTH";

typedef struct {
    const char *username;
    const char *password;
} basic_auth_info_t;

basic_auth_info_t basic_auth_info = { NULL, NULL };

// Own copies of the credentials: init_basic_auth() can be re-run after wlan.ini is rewritten
// (web-password settings apply live), and wlan_config's strings may reallocate on reload -
// raw c_str() pointers into them would dangle.
static std::string auth_username;
static std::string auth_password;

bool basic_auth_enabled() {
    return basic_auth_info.username != NULL && basic_auth_info.password != NULL;
}

void init_basic_auth() {
    bool haveCreds = !wlan_config.http_username.empty() && !wlan_config.http_password.empty();
    // The explicit http_auth switch wins. When it is not set (legacy wlan.ini without the key),
    // the presence of both credentials enables auth, as it always did.
    bool enabled = (wlan_config.http_auth == -1) ? haveCreds : (wlan_config.http_auth == 1);

    if (enabled && !haveCreds) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                "http_auth is enabled but http_username/http_password are not both set - web password stays DISABLED");
        enabled = false;
    }

    if (enabled) {
        auth_username = wlan_config.http_username;
        auth_password = wlan_config.http_password;
        basic_auth_info.username = auth_username.c_str();
        basic_auth_info.password = auth_password.c_str();
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Web password protection ENABLED (user '" + auth_username + "')");
    } else {
        basic_auth_info.username = NULL;
        basic_auth_info.password = NULL;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Web password protection disabled");
    }
}

static char *http_auth_basic(const char *username, const char *password)
{
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = static_cast<char*>(calloc(1, 6 + n + 1));
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

esp_err_t basic_auth_request_filter(httpd_req_t *req, esp_err_t original_handler(httpd_req_t *))
{
    char *buf = NULL;
    size_t buf_len = 0;
    esp_err_t ret = ESP_OK;

    char unauthorized[] = "You are not authorized to use this website!";

    if (basic_auth_info.username == NULL || basic_auth_info.password == NULL) {
        ret = original_handler(req);
    } else {
        buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
        if (buf_len > 1) {
            buf = static_cast<char*>(calloc(1, buf_len));
            if (!buf) {
                ESP_LOGE(TAG, "No enough memory for basic authorization");
                return ESP_ERR_NO_MEM;
            }

            if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
                ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
            } else {
                ESP_LOGE(TAG, "No auth value received");
            }

            char *auth_credentials = http_auth_basic(basic_auth_info.username, basic_auth_info.password);
            if (!auth_credentials) {
                ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
                free(buf);
                return ESP_ERR_NO_MEM;
            }

            if (strncmp(auth_credentials, buf, buf_len)) {
                ESP_LOGE(TAG, "Not authenticated");
                httpd_resp_set_status(req, HTTPD_401);
                httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
                httpd_resp_set_hdr(req, "Connection", "keep-alive");
                httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"AIOTED\"");
                httpd_resp_send(req, unauthorized, strlen(unauthorized));
            } else {
                ESP_LOGI(TAG, "Authenticated calling http handler now!");
                ret=original_handler(req);
            }
            free(auth_credentials);
            free(buf);
        } else {
            ESP_LOGE(TAG, "No auth header received");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"AIOTED\"");
            httpd_resp_send(req, unauthorized, strlen(unauthorized));
        }
    }

    return ret;
}
