/**
 * AXION Module Interface
 *
 * Defines the plugin system for modular ACAP applications.
 * All modules (Core, Detection, LPR, OCR) implement this interface.
 */

#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include "cJSON.h"
#include <vdo-stream.h>
#include <larod.h>

// Forward declarations
typedef struct ModuleInterface ModuleInterface;
typedef struct ModuleContext ModuleContext;
typedef struct FrameData FrameData;
typedef struct MetadataFrame MetadataFrame;
typedef struct CoreContext CoreContext;

/**
 * Module return codes
 */
typedef enum {
    AXION_MODULE_SUCCESS = 0,
    AXION_MODULE_ERROR = -1,
    AXION_MODULE_SKIP = 1,      // Module skipped processing this frame
    AXION_MODULE_NOT_READY = 2  // Module not initialized
} ModuleStatus;

/**
 * Detection result from inference
 */
typedef struct {
    int class_id;
    float confidence;
    float x, y, width, height;  // Normalized coordinates [0-1]
} Detection;

/**
 * Aggregated metadata from all modules
 */
struct MetadataFrame {
    int64_t timestamp_us;
    int sequence;
    float motion_score;
    int object_count;
    uint32_t scene_hash;

    // Detection results
    Detection* detections;
    int detection_count;
    int detection_capacity;

    // Module-specific data (JSON)
    cJSON* custom_data;  // Each module adds its own fields
};

/**
 * Shared frame data passed through module pipeline
 */
struct FrameData {
    VdoBuffer* vdo_buffer;       // VDO buffer (zero-copy)
    VdoFrame* vdo_frame;         // Extracted frame
    void* frame_data;            // Raw pixel data
    unsigned int width;
    unsigned int height;
    VdoFormat format;

    MetadataFrame* metadata;     // Aggregated metadata
    int64_t timestamp_us;        // Frame timestamp
    int frame_id;                // Sequential frame ID
};

/**
 * Module context for preserving state
 */
struct ModuleContext {
    void* module_state;          // Module's private data
    cJSON* config;               // Module configuration
    CoreContext* core;           // Access to core APIs
    const char* module_name;     // Module name
};

/**
 * Core API exposed to modules
 */
typedef struct {
    // Frame access
    VdoBuffer* (*get_frame)(CoreContext* ctx);
    void (*release_frame)(CoreContext* ctx, VdoBuffer* buffer);

    // Inference
    larodTensor** (*run_inference)(CoreContext* ctx, const char* model_name,
                                   void* input_data, size_t input_size);
    void (*free_inference)(larodTensor** outputs);

    // Metadata management
    void (*add_detection)(MetadataFrame* meta, Detection det);
    void (*publish_metadata)(CoreContext* ctx, MetadataFrame* meta);

    // Logging
    void (*log)(int level, const char* module, const char* format, ...);

    // HTTP client for AI APIs
    int (*http_post)(const char* url, const char* headers,
                     const char* body, char** response);
} CoreAPI;

/**
 * Module interface - all modules implement these functions
 */
struct ModuleInterface {
    const char* name;            // Module name
    const char* version;         // Module version
    int priority;                // Execution order (lower = earlier)

    // Lifecycle functions
    int (*init)(ModuleContext* ctx, cJSON* config);
    int (*process)(ModuleContext* ctx, FrameData* frame);
    void (*cleanup)(ModuleContext* ctx);

    // Optional hooks
    int (*on_start)(ModuleContext* ctx);
    int (*on_stop)(ModuleContext* ctx);
};

/**
 * Module registration macro
 *
 * Usage in module implementation:
 *   MODULE_REGISTER(my_module, "MyModule", "1.0.0", 100,
 *                   my_init, my_process, my_cleanup);
 */
#define MODULE_REGISTER(var_name, mod_name, mod_version, mod_priority, \
                        init_fn, process_fn, cleanup_fn) \
    static ModuleInterface var_name = { \
        .name = mod_name, \
        .version = mod_version, \
        .priority = mod_priority, \
        .init = init_fn, \
        .process = process_fn, \
        .cleanup = cleanup_fn, \
        .on_start = NULL, \
        .on_stop = NULL \
    }; \
    static ModuleInterface* __module_ptr_##var_name \
        __attribute__((used, section(".axion_modules"))) = &var_name

/**
 * Utility functions for modules
 */

// Create metadata frame
MetadataFrame* metadata_create(void);

// Free metadata frame
void metadata_free(MetadataFrame* meta);

// Add detection to metadata
void metadata_add_detection(MetadataFrame* meta, Detection det);

// Get module configuration value
const char* module_config_get_string(cJSON* config, const char* key, const char* default_val);
int module_config_get_int(cJSON* config, const char* key, int default_val);
float module_config_get_float(cJSON* config, const char* key, float default_val);
bool module_config_get_bool(cJSON* config, const char* key, bool default_val);

// Image encoding utilities
int encode_jpeg(void* pixels, int width, int height, VdoFormat format,
                char** jpeg_data, size_t* jpeg_size);
int encode_base64(const char* input, size_t input_len, char** output);

// HTTP client utilities
int http_post_json(const char* url, const char* api_key, cJSON* request, cJSON** response);

#endif // MODULE_H
