#include "server_ota.h"

#include <string>
#include "string.h"

/* TODO Rethink the usage of the int watchdog. It is no longer to be used, see
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/migration-guides/release-5.x/5.0/system.html?highlight=esp_int_wdt */
#include "esp_private/esp_int_wdt.h"

#include <esp_task_wdt.h>


#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include <esp_ota_ops.h>
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include <nvs.h>
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
// #include "protocol_examples_common.h"
#include "errno.h"

#include <sys/stat.h>

#include "MainFlowControl.h"
#include "server_file.h"
#include "server_GPIO.h"
#ifdef ENABLE_MQTT
    #include "interface_mqtt.h"
#endif //ENABLE_MQTT
#include "ClassControllCamera.h"
#include "connect_wlan.h"


#include "ClassLogFile.h"

#include "Helper.h"
#include "statusled.h"
#include "basic_auth.h"
#include "../../include/defines.h"

/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[SERVER_OTA_SCRATCH_BUFSIZE + 1] = { 0 };

static const char *TAG = "OTA";

esp_err_t handler_reboot(httpd_req_t *req);
static bool ota_update_task(std::string fn);

std::string _file_name_update;
bool initial_setup = false;


static void infinite_loop(void)
{
    int i = 0;
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "When a new firmware is available on the server, press the reset button to download it");
    while(1) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Waiting for a new firmware... (" + to_string(++i) + ")");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


void task_do_Update_ZIP(void *pvParameter)
{
    StatusLED(AP_OR_OTA, 1, true);  // Signaling an OTA update
    
    std::string filetype = toUpper(getFileType(_file_name_update));

  	LogFile.WriteToFile(ESP_LOG_INFO, TAG, "File: " + _file_name_update + " Filetype: " + filetype);

    if (filetype == "ZIP")
    {
        std::string in, outHtml, outHtmlTmp, outHtmlOld, outbin, zw, retfirmware;

        outHtml = "/sdcard/html";
        outHtmlTmp = "/sdcard/html_tmp";
        outHtmlOld = "/sdcard/html_old";
        outbin = "/sdcard/firmware";

        /* Remove the old and tmp html folder in case they still exist */
        removeFolder(outHtmlTmp.c_str(), TAG);
        removeFolder(outHtmlOld.c_str(), TAG);

        /* Extract the ZIP file. The content of the html folder gets extracted to the temporar folder html-temp. */
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Extracting ZIP file " + _file_name_update + "...");
        retfirmware = unzip_new(_file_name_update, outHtmlTmp+"/", outHtml+"/", outbin+"/", "/sdcard/", initial_setup);
    	LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Files unzipped.");

        /* ZIP file got extracted, replace the old html folder with the new one */
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Renaming folder " + outHtml + " to " + outHtmlOld + "...");
        RenameFolder(outHtml, outHtmlOld);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Renaming folder " + outHtmlTmp + " to " + outHtml + "...");
        RenameFolder(outHtmlTmp, outHtml);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Deleting folder " + outHtmlOld + "...");
        removeFolder(outHtmlOld.c_str(), TAG);

        if (retfirmware.length() > 0)
        {
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Found firmware.bin");
            ota_update_task(retfirmware);
        }

        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Trigger reboot due to firmware update");
        doRebootOTA();
    } else if (filetype == "BIN")
    {
       	LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Do firmware update - file: " + _file_name_update);
        ota_update_task(_file_name_update);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Trigger reboot due to firmware update");
        doRebootOTA();
    }
    else
    {
    	LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Only ZIP-Files support for update during startup!");
    }
}


void CheckUpdate()
{
 	FILE *pfile;
    if ((pfile = fopen("/sdcard/update.txt", "r")) == NULL)
    {
		LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "No pending update");
        return;
	}

	char zw[1024] = "";
	fgets(zw, 1024, pfile);
    _file_name_update = std::string(zw);
    if (fgets(zw, 1024, pfile))
	{
		std::string _szw = std::string(zw);
        if (_szw == "init")
        {
       		LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Inital Setup triggered");
        }
	}

    fclose(pfile);
    DeleteFile("/sdcard/update.txt");   // Prevent Boot Loop!!!
	LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Start update process (" + _file_name_update + ")");


    xTaskCreate(&task_do_Update_ZIP, "task_do_Update_ZIP", configMINIMAL_STACK_SIZE * 35, NULL, tskIDLE_PRIORITY+1, NULL);
    while(1) { // wait until reboot within task_do_Update_ZIP
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


static bool ota_update_task(std::string fn)
{
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA update");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {        
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Configured OTA boot partition at offset " + to_string(configured->address) + 
                ", but running from offset " + to_string(running->address));
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "(This can happen if either the OTA boot data or preferred boot image become somehow corrupted.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, (unsigned int)running->address);


    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, (unsigned int)update_partition->address);
//    assert(update_partition != NULL);

    int binary_file_length = 0;

    // deal with all receive packet 
    bool image_header_was_checked = false;

    int data_read;     

    FILE* f = fopen(fn.c_str(), "rb");     // previously only "r

    if (f == NULL) { // File does not exist
        return false;
    }

    data_read = fread(ota_write_data, 1, SERVER_OTA_SCRATCH_BUFSIZE, f);

    while (data_read > 0) {
        if (data_read < 0) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Error: SSL data read error");
            return false;
        } else if (data_read > 0) {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "New version is the same as invalid version");
                            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Previously, there was an attempt to launch the firmware with " + 
                                    string(invalid_app_info.version) + " version, but it failed");
                            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "The firmware has been rolled back to the previous version");
                            infinite_loop();
                        }
                    }

/*
                    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
                        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Current running version is the same as a new. We will not continue the update");
                        infinite_loop();
                    }
*/
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                    if (err != ESP_OK) {
                        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_ota_begin failed (" + string(esp_err_to_name(err)) + ")");
                        return false;
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } else {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "received package is not fit len");
                    return false;
                }
            }            
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                return false;
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        } else if (data_read == 0) {
           //
           // * As esp_http_client_read never returns negative error code, we rely on
           // * `errno` to check for underlying transport connectivity closure if any
           //
            if (errno == ECONNRESET || errno == ENOTCONN) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection closed, errno = " + to_string(errno));
                break;
            }
        }
        data_read = fread(ota_write_data, 1, SERVER_OTA_SCRATCH_BUFSIZE, f);
    }
    fclose(f);  

    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Image validation failed, image is corrupted");
        }
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_ota_end failed (" + string(esp_err_to_name(err)) + ")!");
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_ota_set_boot_partition failed (" + string(esp_err_to_name(err)) + ")!");

    }
//    ESP_LOGI(TAG, "Prepare to restart system!");
//    esp_restart();

    return true ;
}


static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}


// Validate a freshly-OTA'd firmware before the bootloader commits to it. Returns false to trigger a
// rollback to the previous app. Called from ConfirmOTAUpdateAfterInit() *after* the web server is up,
// so simply reaching it already means the app didn't crash-loop through init (the main protection -
// a crash before this point never confirms, and the bootloader rolls back). On top of that, reject the
// two flags that mean the new firmware fundamentally cannot run on this hardware (no usable PSRAM /
// too little heap) so a broken build rolls back instead of sitting bricked. Flags that reflect the
// SD card / camera / network (which a re-flash of the same image wouldn't fix, and which still leave a
// serving web UI to recover from) are intentionally NOT grounds for rollback.
static bool diagnostic(void)
{
    if (isSetSystemStatusFlag(SYSTEM_STATUS_PSRAM_BAD) || isSetSystemStatusFlag(SYSTEM_STATUS_HEAP_TOO_SMALL)) {
        return false;
    }
    return true;
}


void CheckOTAUpdate(void)
{
    ESP_LOGI(TAG, "Start CheckOTAUpdateCheck...");

    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    // If this is a post-OTA *trial* boot (PENDING_VERIFY), only LOG it here - do NOT confirm the image
    // yet. Confirmation is deferred to ConfirmOTAUpdateAfterInit() once the device has proven it can
    // finish initialization (web server up). If it instead crash-loops before that point, the
    // bootloader (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) rolls back to the previous working app.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Running a freshly updated firmware on trial - it will be "
            "confirmed once initialization completes; if it crash-loops before then, the bootloader "
            "rolls back to the previous version.");
    }
}


// Called AFTER initialization completes (web server + handlers up). On a post-OTA trial boot, run the
// diagnostic and either commit the new app (cancel the pending rollback) or mark it invalid and reboot
// into the previous app. No-op on a normal boot, or on a board with no OTA partitions (the WROVER
// factory slot is never PENDING_VERIFY). Requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE to do anything.
void ConfirmOTAUpdateAfterInit(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
        return; // normal boot (already valid) or no OTA state -> nothing to confirm
    }

    if (diagnostic()) {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "OTA trial firmware passed diagnostics - confirming the update (rollback cancelled).");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "OTA trial firmware failed diagnostics (PSRAM/heap) - rolling back to the previous version!");
        esp_ota_mark_app_invalid_rollback_and_reboot(); // reboots; bootloader boots the previous app
    }
}


esp_err_t handler_ota_update(httpd_req_t *req)
{
#ifdef DEBUG_DETAIL_ON     
    LogFile.WriteHeapInfo("handler_ota_update - Start");    
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handler_ota_update");
    char _query[200];
    char _filename[100];
    char _valuechar[30];    
    std::string fn = "/sdcard/firmware/";
    bool _file_del = false;
    std::string _task = "";

    if (httpd_req_get_url_query_str(req, _query, 200) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);
        
        if (httpd_query_key_value(_query, "task", _valuechar, 30) == ESP_OK)
        {
            ESP_LOGD(TAG, "task is found: %s", _valuechar);
            _task = std::string(_valuechar);
        }

        if (httpd_query_key_value(_query, "file", _filename, 100) == ESP_OK)
        {
            fn.append(_filename);
            ESP_LOGD(TAG, "File: %s", fn.c_str());
        }
        if (httpd_query_key_value(_query, "delete", _filename, 100) == ESP_OK)
        {
            fn.append(_filename);
            _file_del = true;
            ESP_LOGD(TAG, "Delete Default File: %s", fn.c_str());
        }

    }

    if (_task.compare("emptyfirmwaredir") == 0)
    {
        ESP_LOGD(TAG, "Start empty directory /firmware");
        delete_all_in_directory("/sdcard/firmware");
        std::string zw = "firmware directory deleted - v2\n";
        ESP_LOGD(TAG, "%s", zw.c_str());
        printf("Ausgabe: %s\n", zw.c_str());
    
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, zw.c_str(), strlen(zw.c_str())); 
        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(req, NULL, 0);  

        ESP_LOGD(TAG, "Done empty directory /firmware");
        return ESP_OK;
    }

    if (_task.compare("update") == 0)
    {
        std::string filetype = toUpper(getFileType(fn));
        if (filetype.length() == 0)
        {
            std::string zw = "Update failed - no file specified (zip, bin, tfl, tlite)";
            httpd_resp_sendstr_chunk(req, zw.c_str());
            httpd_resp_sendstr_chunk(req, NULL);  
            return ESP_OK;        
        }

        if ((filetype == "TFLITE") || (filetype == "TFL"))
        {
            std::string out = "/sdcard/config/" + getFileFullFileName(fn);
            DeleteFile(out);
            CopyFile(fn, out);
            DeleteFile(fn);

            const char*  resp_str = "Neural Network File copied.";
            httpd_resp_sendstr_chunk(req, resp_str);
            httpd_resp_sendstr_chunk(req, NULL);  
            return ESP_OK;
        }


        if ((filetype == "ZIP") || (filetype == "BIN"))
        {
           	FILE *pfile;
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Update for reboot");
            pfile = fopen("/sdcard/update.txt", "w");
            fwrite(fn.c_str(), fn.length(), 1, pfile);
            fclose(pfile);

            std::string zw = "reboot\n";
            httpd_resp_sendstr_chunk(req, zw.c_str());
            httpd_resp_sendstr_chunk(req, NULL);  
            ESP_LOGD(TAG, "Send reboot");
            return ESP_OK;                

        }

/*
        if (filetype == "BIN")
        {
            const char* resp_str; 

            DeleteMainFlowTask();
            gpio_handler_deinit();
            if (ota_update_task(fn))
            {
                std::string zw = "reboot\n";
                httpd_resp_sendstr_chunk(req, zw.c_str());
                httpd_resp_sendstr_chunk(req, NULL);  
                ESP_LOGD(TAG, "Send reboot");
                return ESP_OK;                
            }

            resp_str = "Error during Firmware Update!!!\nPlease check output of console.";
            httpd_resp_send(req, resp_str, strlen(resp_str));  

            #ifdef DEBUG_DETAIL_ON 
                LogFile.WriteHeapInfo("handler_ota_update - Done");    
            #endif

            return ESP_OK;
        }
*/

        std::string zw = "Update failed - no valid file specified (zip, bin, tfl, tlite)!";
        httpd_resp_sendstr_chunk(req, zw.c_str());
        httpd_resp_sendstr_chunk(req, NULL);  
        return ESP_OK;        
    }


    if (_task.compare("unziphtml") == 0)
    {
        ESP_LOGD(TAG, "Task unziphtml");
        std::string in, out, zw;

        in = "/sdcard/firmware/html.zip";
        out = "/sdcard/html";

        delete_all_in_directory(out);

        unzip(in, out+"/");
        zw = "Web Interface Update Successfull!\nNo reboot necessary";
        httpd_resp_send(req, zw.c_str(), strlen(zw.c_str()));
        httpd_resp_sendstr_chunk(req, NULL);  
        return ESP_OK;        
    }

    if (_file_del)
    {
        ESP_LOGD(TAG, "Delete !! _file_del: %s", fn.c_str());
        struct stat file_stat;
        int _result = stat(fn.c_str(), &file_stat);
        ESP_LOGD(TAG, "Ergebnis %d\n", _result);
        if (_result == 0) {
            ESP_LOGD(TAG, "Deleting file: %s", fn.c_str());
            /* Delete file */
            unlink(fn.c_str());
        }
        else
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "File does not exist: " + fn);
        }
        /* Respond with an empty chunk to signal HTTP response completion */
        std::string zw = "file deleted\n";
        ESP_LOGD(TAG, "%s", zw.c_str());
        httpd_resp_send(req, zw.c_str(), strlen(zw.c_str()));
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    string zw = "ota without parameter - should not be the case!";
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, zw.c_str(), strlen(zw.c_str())); 
    httpd_resp_send_chunk(req, NULL, 0);  

    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ota without parameter - should not be the case!");

/*  
    const char* resp_str;    

    DeleteMainFlowTask();
    gpio_handler_deinit();
    if (ota_update_task(fn))
    {
        resp_str = "Firmware Update Successfull! You can restart now.";
    }
    else
    {
        resp_str = "Error during Firmware Update!!! Please check console output.";
    }

    httpd_resp_send(req, resp_str, strlen(resp_str));  
*/

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("handler_ota_update - Done");    
    #endif

    return ESP_OK;
}


void hard_restart() 
{
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 1,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
    .trigger_panic = true,
  };
  ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));

  esp_task_wdt_add(NULL);
  while(true);
}


// Failsafe so a stuck reboot can never wedge the device: a hung flow task can hold the SD lock, which
// makes the fopen("/sdcard/reboot.txt") and log-flush in the reboot path below block forever, so
// esp_restart() is never reached and the board hangs until it is physically power-cycled. This
// independent task forces the restart after a hard timeout regardless of what the reboot path is
// blocked on. esp_restart() is a hardware reset that does not touch the SD/VFS locks; hard_restart()
// (watchdog panic) is the backstop in the unlikely event esp_restart() itself stalls.
static void reboot_failsafe_task(void *arg)
{
    const int timeout_ms = (int)(intptr_t)arg;
    vTaskDelay(timeout_ms / portTICK_PERIOD_MS);
    ESP_LOGE(TAG, "Reboot failsafe (%d ms) fired - forcing restart (reboot path likely blocked on SD lock)", timeout_ms);
    esp_restart();
    hard_restart();
}

// Start the reboot failsafe (call at the top of any reboot path that touches the SD card before resetting).
static void start_reboot_failsafe(int timeout_ms)
{
    xTaskCreate(&reboot_failsafe_task, "reboot_failsafe", configMINIMAL_STACK_SIZE * 2,
                (void*)(intptr_t)timeout_ms, configMAX_PRIORITIES - 2, NULL);
}

void task_reboot(void *DeleteMainFlow)
{
    // Guarantee a restart even if the cleanup below blocks on a held SD lock (see reboot_failsafe_task).
    start_reboot_failsafe(10000);

    // write a reboot, to identify a reboot by purpouse
    FILE* pfile = fopen("/sdcard/reboot.txt", "w");
    if (pfile != NULL) {
        std::string _s_zw= "reboot";
        fwrite(_s_zw.c_str(), strlen(_s_zw.c_str()), 1, pfile);
        fclose(pfile);
    }

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    if ((bool)DeleteMainFlow) {
        DeleteMainFlowTask();  // Kill autoflow task if executed in extra task, if not don't kill parent task
    }

    Camera.LightOnOff(false);
    StatusLEDOff();

    /* Stop service tasks */
    #ifdef ENABLE_MQTT
        MQTTdestroy_client(true);
    #endif //ENABLE_MQTT
    gpio_handler_destroy();
    esp_camera_deinit();
    WIFIDestroy();

    // Persist any buffered log lines before the SD card goes away with the reset.
    LogFile.FlushLogBuffer();

    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();      // Reset type: CPU reset (Reset both CPUs)

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    hard_restart();     // Reset type: System reset (Triggered by watchdog), if esp_restart stalls (WDT needs to be activated)

    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Reboot failed!");
    vTaskDelete(NULL); //Delete this task if it comes to this point
}


void doReboot()
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Reboot triggered by Software (5s)");
    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Reboot in 5sec");

    BaseType_t xReturned = xTaskCreate(&task_reboot, "task_reboot", configMINIMAL_STACK_SIZE * 4, (void*) true, 10, NULL);
    if( xReturned != pdPASS )
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "task_reboot not created -> force reboot without killing flow");
        task_reboot((void*) false);
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Prevent serving web client fetch response until system is shuting down
}


void doRebootOTA()
{
    // Guarantee a restart even if the log flush below blocks on a held SD lock (see reboot_failsafe_task).
    start_reboot_failsafe(10000);

    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Reboot in 5sec");

    Camera.LightOnOff(false);
    StatusLEDOff();
    esp_camera_deinit();
    LogFile.FlushLogBuffer();   // persist buffered log lines before the reset

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();      // Reset type: CPU reset (Reset both CPUs)

    vTaskDelay(5000 / portTICK_PERIOD_MS);
    hard_restart();     // Reset type: System reset (Triggered by watchdog), if esp_restart stalls (WDT needs to be activated)
}


esp_err_t handler_reboot(httpd_req_t *req)
{
    #ifdef DEBUG_DETAIL_ON     
        LogFile.WriteHeapInfo("handler_reboot - Start");
    #endif    

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handler_reboot");
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "!!! System will restart within 5 sec!!!");

    // Board-aware reboot estimate + poll cadence: the ESP32-S3 is back in ~10s, while the ESP32-CAM
    // is slower (camera + SD init). Detect the chip at runtime so the "rebooting" page sets the right
    // expectation and starts polling sooner on fast boards.
    esp_chip_info_t _chip; esp_chip_info(&_chip);
    bool _isS3 = (_chip.model == CHIP_ESP32S3);
    std::string _est   = _isS3 ? "about 20 seconds" : "about 25-40 seconds";
    std::string _first = _isS3 ? "2500" : "6000";   // ms before the first heartbeat poll
    std::string _ivl   = _isS3 ? "1000" : "1500";   // ms between polls

    std::string response =
        "<!DOCTYPE html><html lang='en'><head>"
            "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<style>"
                "body{font-family:arial;margin:0;min-height:100vh;display:flex;align-items:center;"
                     "justify-content:center;background:#fff;color:#1c2024;}"
                "html[data-theme=dark] body{background:#1e1e1e;color:#dcdcdc;}"
                ".card{text-align:center;padding:24px;}"
                ".spinner{width:64px;height:64px;margin:0 auto 20px;border:6px solid rgba(127,127,127,.25);"
                         "border-top-color:#2c7be5;border-radius:50%;animation:spin .9s linear infinite;}"
                "@keyframes spin{to{transform:rotate(360deg);}}"
                "h3{margin:0 0 6px;font-weight:600;}"
                ".sub{color:#888;font-size:.9em;margin:0 0 16px;}"
                ".bar{width:240px;max-width:80vw;height:6px;border-radius:3px;"
                     "background:rgba(127,127,127,.2);margin:0 auto;overflow:hidden;}"
                ".bar>i{display:block;height:100%;width:35%;border-radius:3px;background:#2c7be5;"
                       "animation:slide 1.4s ease-in-out infinite;}"
                "@keyframes slide{0%{margin-left:-35%}100%{margin-left:100%}}"
                ".ok{color:#1f9d55;font-weight:600;}"
            "</style>"
            "<script>"
                // honour the user's saved theme (same key theme.js uses), with an OS fallback
                "try{var s=localStorage.getItem('aiotedge-theme');"
                "var d=s?(s==='dark'):(window.matchMedia&&window.matchMedia('(prefers-color-scheme: dark)').matches);"
                "if(d)document.documentElement.setAttribute('data-theme','dark');}catch(e){}"
                "var n=0,down=false;"   // only reload after the device has gone down AND come back
                "function done(){var m=document.getElementById('msg');"
                    "m.textContent='Back online - reloading...';m.className='ok';"
                    "var t=(window.top&&window.top!==window)?window.top:window;t.location.href='index.html';}"
                "function ping(){n++;"
                    "fetch('reboot_page.html?_='+Date.now(),{cache:'no-store'})"
                    ".then(function(r){if(r&&r.ok){if(down){done();}else{again();}}else{down=true;again();}})"
                    ".catch(function(){down=true;again();});}"
                "function again(){if(n<120){setTimeout(ping," + _ivl + ");}else{"
                    "document.getElementById('msg').textContent='Still waiting - try reloading manually.';}}"
                // The device is definitely restarting a few seconds in, so flag it 'down' even if our
                // polls happened to miss the brief offline window - the next OK then reloads (no stall).
                "setTimeout(function(){down=true;},8000);"
                "window.addEventListener('load',function(){setTimeout(ping," + _first + ");});"
            "</script>"
            "</head><body><div class='card'>"
                "<div class='spinner'></div>"
                "<h3>Rebooting...</h3>"
                "<p class='sub' id='msg'>This usually takes " + _est + ".</p>"
                "<div class='bar'><i></i></div>"
            "</div></body></html>";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response.c_str(), strlen(response.c_str()));
    
    doReboot();

    #ifdef DEBUG_DETAIL_ON 
        LogFile.WriteHeapInfo("handler_reboot - Done");    
    #endif

    return ESP_OK;
}


void register_server_ota_sdcard_uri(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering URI handlers");
    
    httpd_uri_t camuri = { };
    camuri.method    = HTTP_GET;
    camuri.uri       = "/ota";
    camuri.handler = APPLY_BASIC_AUTH_FILTER(handler_ota_update);
    camuri.user_ctx  = (void*) "Do OTA";    
    httpd_register_uri_handler(server, &camuri);

    camuri.method    = HTTP_GET;
    camuri.uri       = "/reboot";
    camuri.handler = APPLY_BASIC_AUTH_FILTER(handler_reboot);
    camuri.user_ctx  = (void*) "Reboot";    
    httpd_register_uri_handler(server, &camuri);

}
