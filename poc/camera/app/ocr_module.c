/**
 * Axis I.S. OCR (Optical Character Recognition) Module
 *
 * Detects text regions → Gemini Vision API → Extracted text
 * Priority: 30 (runs after detection and LPR)
 */

#ifdef MODULE_OCR

#include "module.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <curl/curl.h>

#define MODULE_NAME "ocr"
#define MODULE_VERSION "1.0.0"
#define MODULE_PRIORITY 30

/**
 * Module state
 */
typedef struct {
    char* api_key;
    char* api_url;
    char* model_name;
    int enabled;
    int process_interval;
    int frame_counter;
    float min_edge_density;  // Minimum edge density to trigger OCR
} OCRState;

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
 * Call Gemini Vision API for OCR
 */
static int call_gemini_api(OCRState* state, const char* image_base64, cJSON** text_results) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    struct ResponseBuffer response = {0};
    response.data = malloc(1);
    response.size = 0;

    // Build JSON request for Gemini
    cJSON* request = cJSON_CreateObject();

    cJSON* contents = cJSON_CreateArray();
    cJSON* content = cJSON_CreateObject();

    cJSON* parts = cJSON_CreateArray();

    // Add text prompt
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "text",
        "Extract all readable text from this image. "
        "Return the results as a JSON array: "
        "[{\"text\": \"example\", \"confidence\": 0.95}]. "
        "If no text is visible, return an empty array []");
    cJSON_AddItemToArray(parts, text_part);

    // Add image
    cJSON* image_part = cJSON_CreateObject();
    cJSON* inline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_data, "mime_type", "image/jpeg");
    cJSON_AddStringToObject(inline_data, "data", image_base64);
    cJSON_AddItemToObject(image_part, "inline_data", inline_data);
    cJSON_AddItemToArray(parts, image_part);

    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(request, "contents", contents);

    // Add generation config
    cJSON* generation_config = cJSON_CreateObject();
    cJSON_AddNumberToObject(generation_config, "temperature", 0.1);
    cJSON_AddNumberToObject(generation_config, "maxOutputTokens", 500);
    cJSON_AddItemToObject(request, "generationConfig", generation_config);

    char* request_json = cJSON_PrintUnformatted(request);

    // Build URL with API key
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", state->api_url, state->api_key);

    // Set up CURL
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
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
        syslog(LOG_ERR, "[%s] Failed to parse Gemini response\n", MODULE_NAME);
        return -1;
    }

    // Extract text from Gemini's response
    cJSON* candidates = cJSON_GetObjectItem(response_json, "candidates");
    if (candidates && cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON* first_candidate = cJSON_GetArrayItem(candidates, 0);
        cJSON* content_obj = cJSON_GetObjectItem(first_candidate, "content");
        if (content_obj) {
            cJSON* parts_arr = cJSON_GetObjectItem(content_obj, "parts");
            if (parts_arr && cJSON_IsArray(parts_arr) && cJSON_GetArraySize(parts_arr) > 0) {
                cJSON* first_part = cJSON_GetArrayItem(parts_arr, 0);
                cJSON* text_field = cJSON_GetObjectItem(first_part, "text");
                if (text_field && cJSON_IsString(text_field)) {
                    // Parse the JSON array from Gemini
                    *text_results = cJSON_Parse(text_field->valuestring);
                }
            }
        }
    }

    cJSON_Delete(response_json);
    return *text_results ? 0 : -1;
}

/**
 * Estimate edge density (simple heuristic for text presence)
 */
static float estimate_edge_density(void* frame_data, int width, int height) {
    // Simplified: In production, you'd use Sobel/Canny edge detection
    // For now, return a placeholder
    return 0.5;  // Placeholder
}

/**
 * Module initialization
 */
static int ocr_init(ModuleContext* ctx, cJSON* config) {
    syslog(LOG_INFO, "[%s] Initializing OCR module\n", MODULE_NAME);

    OCRState* state = (OCRState*)calloc(1, sizeof(OCRState));
    if (!state) {
        syslog(LOG_ERR, "[%s] Failed to allocate state\n", MODULE_NAME);
        return -1;
    }

    // Load configuration
    state->enabled = module_config_get_bool(config, "enabled", true);
    state->process_interval = module_config_get_int(config, "process_interval", 30);
    state->min_edge_density = module_config_get_float(config, "min_edge_density", 0.3);

    const char* api_key = module_config_get_string(config, "gemini_api_key", NULL);
    if (!api_key || strlen(api_key) == 0) {
        syslog(LOG_WARN, "[%s] No API key configured, module disabled\n", MODULE_NAME);
        state->enabled = 0;
    } else {
        state->api_key = strdup(api_key);
    }

    state->model_name = strdup(module_config_get_string(config, "model", "gemini-2.0-flash-exp"));

    // Build API URL with model
    char url_template[256];
    snprintf(url_template, sizeof(url_template),
             "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent",
             state->model_name);
    state->api_url = strdup(url_template);

    ctx->module_state = state;

    syslog(LOG_INFO, "[%s] Initialized (enabled=%d, model=%s, interval=%d)\n",
           MODULE_NAME, state->enabled, state->model_name, state->process_interval);

    return AXIS_IS_MODULE_SUCCESS;
}

/**
 * Process frame: Detect text regions → Extract text
 */
static int ocr_process(ModuleContext* ctx, FrameData* frame) {
    OCRState* state = (OCRState*)ctx->module_state;
    if (!state || !state->enabled) {
        return AXIS_IS_MODULE_SKIP;
    }

    // Process interval throttling
    state->frame_counter++;
    if (state->frame_counter % state->process_interval != 0) {
        return AXIS_IS_MODULE_SKIP;
    }

    // Check if frame likely contains text
    float edge_density = estimate_edge_density(frame->frame_data, frame->width, frame->height);
    if (edge_density < state->min_edge_density) {
        return AXIS_IS_MODULE_SKIP;
    }

    // TODO: Encode full frame or ROI as JPEG
    // TODO: Base64 encode
    // For now, just log processing

    syslog(LOG_INFO, "[%s] Processing frame %d (edge_density=%.2f)\n",
           MODULE_NAME, frame->frame_id, edge_density);

    // Placeholder: Call Gemini API with mock data
    // In production, this would be the encoded frame/ROI
    cJSON* text_results = NULL;

    // Note: Actual implementation requires frame encoding
    // This is a placeholder showing the API call pattern

    // Create OCR record even without actual API call
    cJSON* ocr_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(ocr_data, "edge_density", edge_density);
    cJSON_AddBoolToObject(ocr_data, "processed", false);
    cJSON_AddStringToObject(ocr_data, "model", state->model_name);

    if (text_results && cJSON_IsArray(text_results)) {
        cJSON_AddNumberToObject(ocr_data, "text_count", cJSON_GetArraySize(text_results));
        cJSON_AddItemToObject(ocr_data, "texts", text_results);
        syslog(LOG_INFO, "[%s] Extracted %d text regions\n",
               MODULE_NAME, cJSON_GetArraySize(text_results));
    } else {
        cJSON_AddNumberToObject(ocr_data, "text_count", 0);
        cJSON_AddItemToObject(ocr_data, "texts", cJSON_CreateArray());
    }

    cJSON_AddItemToObject(frame->metadata->custom_data, "ocr", ocr_data);

    return AXIS_IS_MODULE_SUCCESS;
}

/**
 * Module cleanup
 */
static void ocr_cleanup(ModuleContext* ctx) {
    OCRState* state = (OCRState*)ctx->module_state;
    if (!state) return;

    syslog(LOG_INFO, "[%s] Cleaning up\n", MODULE_NAME);

    if (state->api_key) free(state->api_key);
    if (state->api_url) free(state->api_url);
    if (state->model_name) free(state->model_name);

    free(state);
    ctx->module_state = NULL;
}

/**
 * Register OCR module
 */
MODULE_REGISTER(ocr_module, MODULE_NAME, MODULE_VERSION, MODULE_PRIORITY,
                ocr_init, ocr_process, ocr_cleanup);

#endif // MODULE_OCR
