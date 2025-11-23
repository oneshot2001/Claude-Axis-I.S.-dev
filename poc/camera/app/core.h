/**
 * Axis I.S. Core Module
 *
 * Manages VDO streams, Larod inference, DLPU coordination, and module orchestration.
 */

#ifndef CORE_H
#define CORE_H

#include "module.h"
#include "vdo_handler.h"
#include "larod_handler.h"
#include "dlpu_basic.h"
#include "MQTT.h"

/**
 * Core context structure
 */
struct CoreContext {
    // VDO streaming
    VdoContext* vdo;

    // Larod inference
    LarodContext* larod;

    // DLPU coordination
    DlpuContext* dlpu;

    // MQTT client
    MQTTContext* mqtt;

    // Module management
    ModuleInterface** modules;
    ModuleContext** module_contexts;
    int module_count;

    // Core API for modules
    CoreAPI api;

    // Frame tracking
    int current_frame_id;
    int64_t start_time_us;

    // Configuration
    cJSON* config;
};

/**
 * Initialize the core module
 */
int core_init(CoreContext** ctx, const char* config_file);

/**
 * Start core module (after all modules initialized)
 */
int core_start(CoreContext* ctx);

/**
 * Stop core module
 */
int core_stop(CoreContext* ctx);

/**
 * Cleanup core module
 */
void core_cleanup(CoreContext* ctx);

/**
 * Discover and initialize all registered modules
 */
int core_discover_modules(CoreContext* ctx);

/**
 * Process a single frame through the module pipeline
 */
int core_process_frame(CoreContext* ctx);

/**
 * Main processing loop
 */
int core_run(CoreContext* ctx);

/**
 * Core API implementations (exposed to modules)
 */
VdoBuffer* core_api_get_frame(CoreContext* ctx);
void core_api_release_frame(CoreContext* ctx, VdoBuffer* buffer);
larodTensor** core_api_run_inference(CoreContext* ctx, const char* model_name,
                                     void* input_data, size_t input_size);
void core_api_free_inference(larodTensor** outputs);
void core_api_add_detection(MetadataFrame* meta, Detection det);
void core_api_publish_metadata(CoreContext* ctx, MetadataFrame* meta);
void core_api_log(int level, const char* module, const char* format, ...);
int core_api_http_post(const char* url, const char* headers,
                       const char* body, char** response);

#endif // CORE_H
