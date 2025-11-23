/**
 * Axis I.S. POC - Main Application (Modular Architecture)
 *
 * Proof of Concept for edge-cloud AI camera surveillance system
 *
 * Modular architecture:
 * - Core: VDO, Larod, DLPU, MQTT coordination
 * - Detection Module: YOLOv5n object detection (always enabled)
 * - LPR Module: License plate recognition with Claude (optional)
 * - OCR Module: Text recognition with Gemini (optional)
 *
 * Build variants:
 *   make                          # Full build (all modules)
 *   make ENABLE_LPR=0             # Core + Detection + OCR
 *   make ENABLE_OCR=0             # Core + Detection + LPR
 *   make ENABLE_LPR=0 ENABLE_OCR=0  # Core + Detection only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <glib.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include "cJSON.h"
#include "ACAP.h"
#include "MQTT.h"
#include "core.h"

#define APP_PACKAGE "axis_is_poc"
#define APP_VERSION "2.0.0"

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args);}
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args);}
#define LOG_ERR(fmt, args...)    { syslog(LOG_ERR, fmt, ## args); fprintf(stderr, fmt, ## args);}

/* Global state */
static GMainLoop *main_loop = NULL;
static CoreContext* core_ctx = NULL;

/* Performance tracking */
static unsigned long frame_count = 0;
static struct timeval app_start_time;

/* Configuration */
typedef struct {
    char camera_id[64];
    int target_fps;
} AppConfig;

static AppConfig config = {
    .camera_id = "axis-camera-001",
    .target_fps = 10
};

/**
 * MQTT Status Callback
 */
void Main_MQTT_Status(int state) {
    switch (state) {
        case MQTT_CONNECTING:
            LOG("MQTT: Connecting to broker...\n");
            break;
        case MQTT_CONNECTED:
            LOG("MQTT: Connected successfully\n");
            // Publish connect event
            char topic[128];
            snprintf(topic, sizeof(topic), "axion/camera/%s/status", config.camera_id);
            cJSON* status = cJSON_CreateObject();
            cJSON_AddStringToObject(status, "state", "online");
            cJSON_AddStringToObject(status, "version", APP_VERSION);
            cJSON_AddNumberToObject(status, "timestamp", time(NULL));
            MQTT_Publish_JSON(topic, status, 1, 1);
            cJSON_Delete(status);
            break;
        case MQTT_DISCONNECTED:
            LOG_WARN("MQTT: Disconnected from broker\n");
            break;
        case MQTT_RECONNECTED:
            LOG("MQTT: Reconnected to broker\n");
            break;
    }
}

/**
 * Process single frame through module pipeline
 */
static gboolean process_frame(gpointer user_data) {
    CoreContext* ctx = (CoreContext*)user_data;

    // Process frame through all registered modules
    if (core_process_frame(ctx) == 0) {
        frame_count++;

        // Log performance every 100 frames
        if (frame_count % 100 == 0) {
            struct timeval now;
            gettimeofday(&now, NULL);
            int uptime_s = (int)(now.tv_sec - app_start_time.tv_sec);
            float actual_fps = uptime_s > 0 ? (float)frame_count / (float)uptime_s : 0.0;

            LOG("Frame %lu: FPS=%.1f Modules=%d\n",
                frame_count, actual_fps, ctx->module_count);
        }
    }

    return G_SOURCE_CONTINUE;
}

/**
 * Settings update callback
 */
void Settings_Updated_Callback(const char* service, cJSON* data) {
    LOG("Settings updated for service: %s\n", service);

    if (strcmp(service, "axion") == 0) {
        cJSON* cam_id = cJSON_GetObjectItem(data, "camera_id");
        if (cam_id && cam_id->valuestring) {
            strncpy(config.camera_id, cam_id->valuestring, sizeof(config.camera_id) - 1);
        }

        cJSON* fps = cJSON_GetObjectItem(data, "target_fps");
        if (fps && cJSON_IsNumber(fps)) {
            config.target_fps = fps->valueint;
        }
    }
}

/**
 * HTTP status endpoint
 */
void HTTP_ENDPOINT_Status(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    struct timeval now;
    gettimeofday(&now, NULL);
    int uptime_s = (int)(now.tv_sec - app_start_time.tv_sec);

    cJSON* status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "app", APP_PACKAGE);
    cJSON_AddStringToObject(status, "version", APP_VERSION);
    cJSON_AddStringToObject(status, "architecture", "modular");
    cJSON_AddStringToObject(status, "camera_id", config.camera_id);
    cJSON_AddNumberToObject(status, "uptime_seconds", uptime_s);
    cJSON_AddNumberToObject(status, "frames_processed", frame_count);
    cJSON_AddNumberToObject(status, "target_fps", config.target_fps);

    float actual_fps = uptime_s > 0 ? (float)frame_count / (float)uptime_s : 0.0;
    cJSON_AddNumberToObject(status, "actual_fps", actual_fps);

    // Add module information
    if (core_ctx) {
        cJSON_AddNumberToObject(status, "module_count", core_ctx->module_count);

        cJSON* modules = cJSON_CreateArray();
        for (int i = 0; i < core_ctx->module_count; i++) {
            cJSON* mod = cJSON_CreateObject();
            cJSON_AddStringToObject(mod, "name", core_ctx->modules[i]->name);
            cJSON_AddStringToObject(mod, "version", core_ctx->modules[i]->version);
            cJSON_AddNumberToObject(mod, "priority", core_ctx->modules[i]->priority);
            cJSON_AddItemToArray(modules, mod);
        }
        cJSON_AddItemToObject(status, "modules", modules);
    }

    char* json_str = cJSON_Print(status);
    ACAP_HTTP_Respond_JSON(response, json_str);
    free(json_str);
    cJSON_Delete(status);
}

/**
 * HTTP modules endpoint
 */
void HTTP_ENDPOINT_Modules(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    if (!core_ctx) {
        ACAP_HTTP_Respond_Error(response, 503, "Core not initialized");
        return;
    }

    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "count", core_ctx->module_count);

    cJSON* modules = cJSON_CreateArray();
    for (int i = 0; i < core_ctx->module_count; i++) {
        ModuleInterface* mod = core_ctx->modules[i];
        cJSON* mod_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(mod_obj, "name", mod->name);
        cJSON_AddStringToObject(mod_obj, "version", mod->version);
        cJSON_AddNumberToObject(mod_obj, "priority", mod->priority);
        cJSON_AddItemToArray(modules, mod_obj);
    }
    cJSON_AddItemToObject(json, "modules", modules);

    char* json_str = cJSON_Print(json);
    ACAP_HTTP_Respond_JSON(response, json_str);
    free(json_str);
    cJSON_Delete(json);
}

/**
 * Signal handler for clean shutdown
 */
static gboolean signal_handler(gpointer user_data) {
    LOG("Received SIGTERM, initiating shutdown\n");

    // Stop processing frames
    if (main_loop && g_main_loop_is_running(main_loop)) {
        g_main_loop_quit(main_loop);
    }

    return G_SOURCE_REMOVE;
}

/**
 * Cleanup resources
 */
void cleanup(void) {
    LOG("Cleaning up resources...\n");

    // Publish offline status
    char topic[128];
    snprintf(topic, sizeof(topic), "axion/camera/%s/status", config.camera_id);
    cJSON* status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "state", "offline");
    cJSON_AddNumberToObject(status, "timestamp", time(NULL));
    MQTT_Publish_JSON(topic, status, 1, 1);
    cJSON_Delete(status);

    // Cleanup core (will cleanup all modules)
    if (core_ctx) {
        core_stop(core_ctx);
        core_cleanup(core_ctx);
        core_ctx = NULL;
    }

    MQTT_Cleanup();
    ACAP_Cleanup();

    LOG("Cleanup complete\n");
}

/**
 * Main entry point
 */
int main(int argc, char* argv[]) {
    openlog(APP_PACKAGE, LOG_PID|LOG_CONS, LOG_USER);
    LOG("====== Starting Axis I.S. POC v%s (Modular) ======\n", APP_VERSION);

    gettimeofday(&app_start_time, NULL);

    // Initialize ACAP framework
    ACAP(APP_PACKAGE, Settings_Updated_Callback);
    ACAP_HTTP_Node("status", HTTP_ENDPOINT_Status);
    ACAP_HTTP_Node("modules", HTTP_ENDPOINT_Modules);

    // Initialize MQTT
    if (!MQTT_Init(Main_MQTT_Status, NULL)) {
        LOG_ERR("Failed to initialize MQTT\n");
        goto error;
    }

    // Initialize core module
    if (core_init(&core_ctx, "settings/core.json") != 0) {
        LOG_ERR("Failed to initialize core\n");
        goto error;
    }

    // Discover and initialize all registered modules
    int module_count = core_discover_modules(core_ctx);
    if (module_count < 0) {
        LOG_ERR("Failed to discover modules\n");
        goto error;
    }

    LOG("Discovered and initialized %d modules:\n", module_count);
    for (int i = 0; i < module_count; i++) {
        LOG("  [%d] %s v%s (priority %d)\n", i,
            core_ctx->modules[i]->name,
            core_ctx->modules[i]->version,
            core_ctx->modules[i]->priority);
    }

    // Start core and all modules
    if (core_start(core_ctx) != 0) {
        LOG_ERR("Failed to start core\n");
        goto error;
    }

    LOG("All components initialized successfully\n");
    LOG("Configuration: Camera=%s FPS=%d\n", config.camera_id, config.target_fps);

    // Set up main loop
    main_loop = g_main_loop_new(NULL, FALSE);

    // Register signal handlers
    GSource *signal_source = g_unix_signal_source_new(SIGTERM);
    if (signal_source) {
        g_source_set_callback(signal_source, signal_handler, NULL, NULL);
        g_source_attach(signal_source, NULL);
    }

    signal_source = g_unix_signal_source_new(SIGINT);
    if (signal_source) {
        g_source_set_callback(signal_source, signal_handler, NULL, NULL);
        g_source_attach(signal_source, NULL);
    }

    // Schedule frame processing (100ms = 10 FPS)
    int interval_ms = 1000 / config.target_fps;
    g_timeout_add(interval_ms, process_frame, core_ctx);

    LOG("Starting main loop (target %d FPS)\n", config.target_fps);

    // Run main loop
    g_main_loop_run(main_loop);

    // Cleanup and exit
    cleanup();
    LOG("====== Axis I.S. POC terminated ======\n");
    closelog();
    return 0;

error:
    cleanup();
    LOG_ERR("====== Axis I.S. POC failed to start ======\n");
    closelog();
    return 1;
}
