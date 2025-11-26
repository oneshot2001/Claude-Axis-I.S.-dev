/**
 * Axis I.S. Core Module Implementation
 *
 * Manages VDO streams, Larod inference, DLPU coordination, MQTT, and module orchestration.
 */

#include "core.h"
#include "ACAP.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/time.h>
#include <curl/curl.h>
#include <ctype.h>

/* Undefine system LOG macros */
#ifdef LOG_WARN
#undef LOG_WARN
#endif

#define LOG(level, fmt, args...) syslog(level, fmt, ## args)
#define LOG_WARN 4

/* External module registry (linker provides these symbols) */
extern ModuleInterface* __start_axis_is_modules __attribute__((weak));
extern ModuleInterface* __stop_axis_is_modules __attribute__((weak));

/* Global core context pointer for modules to access shared resources */
static CoreContext* g_core_context = NULL;

/**
 * Get current timestamp in microseconds
 */
static int64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/**
 * Initialize core context
 */
int core_init(CoreContext** ctx, const char* config_file) {
    *ctx = (CoreContext*)calloc(1, sizeof(CoreContext));
    if (!*ctx) {
        LOG(LOG_ERR, "Core: Failed to allocate context\n");
        return -1;
    }

    CoreContext* core = *ctx;

    // Load configuration
    if (config_file) {
        cJSON* cfg_json = ACAP_FILE_Read(config_file);
        if (cfg_json) {
            core->config = cfg_json;
        }
    }

    if (!core->config) {
        // Default configuration
        core->config = cJSON_CreateObject();
        cJSON_AddStringToObject(core->config, "camera_id", "axis-camera-001");
        cJSON_AddNumberToObject(core->config, "target_fps", 10);
        cJSON_AddNumberToObject(core->config, "confidence_threshold", 0.25);
    }

    // Extract config values
    cJSON* cam_id = cJSON_GetObjectItem(core->config, "camera_id");
    const char* camera_id = cam_id && cam_id->valuestring ? cam_id->valuestring : "axis-camera-001";

    cJSON* fps = cJSON_GetObjectItem(core->config, "target_fps");
    int target_fps = fps && cJSON_IsNumber(fps) ? fps->valueint : 10;

    cJSON* threshold = cJSON_GetObjectItem(core->config, "confidence_threshold");
    float conf_threshold = threshold && cJSON_IsNumber(threshold) ? (float)threshold->valuedouble : 0.25;

    // Initialize DLPU coordinator
    core->dlpu = Dlpu_Init(camera_id, 0);
    if (!core->dlpu) {
        LOG(LOG_ERR, "Core: Failed to initialize DLPU\n");
        goto error;
    }

    // Initialize VDO stream at 640x640 to match YOLOv5n model from Axis Model Zoo
    core->vdo = Vdo_Init(640, 640, target_fps);
    if (!core->vdo) {
        LOG(LOG_ERR, "Core: Failed to initialize VDO\n");
        goto error;
    }

    // Initialize Larod inference (optional - POC can run without ML model)
    // Model: YOLOv5n for ARTPEC-8 from Axis Model Zoo (640x640 INT8)
    core->larod = Larod_Init("/usr/local/packages/axis_is_poc/models/yolov5n_artpec8_coco_640.tflite", conf_threshold);
    if (!core->larod) {
        LOG(LOG_WARNING, "Core: Larod init failed - running without ML inference (model not found)\n");
        LOG(LOG_INFO, "Core: To enable ML inference, add yolov5n_int8.tflite to models/ directory\n");
        // Continue without ML - other features still work
    }

    // Initialize MQTT (external - already initialized by main)
    // MQTT context managed by MQTT module, not core

    // Initialize Core API function pointers
    core->api.get_frame = core_api_get_frame;
    core->api.release_frame = core_api_release_frame;
    core->api.run_inference = core_api_run_inference;
    core->api.free_inference = core_api_free_inference;
    core->api.add_detection = core_api_add_detection;
    core->api.publish_metadata = core_api_publish_metadata;
    core->api.log = core_api_log;
    core->api.http_post = core_api_http_post;

    // Initialize frame tracking
    core->current_frame_id = 0;
    core->start_time_us = get_timestamp_us();

    // Set global context pointer for module access to shared resources
    g_core_context = core;

    LOG(LOG_INFO, "Core: Initialization complete\n");
    return 0;

error:
    core_cleanup(core);
    *ctx = NULL;
    return -1;
}

/**
 * Compare modules by priority (for sorting)
 */
static int compare_modules(const void* a, const void* b) {
    ModuleInterface** mod_a = (ModuleInterface**)a;
    ModuleInterface** mod_b = (ModuleInterface**)b;
    return (*mod_a)->priority - (*mod_b)->priority;
}

/**
 * Discover and initialize all registered modules
 */
int core_discover_modules(CoreContext* ctx) {
    if (!ctx) return -1;

    // Count modules using linker symbols
    ModuleInterface** start = &__start_axis_is_modules;
    ModuleInterface** stop = &__stop_axis_is_modules;

    int count = (int)(stop - start);
    if (count <= 0) {
        LOG(LOG_WARN, "Core: No modules registered\n");
        ctx->module_count = 0;
        return 0;
    }

    LOG(LOG_INFO, "Core: Discovered %d modules\n", count);

    // Allocate module arrays
    ctx->modules = (ModuleInterface**)malloc(count * sizeof(ModuleInterface*));
    ctx->module_contexts = (ModuleContext**)malloc(count * sizeof(ModuleContext*));

    if (!ctx->modules || !ctx->module_contexts) {
        LOG(LOG_ERR, "Core: Failed to allocate module arrays\n");
        return -1;
    }

    // Copy module pointers
    for (int i = 0; i < count; i++) {
        ctx->modules[i] = start[i];
    }

    // Sort modules by priority
    qsort(ctx->modules, count, sizeof(ModuleInterface*), compare_modules);

    // Initialize each module
    ctx->module_count = 0;
    for (int i = 0; i < count; i++) {
        ModuleInterface* mod = ctx->modules[i];

        LOG(LOG_INFO, "Core: Initializing module '%s' v%s (priority %d)\n",
            mod->name, mod->version, mod->priority);

        // Create module context
        ModuleContext* mod_ctx = (ModuleContext*)calloc(1, sizeof(ModuleContext));
        if (!mod_ctx) {
            LOG(LOG_ERR, "Core: Failed to allocate context for module '%s'\n", mod->name);
            continue;
        }

        mod_ctx->core = ctx;
        mod_ctx->module_name = mod->name;

        // Load module-specific configuration
        char config_path[256];
        snprintf(config_path, sizeof(config_path), "settings/%s.json", mod->name);
        cJSON* mod_config = ACAP_FILE_Read(config_path);
        if (!mod_config) {
            // Try lowercase name
            snprintf(config_path, sizeof(config_path), "settings/%s.json", mod->name);
            for (char* p = config_path; *p; p++) *p = tolower(*p);
            mod_config = ACAP_FILE_Read(config_path);
        }

        mod_ctx->config = mod_config ? mod_config : cJSON_CreateObject();

        // Initialize module
        if (mod->init && mod->init(mod_ctx, mod_ctx->config) == 0) {
            ctx->module_contexts[ctx->module_count] = mod_ctx;
            ctx->module_count++;
            LOG(LOG_INFO, "Core: Module '%s' initialized successfully\n", mod->name);
        } else {
            LOG(LOG_ERR, "Core: Module '%s' initialization failed\n", mod->name);
            if (mod_ctx->config) cJSON_Delete(mod_ctx->config);
            free(mod_ctx);
        }
    }

    LOG(LOG_INFO, "Core: %d/%d modules initialized successfully\n", ctx->module_count, count);
    return ctx->module_count;
}

/**
 * Start core module
 */
int core_start(CoreContext* ctx) {
    if (!ctx) return -1;

    LOG(LOG_INFO, "Core: Starting module pipeline\n");

    // Call on_start hooks for all modules
    for (int i = 0; i < ctx->module_count; i++) {
        ModuleInterface* mod = ctx->modules[i];
        if (mod->on_start) {
            mod->on_start(ctx->module_contexts[i]);
        }
    }

    return 0;
}

/**
 * Stop core module
 */
int core_stop(CoreContext* ctx) {
    if (!ctx) return -1;

    LOG(LOG_INFO, "Core: Stopping module pipeline\n");

    // Call on_stop hooks for all modules
    for (int i = 0; i < ctx->module_count; i++) {
        ModuleInterface* mod = ctx->modules[i];
        if (mod->on_stop) {
            mod->on_stop(ctx->module_contexts[i]);
        }
    }

    return 0;
}

/**
 * Process single frame through module pipeline
 */
int core_process_frame(CoreContext* ctx) {
    if (!ctx) return -1;

    // Acquire DLPU time slot
    if (!Dlpu_Wait_For_Slot(ctx->dlpu)) {
        LOG(LOG_WARN, "Core: DLPU slot wait timeout\n");
        return -1;
    }

    // Capture frame from VDO
    VdoBuffer* buffer = Vdo_Get_Frame(ctx->vdo);
    if (!buffer) {
        LOG(LOG_WARN, "Core: Failed to capture frame\n");
        Dlpu_Release_Slot(ctx->dlpu);
        return -1;
    }

    // Get frame data directly from VDO buffer
    // Note: ACAP SDK uses VdoBuffer directly, VdoFrame functions don't exist
    void* frame_data = vdo_buffer_get_data(buffer);
    if (!frame_data) {
        LOG(LOG_ERR, "Core: Failed to get frame data from buffer\n");
        Vdo_Release_Frame(ctx->vdo, buffer);
        Dlpu_Release_Slot(ctx->dlpu);
        return -1;
    }

    // Use dimensions from VdoContext (set at initialization)
    unsigned int width = ctx->vdo->width;
    unsigned int height = ctx->vdo->height;
    VdoFormat format = VDO_FORMAT_YUV;  // Set during VDO init

    // Create frame data structure
    FrameData fdata = {
        .vdo_buffer = buffer,
        .vdo_frame = NULL,  // VdoFrame type not used in ACAP SDK
        .frame_data = frame_data,
        .width = width,
        .height = height,
        .format = format,
        .timestamp_us = get_timestamp_us(),
        .frame_id = ctx->current_frame_id++,
        .metadata = metadata_create()
    };

    if (!fdata.metadata) {
        LOG(LOG_ERR, "Core: Failed to create metadata\n");
        Vdo_Release_Frame(ctx->vdo, buffer);
        Dlpu_Release_Slot(ctx->dlpu);
        return -1;
    }

    fdata.metadata->timestamp_us = fdata.timestamp_us;
    fdata.metadata->sequence = ctx->current_frame_id - 1;

    // Process frame through module pipeline
    for (int i = 0; i < ctx->module_count; i++) {
        ModuleInterface* mod = ctx->modules[i];
        ModuleContext* mod_ctx = ctx->module_contexts[i];

        if (mod->process) {
            int status = mod->process(mod_ctx, &fdata);
            if (status == AXIS_IS_MODULE_ERROR) {
                LOG(LOG_WARN, "Core: Module '%s' returned error\n", mod->name);
            }
        }
    }

    // Release DLPU slot after all processing
    Dlpu_Release_Slot(ctx->dlpu);

    // Publish aggregated metadata
    core_api_publish_metadata(ctx, fdata.metadata);

    // Cleanup
    metadata_free(fdata.metadata);
    // Note: No vdo_frame_unref needed - VdoFrame not used in ACAP SDK
    Vdo_Release_Frame(ctx->vdo, buffer);

    return 0;
}

/**
 * Cleanup core module
 */
void core_cleanup(CoreContext* ctx) {
    if (!ctx) return;

    LOG(LOG_INFO, "Core: Cleaning up\n");

    // Cleanup modules in reverse order
    if (ctx->module_contexts) {
        for (int i = ctx->module_count - 1; i >= 0; i--) {
            ModuleInterface* mod = ctx->modules[i];
            ModuleContext* mod_ctx = ctx->module_contexts[i];

            if (mod->cleanup) {
                mod->cleanup(mod_ctx);
            }

            if (mod_ctx->config) {
                cJSON_Delete(mod_ctx->config);
            }
            free(mod_ctx);
        }
        free(ctx->module_contexts);
    }

    if (ctx->modules) {
        free(ctx->modules);
    }

    // Cleanup core resources
    if (ctx->larod) {
        Larod_Cleanup(ctx->larod);
    }

    if (ctx->vdo) {
        Vdo_Cleanup(ctx->vdo);
    }

    if (ctx->dlpu) {
        Dlpu_Cleanup(ctx->dlpu);
    }

    if (ctx->config) {
        cJSON_Delete(ctx->config);
    }

    // Clear global context pointer
    g_core_context = NULL;

    free(ctx);
    LOG(LOG_INFO, "Core: Cleanup complete\n");
}

/**
 * Core API Implementations
 */

VdoBuffer* core_api_get_frame(CoreContext* ctx) {
    return Vdo_Get_Frame(ctx->vdo);
}

void core_api_release_frame(CoreContext* ctx, VdoBuffer* buffer) {
    Vdo_Release_Frame(ctx->vdo, buffer);
}

larodTensor** core_api_run_inference(CoreContext* ctx, const char* model_name,
                                     void* input_data, size_t input_size) {
    // For now, use the default model (YOLOv5n)
    // Future: support multiple models via model registry
    return NULL;  // TODO: Implement multi-model support
}

void core_api_free_inference(larodTensor** outputs) {
    // TODO: Implement
}

void core_api_add_detection(MetadataFrame* meta, Detection det) {
    metadata_add_detection(meta, det);
}

void core_api_publish_metadata(CoreContext* ctx, MetadataFrame* meta) {
    // Get camera ID from config
    cJSON* cam_id = cJSON_GetObjectItem(ctx->config, "camera_id");
    const char* camera_id = cam_id && cam_id->valuestring ? cam_id->valuestring : "axis-camera-001";

    // Build topic
    char topic[128];
    snprintf(topic, sizeof(topic), "axis-is/camera/%s/metadata", camera_id);

    // Convert metadata to JSON
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "camera_id", camera_id);
    cJSON_AddNumberToObject(json, "timestamp_us", meta->timestamp_us);
    cJSON_AddNumberToObject(json, "sequence", meta->sequence);
    cJSON_AddNumberToObject(json, "motion_score", meta->motion_score);
    cJSON_AddNumberToObject(json, "object_count", meta->object_count);
    cJSON_AddNumberToObject(json, "scene_hash", meta->scene_hash);

    // Add detections array
    cJSON* dets = cJSON_CreateArray();
    for (int i = 0; i < meta->detection_count; i++) {
        cJSON* det = cJSON_CreateObject();
        cJSON_AddNumberToObject(det, "class_id", meta->detections[i].class_id);
        cJSON_AddNumberToObject(det, "confidence", meta->detections[i].confidence);
        cJSON_AddNumberToObject(det, "x", meta->detections[i].x);
        cJSON_AddNumberToObject(det, "y", meta->detections[i].y);
        cJSON_AddNumberToObject(det, "width", meta->detections[i].width);
        cJSON_AddNumberToObject(det, "height", meta->detections[i].height);
        cJSON_AddItemToArray(dets, det);
    }
    cJSON_AddItemToObject(json, "detections", dets);

    // Add custom module data
    if (meta->custom_data) {
        cJSON_AddItemToObject(json, "modules", cJSON_Duplicate(meta->custom_data, 1));
    }

    // Publish
    MQTT_Publish_JSON(topic, json, 0, 0);
    cJSON_Delete(json);
}

void core_api_log(int level, const char* module, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    syslog(level, "[%s] %s", module, buffer);
}

/**
 * HTTP POST helper with CURL
 */
struct MemoryStruct {
    char* memory;
    size_t size;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/**
 * Get the shared Larod context from core
 * Modules should use this instead of creating their own Larod connection
 * to avoid DLPU resource conflicts on ARTPEC-9 cameras.
 */
LarodContext* core_api_get_larod(void) {
    return g_core_context ? g_core_context->larod : NULL;
}

int core_api_http_post(const char* url, const char* headers,
                       const char* body, char** response) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct MemoryStruct chunk = {0};
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    // Set headers if provided
    struct curl_slist* header_list = NULL;
    if (headers) {
        header_list = curl_slist_append(NULL, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);

    if (header_list) {
        curl_slist_free_all(header_list);
    }

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(chunk.memory);
        return -1;
    }

    *response = chunk.memory;
    return 0;
}
