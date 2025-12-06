/**
 * Axis I.S. Base Module - Main Application (Modular Architecture)
 *
 * Foundation for edge-cloud AI camera surveillance system
 *
 * Base module includes:
 * - Core: VDO, Larod, DLPU, MQTT coordination
 * - Detection Module: YOLOv5n object detection (always enabled)
 * - Module Framework: Plugin system for custom modules
 *
 * This serves as the foundation for future modules:
 * - LPR Module (License Plate Recognition)
 * - OCR Module (Optical Character Recognition)
 * - Face Recognition Module
 * - Object Tracking Module
 * - Custom AI modules
 *
 * Build:
 *   make                          # Build base module
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

/* External: Frame publisher callback for MQTT messages */
extern void frame_request_callback(const char* topic, const char* payload);

#define APP_PACKAGE "axis_is_poc"
#define APP_VERSION "2.0.0"

/* Undefine system LOG macros to avoid conflicts */
#ifdef LOG_ERR
#undef LOG_ERR
#endif

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args);}
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, fmt, ## args); printf(fmt, ## args);}
#define LOG_ERR(fmt, args...)    { syslog(3, fmt, ## args); fprintf(stderr, fmt, ## args);}

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
            snprintf(topic, sizeof(topic), "axis-is/camera/%s/status", config.camera_id);
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

    if (strcmp(service, "axis_is") == 0) {
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

    ACAP_HTTP_Respond_JSON(response, status);
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

    ACAP_HTTP_Respond_JSON(response, json);
    cJSON_Delete(json);
}

/**
 * HTTP UI endpoint (Static File Serving)
 */
void HTTP_ENDPOINT_UI(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    const char* uri = FCGX_GetParam("REQUEST_URI", request->request->envp);
    if (!uri) {
        ACAP_HTTP_Respond_Error(response, 400, "Invalid URI");
        return;
    }
    
    // URI is like /local/axis_is_poc/ui/index.html
    const char* prefix = "/ui/";
    const char* start = strstr(uri, prefix);
    char filepath[256];
    
    if (start) {
        start += strlen(prefix);
        if (*start == '\0' || *start == '?') {
            snprintf(filepath, sizeof(filepath), "html/index.html");
        } else {
            const char* query = strchr(start, '?');
            size_t len = query ? (size_t)(query - start) : strlen(start);
            if (len > 200) len = 200; 
            snprintf(filepath, sizeof(filepath), "html/%.*s", (int)len, start);
        }
    } else {
        snprintf(filepath, sizeof(filepath), "html/index.html");
    }
    
    if (strstr(filepath, "..")) {
        ACAP_HTTP_Respond_Error(response, 403, "Forbidden");
        return;
    }
    
    if (!ACAP_HTTP_Serve_Static(response, filepath)) {
        ACAP_HTTP_Respond_Error(response, 404, "Not Found");
    }
}

/**
 * HTTP Detections endpoint
 */
void HTTP_ENDPOINT_Detections(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    if (!core_ctx) {
        ACAP_HTTP_Respond_Error(response, 503, "Core not initialized");
        return;
    }
    cJSON* meta = core_get_latest_metadata(core_ctx);
    if (meta) {
        // Direct pass of cJSON object
        ACAP_HTTP_Respond_JSON(response, meta);
        cJSON_Delete(meta);
    } else {
        cJSON* empty = cJSON_CreateObject();
        ACAP_HTTP_Respond_JSON(response, empty);
        cJSON_Delete(empty);
    }
}

/**
 * HTTP Frame endpoint
 */
void HTTP_ENDPOINT_Frame(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    ACAP_HTTP_Respond_Error(response, 501, "Not Implemented (Requires JPEG Encoder)");
}

/**
 * HTTP Config endpoint
 */
void HTTP_ENDPOINT_Config(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    if (!core_ctx || !core_ctx->config) {
        ACAP_HTTP_Respond_Error(response, 503, "Config not available");
        return;
    }
    ACAP_HTTP_Respond_JSON(response, core_ctx->config);
}

/**
 * HTTP logs endpoint - fetches recent syslog entries for this app
 */
void HTTP_ENDPOINT_Logs(ACAP_HTTP_Response response, const ACAP_HTTP_Request request) {
    FILE* fp;
    char buffer[512];
    char* logs = NULL;
    size_t logs_size = 0;
    size_t logs_capacity = 8192;

    logs = malloc(logs_capacity);
    if (!logs) {
        ACAP_HTTP_Respond_Error(response, 500, "Memory allocation failed");
        return;
    }
    logs[0] = '\0';

    // Get recent log entries for axis_is_poc from syslog
    fp = popen("grep axis_is_poc /var/log/messages 2>/dev/null | tail -500", "r");
    if (fp == NULL) {
        // Try journalctl as fallback
        fp = popen("journalctl -u axis_is_poc --no-pager -n 500 2>/dev/null", "r");
    }

    if (fp != NULL) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            size_t len = strlen(buffer);
            if (logs_size + len + 1 > logs_capacity) {
                logs_capacity *= 2;
                char* new_logs = realloc(logs, logs_capacity);
                if (!new_logs) {
                    break;
                }
                logs = new_logs;
            }
            strcpy(logs + logs_size, buffer);
            logs_size += len;
        }
        pclose(fp);
    }

    if (logs_size == 0) {
        strcpy(logs, "No log entries found for axis_is_poc\n");
    }

    // Send as plain text
    ACAP_HTTP_Respond_Text(response, logs);
    free(logs);
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
    snprintf(topic, sizeof(topic), "axis-is/camera/%s/status", config.camera_id);
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
    ACAP_HTTP_Node("app_status", HTTP_ENDPOINT_Status);
    ACAP_HTTP_Node("modules", HTTP_ENDPOINT_Modules);
    ACAP_HTTP_Node("ui/", HTTP_ENDPOINT_UI);
    ACAP_HTTP_Node("detections", HTTP_ENDPOINT_Detections);
    ACAP_HTTP_Node("frame/preview", HTTP_ENDPOINT_Frame);
    ACAP_HTTP_Node("config", HTTP_ENDPOINT_Config);
    ACAP_HTTP_Node("logs", HTTP_ENDPOINT_Logs);

    // Initialize MQTT with frame request callback
    if (!MQTT_Init(Main_MQTT_Status, frame_request_callback)) {
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
