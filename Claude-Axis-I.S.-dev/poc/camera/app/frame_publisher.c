/**
 * Frame Publisher Module - On-Demand Frame Transmission
 *
 * Listens for frame requests from cloud service and publishes
 * JPEG-encoded frames via MQTT.
 *
 * Features:
 * - JPEG encoding with configurable quality
 * - Base64 encoding for MQTT transmission
 * - Rate limiting (max 1 frame/minute per camera)
 * - Frame metadata correlation
 */

#include "module.h"
#include "MQTT.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <jpeglib.h>
#include <time.h>

/* Undefine system LOG macros */
#ifdef LOG_ERR
#undef LOG_ERR
#endif

#define LOG(fmt, args...)    { syslog(LOG_INFO, "[frame_publisher] " fmt, ## args); printf("[frame_publisher] " fmt, ## args);}
#define LOG_WARN(fmt, args...)    { syslog(LOG_WARNING, "[frame_publisher] " fmt, ## args); printf("[frame_publisher] " fmt, ## args);}
#define LOG_ERR(fmt, args...)    { syslog(3, "[frame_publisher] " fmt, ## args); fprintf(stderr, "[frame_publisher] " fmt, ## args);}

/* Module state */
typedef struct {
    bool enabled;
    int jpeg_quality;           // JPEG compression quality (0-100)
    int rate_limit_seconds;     // Minimum seconds between frames
    char camera_id[64];         // Camera identifier

    /* Runtime state */
    time_t last_frame_sent;     // Timestamp of last frame transmission
    unsigned long frames_sent;  // Total frames transmitted
    unsigned long requests_received; // Total requests received
    unsigned long requests_throttled; // Requests rejected due to rate limit

    /* Current frame request */
    bool frame_requested;
    char request_id[128];
    char request_reason[256];
} FramePublisherState;

/* Global state pointer for MQTT callback access */
static FramePublisherState* g_frame_publisher_state = NULL;

/* Base64 encoding table */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Base64 encode data
 */
static char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    char* encoded = malloc(*output_length + 1);
    if (!encoded) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded[j++] = base64_table[triple & 0x3F];
    }

    // Handle padding
    int padding = (3 - (input_length % 3)) % 3;
    for (i = 0; i < padding; i++) {
        encoded[*output_length - 1 - i] = '=';
    }

    encoded[*output_length] = '\0';
    return encoded;
}

/**
 * Encode YUV frame to JPEG
 */
static unsigned char* encode_yuv_to_jpeg(const unsigned char* yuv_data, int width, int height,
                                         int quality, size_t* jpeg_size) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned char* jpeg_buffer = NULL;
    unsigned long jpeg_buffer_size = 0;

    // Initialize JPEG compression
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // Output to memory buffer
    jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_buffer_size);

    // Set compression parameters
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;  // RGB
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    // Start compression
    jpeg_start_compress(&cinfo, TRUE);

    // Convert YUV to RGB and compress line by line
    unsigned char* rgb_row = malloc(width * 3);
    if (!rgb_row) {
        jpeg_destroy_compress(&cinfo);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        // Simple YUV to RGB conversion (Y component only for grayscale)
        // For full color, you'd need proper YUV->RGB conversion
        for (int x = 0; x < width; x++) {
            unsigned char Y = yuv_data[y * width + x];
            rgb_row[x * 3 + 0] = Y;  // R
            rgb_row[x * 3 + 1] = Y;  // G
            rgb_row[x * 3 + 2] = Y;  // B
        }

        JSAMPROW row_pointer[1] = { rgb_row };
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    free(rgb_row);
    jpeg_finish_compress(&cinfo);

    *jpeg_size = jpeg_buffer_size;

    // Copy buffer (jpeg_mem_dest allocates its own buffer)
    unsigned char* result = malloc(jpeg_buffer_size);
    if (result) {
        memcpy(result, jpeg_buffer, jpeg_buffer_size);
    }

    free(jpeg_buffer);
    jpeg_destroy_compress(&cinfo);

    return result;
}

/**
 * MQTT callback for frame requests
 */
void frame_request_callback(const char* topic, const char* payload, void* user_data) {
    FramePublisherState* state = (FramePublisherState*)user_data;

    if (!state || !state->enabled) {
        return;
    }

    state->requests_received++;

    // Parse request JSON
    cJSON* req = cJSON_Parse(payload);
    if (!req) {
        LOG_WARN("Invalid frame request JSON\n");
        return;
    }

    cJSON* req_id = cJSON_GetObjectItem(req, "request_id");
    cJSON* reason = cJSON_GetObjectItem(req, "reason");

    if (req_id && req_id->valuestring) {
        strncpy(state->request_id, req_id->valuestring, sizeof(state->request_id) - 1);
    }

    if (reason && reason->valuestring) {
        strncpy(state->request_reason, reason->valuestring, sizeof(state->request_reason) - 1);
    }

    // Check rate limit
    time_t now = time(NULL);
    if (now - state->last_frame_sent < state->rate_limit_seconds) {
        state->requests_throttled++;
        LOG_WARN("Frame request throttled (last frame %lds ago)\n",
                 (long)(now - state->last_frame_sent));
        cJSON_Delete(req);
        return;
    }

    // Mark frame as requested (will be processed in next process() call)
    state->frame_requested = true;

    LOG("Frame requested: id=%s reason=%s\n", state->request_id, state->request_reason);

    cJSON_Delete(req);
}

/**
 * Public MQTT message handler for frame requests
 * This is the callback registered with MQTT_Init()
 * It routes frame_request messages to the frame_publisher module
 */
void frame_publisher_mqtt_callback(const char* topic, const char* payload) {
    // Check if this is a frame_request message for us
    if (!topic || !strstr(topic, "frame_request")) {
        return;
    }

    // Use global state pointer
    if (g_frame_publisher_state) {
        frame_request_callback(topic, payload, g_frame_publisher_state);
    }
}

/**
 * Initialize frame publisher module
 */
static int frame_publisher_init(ModuleContext* ctx, cJSON* config) {
    LOG("Initializing frame publisher module\n");

    FramePublisherState* state = calloc(1, sizeof(FramePublisherState));
    if (!state) {
        LOG_ERR("Failed to allocate state\n");
        return AXIS_IS_MODULE_ERROR;
    }

    // Load configuration
    state->enabled = module_config_get_bool(config, "enabled", true);
    state->jpeg_quality = module_config_get_int(config, "jpeg_quality", 85);
    state->rate_limit_seconds = module_config_get_int(config, "rate_limit_seconds", 60);

    const char* camera_id = module_config_get_string(config, "camera_id", "axis-camera-001");
    strncpy(state->camera_id, camera_id, sizeof(state->camera_id) - 1);

    // Validate configuration
    if (state->jpeg_quality < 1 || state->jpeg_quality > 100) {
        LOG_WARN("Invalid JPEG quality %d, using 85\n", state->jpeg_quality);
        state->jpeg_quality = 85;
    }

    if (state->rate_limit_seconds < 1) {
        LOG_WARN("Invalid rate limit %d, using 60s\n", state->rate_limit_seconds);
        state->rate_limit_seconds = 60;
    }

    // Initialize runtime state
    state->last_frame_sent = 0;
    state->frames_sent = 0;
    state->requests_received = 0;
    state->requests_throttled = 0;
    state->frame_requested = false;

    // Subscribe to frame request topic
    char topic[256];
    snprintf(topic, sizeof(topic), "axis-is/camera/%s/frame_request", state->camera_id);
    MQTT_Subscribe(topic);

    // Note: frame_request_callback needs to be registered via MQTT_Init's messageCallback parameter
    // For now, just subscribe to the topic

    LOG("Subscribed to: %s\n", topic);
    LOG("Configuration: quality=%d rate_limit=%ds\n",
        state->jpeg_quality, state->rate_limit_seconds);

    ctx->module_state = state;

    // Set global state pointer for MQTT callback access
    g_frame_publisher_state = state;

    return AXIS_IS_MODULE_SUCCESS;
}

/**
 * Process frame and publish if requested
 */
static int frame_publisher_process(ModuleContext* ctx, FrameData* frame) {
    FramePublisherState* state = (FramePublisherState*)ctx->module_state;

    if (!state || !state->enabled) {
        return AXIS_IS_MODULE_SKIP;
    }

    // Check if frame was requested
    if (!state->frame_requested) {
        return AXIS_IS_MODULE_SKIP;
    }

    // Reset request flag
    state->frame_requested = false;

    LOG("Processing frame request: %s\n", state->request_id);

    // Encode frame to JPEG
    size_t jpeg_size = 0;
    unsigned char* jpeg_data = encode_yuv_to_jpeg(
        frame->frame_data,
        frame->width,
        frame->height,
        state->jpeg_quality,
        &jpeg_size
    );

    if (!jpeg_data) {
        LOG_ERR("Failed to encode JPEG\n");
        return AXIS_IS_MODULE_ERROR;
    }

    LOG("JPEG encoded: %zu bytes (quality=%d)\n", jpeg_size, state->jpeg_quality);

    // Base64 encode JPEG
    size_t base64_size = 0;
    char* base64_data = base64_encode(jpeg_data, jpeg_size, &base64_size);
    free(jpeg_data);

    if (!base64_data) {
        LOG_ERR("Failed to Base64 encode\n");
        return AXIS_IS_MODULE_ERROR;
    }

    LOG("Base64 encoded: %zu bytes\n", base64_size);

    // Build MQTT message
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "request_id", state->request_id);
    cJSON_AddNumberToObject(msg, "timestamp_us", frame->timestamp_us);
    cJSON_AddNumberToObject(msg, "frame_id", frame->frame_id);
    cJSON_AddNumberToObject(msg, "width", frame->width);
    cJSON_AddNumberToObject(msg, "height", frame->height);
    cJSON_AddStringToObject(msg, "format", "jpeg");
    cJSON_AddNumberToObject(msg, "quality", state->jpeg_quality);
    cJSON_AddNumberToObject(msg, "jpeg_size", jpeg_size);
    cJSON_AddStringToObject(msg, "image_base64", base64_data);

    // Publish to MQTT
    char topic[256];
    snprintf(topic, sizeof(topic), "axis-is/camera/%s/frame", state->camera_id);

    int result = MQTT_Publish_JSON(topic, msg, 1, 0);  // QoS 1, no retain

    // MQTT_Publish_JSON returns 1 on success, 0 on failure
    if (result != 0) {
        state->frames_sent++;
        state->last_frame_sent = time(NULL);
        LOG("Frame published: id=%s size=%zu bytes (JPEG) / %zu bytes (Base64)\n",
            state->request_id, jpeg_size, base64_size);
    } else {
        LOG_ERR("Failed to publish frame\n");
    }

    // Cleanup
    free(base64_data);
    cJSON_Delete(msg);

    // Add module metadata
    cJSON* module_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(module_data, "frames_sent", state->frames_sent);
    cJSON_AddNumberToObject(module_data, "requests_received", state->requests_received);
    cJSON_AddNumberToObject(module_data, "requests_throttled", state->requests_throttled);
    cJSON_AddNumberToObject(module_data, "jpeg_size_bytes", jpeg_size);
    cJSON_AddNumberToObject(module_data, "base64_size_bytes", base64_size);
    cJSON_AddItemToObject(frame->metadata->custom_data, "frame_publisher", module_data);

    // MQTT_Publish_JSON returns 1 on success, 0 on failure
    return result != 0 ? AXIS_IS_MODULE_SUCCESS : AXIS_IS_MODULE_ERROR;
}

/**
 * Cleanup frame publisher module
 */
static void frame_publisher_cleanup(ModuleContext* ctx) {
    FramePublisherState* state = (FramePublisherState*)ctx->module_state;

    if (state) {
        LOG("Cleanup: sent %lu frames, throttled %lu requests\n",
            state->frames_sent, state->requests_throttled);

        // Unsubscribe from MQTT topic
        char topic[256];
        snprintf(topic, sizeof(topic), "axis-is/camera/%s/frame_request", state->camera_id);
        MQTT_Unsubscribe(topic);

        free(state);
        ctx->module_state = NULL;
    }
}

/* Register module with priority 40 (after detection, LPR, OCR) */
MODULE_REGISTER(frame_publisher, "frame_publisher", "1.0.0", 40,
                frame_publisher_init, frame_publisher_process, frame_publisher_cleanup);
