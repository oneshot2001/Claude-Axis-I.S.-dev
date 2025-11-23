/**
 * AXION LPR (License Plate Recognition) Module
 *
 * Detects vehicles → Extracts plate region → Claude Vision API → Plate text
 * Priority: 20 (runs after detection)
 */

#ifdef MODULE_LPR

#include "module.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <curl/curl.h>

#define MODULE_NAME "lpr"
#define MODULE_VERSION "1.0.0"
#define MODULE_PRIORITY 20

// COCO class IDs for vehicles
#define COCO_CAR 2
#define COCO_MOTORCYCLE 3
#define COCO_BUS 5
#define COCO_TRUCK 7

/**
 * Module state
 */
typedef struct {
    char* api_key;
    char* api_url;
    float min_confidence;
    int enabled;
    int process_interval;  // Process every Nth vehicle
    int frame_counter;
} LPRState;

/**
 * HTTP response buffer
 */
struct ResponseBuffer {
    char* data;
    size_t size;
};

static size_t write_response(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct ResponseBuffer* buf = (struct ResponseBuffer*)userp;

    char* ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;

    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;

    return realsize;
}

/**
 * Call Claude Vision API for license plate recognition
 */
static int call_claude_api(LPRState* state, const char* image_base64, char** plate_text, float* confidence) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;

    // Build JSON request
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "model", "claude-3-5-sonnet-20241022");
    cJSON_AddNumberToObject(request, "max_tokens", 100);

    cJSON* messages = cJSON_CreateArray();
    cJSON* message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");

    cJSON* content = cJSON_CreateArray();

    // Add text prompt
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text",
        "Extract the license plate number from this image. "
        "Return ONLY the plate number in JSON format: {\"plate\": \"ABC123\", \"confidence\": 0.95}. "
        "If no plate is visible, return {\"plate\": null, \"confidence\": 0.0}");
    cJSON_AddItemToArray(content, text_part);

    // Add image
    cJSON* image_part = cJSON_CreateObject();
    cJSON_AddStringToObject(image_part, "type", "image");
    cJSON* source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "type", "base64");
    cJSON_AddStringToObject(source, "media_type", "image/jpeg");
    cJSON_AddStringToObject(source, "data", image_base64);
    cJSON_AddItemToObject(image_part, "source", source);
    cJSON_AddItemToArray(content, image_part);

    cJSON_AddItemToObject(message, "content", content);
    cJSON_AddItemToArray(messages, message);
    cJSON_AddItemToObject(request, "messages", messages);

    char* request_json = cJSON_PrintUnformatted(request);

    // Set up CURL
    struct curl_slist* headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", state->api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, state->api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(request_json);
    cJSON_Delete(request);

    if (res != CURLE_OK) {
        syslog(LOG_ERR, "[%s] CURL error: %s\n", MODULE_NAME, curl_easy_strerror(res));
        free(response.data);
        return -1;
    }

    // Parse response
    cJSON* response_json = cJSON_Parse(response.data);
    free(response.data);

    if (!response_json) {
        syslog(LOG_ERR, "[%s] Failed to parse Claude response\n", MODULE_NAME);
        return -1;
    }

    // Extract plate text from Claude's response
    cJSON* content_arr = cJSON_GetObjectItem(response_json, "content");
    if (content_arr && cJSON_IsArray(content_arr) && cJSON_GetArraySize(content_arr) > 0) {
        cJSON* first_content = cJSON_GetArrayItem(content_arr, 0);
        cJSON* text_obj = cJSON_GetObjectItem(first_content, "text");
        if (text_obj && cJSON_IsString(text_obj)) {
            // Parse the JSON response from Claude
            cJSON* plate_json = cJSON_Parse(text_obj->valuestring);
            if (plate_json) {
                cJSON* plate_field = cJSON_GetObjectItem(plate_json, "plate");
                cJSON* conf_field = cJSON_GetObjectItem(plate_json, "confidence");

                if (plate_field && cJSON_IsString(plate_field) && plate_field->valuestring) {
                    *plate_text = strdup(plate_field->valuestring);
                }
                if (conf_field && cJSON_IsNumber(conf_field)) {
                    *confidence = (float)conf_field->valuedouble;
                }

                cJSON_Delete(plate_json);
            }
        }
    }

    cJSON_Delete(response_json);
    return *plate_text ? 0 : -1;
}

/**
 * Check if detection is a vehicle
 */
static int is_vehicle(int class_id) {
    return (class_id == COCO_CAR || class_id == COCO_MOTORCYCLE ||
            class_id == COCO_BUS || class_id == COCO_TRUCK);
}

/**
 * Module initialization
 */
static int lpr_init(ModuleContext* ctx, cJSON* config) {
    syslog(LOG_INFO, "[%s] Initializing LPR module\n", MODULE_NAME);

    LPRState* state = (LPRState*)calloc(1, sizeof(LPRState));
    if (!state) {
        syslog(LOG_ERR, "[%s] Failed to allocate state\n", MODULE_NAME);
        return -1;
    }

    // Load configuration
    state->enabled = module_config_get_bool(config, "enabled", true);
    state->min_confidence = module_config_get_float(config, "min_confidence", 0.5);
    state->process_interval = module_config_get_int(config, "process_interval", 10);

    const char* api_key = module_config_get_string(config, "claude_api_key", NULL);
    if (!api_key || strlen(api_key) == 0) {
        syslog(LOG_WARN, "[%s] No API key configured, module disabled\n", MODULE_NAME);
        state->enabled = 0;
    } else {
        state->api_key = strdup(api_key);
    }

    state->api_url = strdup(module_config_get_string(config, "api_url",
                                                       "https://api.anthropic.com/v1/messages"));

    ctx->module_state = state;

    syslog(LOG_INFO, "[%s] Initialized (enabled=%d, interval=%d)\n",
           MODULE_NAME, state->enabled, state->process_interval);

    return AXION_MODULE_SUCCESS;
}

/**
 * Process frame: Detect vehicles → Recognize plates
 */
static int lpr_process(ModuleContext* ctx, FrameData* frame) {
    LPRState* state = (LPRState*)ctx->module_state;
    if (!state || !state->enabled) {
        return AXION_MODULE_SKIP;
    }

    // Process interval throttling
    state->frame_counter++;
    if (state->frame_counter % state->process_interval != 0) {
        return AXION_MODULE_SKIP;
    }

    // Check for vehicle detections
    int vehicle_count = 0;
    cJSON* plates_array = cJSON_CreateArray();

    for (int i = 0; i < frame->metadata->detection_count; i++) {
        Detection* det = &frame->metadata->detections[i];

        if (!is_vehicle(det->class_id)) continue;
        if (det->confidence < state->min_confidence) continue;

        vehicle_count++;

        // TODO: Crop vehicle region from frame
        // TODO: Encode as JPEG
        // TODO: Base64 encode
        // For now, just log vehicle detection

        // Placeholder: Call Claude API with mock data
        // In production, this would be the cropped/encoded plate region
        char* plate_text = NULL;
        float plate_confidence = 0.0;

        // Note: Actual implementation requires image cropping and encoding
        // This is a placeholder showing the API call pattern
        syslog(LOG_INFO, "[%s] Vehicle detected (class=%d, conf=%.2f) at (%.2f,%.2f)\n",
               MODULE_NAME, det->class_id, det->confidence, det->x, det->y);

        // Create plate record
        cJSON* plate_record = cJSON_CreateObject();
        cJSON_AddNumberToObject(plate_record, "vehicle_class", det->class_id);
        cJSON_AddNumberToObject(plate_record, "vehicle_confidence", det->confidence);
        cJSON_AddNumberToObject(plate_record, "bbox_x", det->x);
        cJSON_AddNumberToObject(plate_record, "bbox_y", det->y);
        cJSON_AddNumberToObject(plate_record, "bbox_w", det->width);
        cJSON_AddNumberToObject(plate_record, "bbox_h", det->height);

        if (plate_text) {
            cJSON_AddStringToObject(plate_record, "plate_number", plate_text);
            cJSON_AddNumberToObject(plate_record, "plate_confidence", plate_confidence);
            free(plate_text);
        } else {
            cJSON_AddNullToObject(plate_record, "plate_number");
            cJSON_AddNumberToObject(plate_record, "plate_confidence", 0.0);
        }

        cJSON_AddItemToArray(plates_array, plate_record);
    }

    // Add LPR data to custom metadata
    if (vehicle_count > 0) {
        cJSON* lpr_data = cJSON_CreateObject();
        cJSON_AddNumberToObject(lpr_data, "vehicle_count", vehicle_count);
        cJSON_AddItemToObject(lpr_data, "plates", plates_array);
        cJSON_AddItemToObject(frame->metadata->custom_data, "lpr", lpr_data);

        syslog(LOG_INFO, "[%s] Processed %d vehicles\n", MODULE_NAME, vehicle_count);
    } else {
        cJSON_Delete(plates_array);
    }

    return AXION_MODULE_SUCCESS;
}

/**
 * Module cleanup
 */
static void lpr_cleanup(ModuleContext* ctx) {
    LPRState* state = (LPRState*)ctx->module_state;
    if (!state) return;

    syslog(LOG_INFO, "[%s] Cleaning up\n", MODULE_NAME);

    if (state->api_key) free(state->api_key);
    if (state->api_url) free(state->api_url);

    free(state);
    ctx->module_state = NULL;
}

/**
 * Register LPR module
 */
MODULE_REGISTER(lpr_module, MODULE_NAME, MODULE_VERSION, MODULE_PRIORITY,
                lpr_init, lpr_process, lpr_cleanup);

#endif // MODULE_LPR
