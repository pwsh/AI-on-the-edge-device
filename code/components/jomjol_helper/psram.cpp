#include "ClassLogFile.h"
#include "../../include/defines.h"
#include "psram.h"

static const char* TAG = "PSRAM";

using namespace std;


void *shared_region = NULL;
size_t shared_region_size = 0;   // actual allocated size (max of the digitization and image-step needs)
uint32_t allocatedBytesForSTBI = 0;
uint32_t peakBytesForSTBI = 0;   // MEM-PROFILE: high-water mark of shared-region use during TakeImage
std::string sharedMemoryInUseFor = "";

// The TakeImage/Aligning step decodes one full RGB image (IMAGE_SIZE) into the shared region
// (measured peak ~921 KB). Floor the region at IMAGE_SIZE + a margin so shrinking MAX_MODEL_SIZE can
// never starve image capture.
#define SHARED_REGION_IMAGE_FLOOR  (IMAGE_SIZE + (128 * 1024))


/** Reserve a large block in the PSRAM which will be shared between the different steps.
 * Each step uses it differently but only wiuthin itself. */
bool reserve_psram_shared_region(void) {
    // The region is time-shared by three sequential steps; size it to the largest:
    //   - Digitization: tensor arena (TENSOR_ARENA_SIZE) + model (MAX_MODEL_SIZE), placed together
    //   - TakeImage / Aligning: one decoded RGB image (~IMAGE_SIZE) + decode margin
    size_t digitization_need = TENSOR_ARENA_SIZE + MAX_MODEL_SIZE;
    shared_region_size = (digitization_need > SHARED_REGION_IMAGE_FLOOR) ? digitization_need : SHARED_REGION_IMAGE_FLOOR;

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Allocating shared PSRAM region (" + std::to_string(shared_region_size) +
            " bytes = max(arena+model " + std::to_string(digitization_need) + ", image floor " +
            std::to_string((size_t)SHARED_REGION_IMAGE_FLOOR) + "))...");
    shared_region = malloc_psram_heap("Shared PSRAM region", shared_region_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (shared_region == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocating shared PSRAM region!");
        return false;
    }
    else {
        return true;
    }
}



/*******************************************************************
 * Memory used in Take Image (STBI)
 *******************************************************************/
bool psram_init_shared_memory_for_take_image_step(void) {
    if (sharedMemoryInUseFor != "") {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM already in use for " + sharedMemoryInUseFor + "!");
        return false;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Init shared memory for step 'Take Image' (STBI buffers)");
    allocatedBytesForSTBI = 0;
    sharedMemoryInUseFor = "TakeImage";

    return true;
}


void psram_deinit_shared_memory_for_take_image_step(void) {
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Deinit shared memory for step 'Take Image' (STBI buffers)");
    // MEM-PROFILE: report the peak shared-region use this capture needed (vs the reserved region).
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "MEM-PROFILE: TakeImage STBI peak " + std::to_string(peakBytesForSTBI) +
            " of reserved " + std::to_string(shared_region_size) + " bytes");
    allocatedBytesForSTBI = 0;
    sharedMemoryInUseFor = "";
}


void *psram_reserve_shared_stbi_memory(size_t size) {
    /* Only large buffers should be placed in the shared PSRAM 
     * If we also place all smaller STBI buffers here, we get artefacts for some reasons. */
    if (size >= 100000) {
        if ((allocatedBytesForSTBI + size) > shared_region_size) { // Check if it still fits in the shared region
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM too small (STBI) to fit additional " +
                    std::to_string(size) + " bytes! Available: " + std::to_string(shared_region_size - allocatedBytesForSTBI) + " bytes!");

            return NULL;
        }
        
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating memory (" + std::to_string(size) + " bytes) for STBI (use shared memory in PSRAM)...");
        allocatedBytesForSTBI += size;
        if (allocatedBytesForSTBI > peakBytesForSTBI) {
            peakBytesForSTBI = allocatedBytesForSTBI;   // MEM-PROFILE high-water mark
        }
        return (uint8_t *)shared_region + allocatedBytesForSTBI - size;
    }
    else { // Normal PSRAM
        return malloc_psram_heap("STBI", size, MALLOC_CAP_SPIRAM);
    }
}


void *psram_reallocate_shared_stbi_memory(void *ptr, size_t newsize) {
    char buf[20];
    sprintf(buf, "%p", ptr);
    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "STBI requested realloc for " + std::string(buf) + " but this is currently unsupported!");
    return NULL;
}


void psram_free_shared_stbi_memory(void *p) {
    if ((p >= shared_region) && (p <= ((uint8_t *)shared_region + allocatedBytesForSTBI))) { // was allocated inside the shared memory
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Part of shared memory used for STBI (PSRAM, part of shared memory) is free again");
    }
    else { // Normal PSRAM
        free_psram_heap("STBI", p);
    }
}



/*******************************************************************
 * Memory used in Aligning Step 
 * During this step we only use the shared part of the PSRAM
 * for the tmpImage. 
 *******************************************************************/
void *psram_reserve_shared_tmp_image_memory(void) {
    if (sharedMemoryInUseFor != "") {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM already in use for " + sharedMemoryInUseFor + "!");
        return NULL;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating tmpImage (" + std::to_string(IMAGE_SIZE) + " bytes, use shared memory in PSRAM)...");
    sharedMemoryInUseFor = "Aligning";
    return shared_region; // Use 1th part of the shared memory for the tmpImage (only user)
}


void psram_free_shared_temp_image_memory(void) {
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Shared memory used for tmpImage (PSRAM, part of shared memory) is free again");
    sharedMemoryInUseFor = "";
}



/*******************************************************************
 * Memory used in Digitization Steps
 * During this step we only use the shared part of the PSRAM for the
 * Tensor Arena and one of the Models.
 * The shared memory is large enough for the largest model and the
 * Tensor Arena. Therefore we do not need to monitor the usage.
 *******************************************************************/
void *psram_get_shared_tensor_arena_memory(void) {
    if ((sharedMemoryInUseFor == "") || (sharedMemoryInUseFor == "Digitization_Model")) {
        sharedMemoryInUseFor = "Digitization_Tensor";
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating Tensor Arena (" + std::to_string(TENSOR_ARENA_SIZE) + " bytes, use shared memory in PSRAM)...");
        return shared_region; // Use 1th part of the shared memory for Tensor
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM already in use for " + sharedMemoryInUseFor + "!");
        return NULL;
    }
}


void *psram_get_shared_model_memory(void) {
    if ((sharedMemoryInUseFor == "") || (sharedMemoryInUseFor == "Digitization_Tensor")) {
        sharedMemoryInUseFor = "Digitization_Model";
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating Model memory (" + std::to_string(MAX_MODEL_SIZE) + " bytes, use shared memory in PSRAM)...");
        return (uint8_t *)shared_region + TENSOR_ARENA_SIZE; // Use 2nd part of the shared memory (after Tensor Arena) for the model
    }
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM already in use for " + sharedMemoryInUseFor + "!");
        return NULL;
    }
}


void psram_free_shared_tensor_arena_and_model_memory(void) {
    sharedMemoryInUseFor = "";
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Shared memory used for Tensor Arena and model (PSRAM, part of shared memory) is free again");
}



/*******************************************************************
 * General
 *******************************************************************/
void *malloc_psram_heap(std::string name, size_t size, uint32_t caps) {
	void *ptr;

	ptr = heap_caps_malloc(size, caps);
    if (ptr != NULL) {
	    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocated " + to_string(size) + " bytes in PSRAM for '" + name + "'");
	}
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate " + to_string(size) + " bytes in PSRAM for '" + name + "'!");
    }

	return ptr;
}


void *realloc_psram_heap(std::string name, void *ptr, size_t size, uint32_t caps) {
	ptr = heap_caps_realloc(ptr, size, caps);
    if (ptr != NULL) {
	    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Reallocated " + to_string(size) + " bytes in PSRAM for '" + name + "'");
	}
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to reallocate " + to_string(size) + " bytes in PSRAM for '" + name + "'!");
    }

	return ptr;
}


void *calloc_psram_heap(std::string name, size_t n, size_t size, uint32_t caps) {
	void *ptr;

	ptr = heap_caps_calloc(n, size, caps);
    if (ptr != NULL) {
	    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Allocated " + to_string(size) + " bytes in PSRAM for '" + name + "'");
	}
    else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate " + to_string(size) + " bytes in PSRAM for '" + name + "'!");
    }

	return ptr;
}


void free_psram_heap(std::string name, void *ptr) {
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Freeing memory in PSRAM used for '" + name + "'...");
    heap_caps_free(ptr);
}
