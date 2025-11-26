/**
 * Axis I.S. Detection Module
 *
 * YOLOv5n object detection + scene/motion analysis
 * Priority: 10 (runs first)
 */

#include "module.h"
#include "larod_handler.h"
#include "core.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/* Define LOG_WARN if not defined */
#ifndef LOG_WARN
#define LOG_WARN 4
#endif

#define MODULE_NAME "detection"
#define MODULE_VERSION "1.0.0"
#define MODULE_PRIORITY 10

/**
 * Module state
 */
typedef struct {
    LarodContext* larod;

    // Motion detection state
    unsigned char* last_frame_data;
    size_t frame_data_size;

    // Scene change detection
    char last_scene_hash[32];

    // Configuration
    float confidence_threshold;
    const char* model_path;
} DetectionState;

/**
 * Compute scene hash for change detection
 */
static void compute_scene_hash(const unsigned char* data, size_t size, uint32_t* hash_out) {
    unsigned long hash = 5381;

    // Sample every 1000th byte
    for (size_t i = 0; i < size; i += 1000) {
        hash = ((hash << 5) + hash) + data[i];
    }

    *hash_out = (uint32_t)(hash & 0xFFFFFFFF);
}

/**
 * Compute motion score via frame differencing
 */
static float compute_motion_score(DetectionState* state, const unsigned char* frame_data, size_t size) {
    if (!state->last_frame_data) {
        // First frame - initialize
        state->last_frame_data = (unsigned char*)malloc(size);
        if (!state->last_frame_data) {
            return 0.0;
        }
        state->frame_data_size = size;
        memcpy(state->last_frame_data, frame_data, size);
        return 0.0;
    }

    int diff_count = 0;
    int threshold = 30;
    size_t sample_count = 0;

    // Sample-based comparison (every 100 pixels)
    size_t min_size = (size < state->frame_data_size) ? size : state->frame_data_size;
    for (size_t i = 0; i < min_size; i += 100) {
        int diff = abs((int)frame_data[i] - (int)state->last_frame_data[i]);
        if (diff > threshold) {
            diff_count++;
        }
        sample_count++;
    }

    // Update last frame
    if (size != state->frame_data_size) {
        free(state->last_frame_data);
        state->last_frame_data = (unsigned char*)malloc(size);
        if (!state->last_frame_data) {
            return 0.0;
        }
        state->frame_data_size = size;
    }
    memcpy(state->last_frame_data, frame_data, size);

    return sample_count > 0 ? (float)diff_count / (float)sample_count : 0.0;
}

/**
 * Module initialization
 *
 * NOTE: Core already initializes Larod and loads the model on the DLPU.
 * The detection module uses the core's Larod context via core_api_get_larod().
 * We do NOT create our own Larod connection to avoid DLPU resource conflicts.
 */
static int detection_init(ModuleContext* ctx, cJSON* config) {
    syslog(LOG_INFO, "[%s] Initializing detection module\n", MODULE_NAME);

    // Allocate module state
    DetectionState* state = (DetectionState*)calloc(1, sizeof(DetectionState));
    if (!state) {
        syslog(LOG_ERR, "[%s] Failed to allocate state\n", MODULE_NAME);
        return -1;
    }

    // Load configuration
    state->confidence_threshold = module_config_get_float(config, "confidence_threshold", 0.25);
    state->model_path = module_config_get_string(config, "model_path",
                                                   "/usr/local/packages/axis_is_poc/models/yolov5n_artpec8_coco_640.tflite");

    // Get Larod context from core (which already initialized it)
    // This avoids creating a duplicate DLPU connection that would fail
    state->larod = core_api_get_larod();
    if (!state->larod) {
        syslog(LOG_WARNING, "[%s] Core Larod not available - motion/scene analysis only\n", MODULE_NAME);
    } else {
        syslog(LOG_INFO, "[%s] Using core's Larod context for inference\n", MODULE_NAME);
    }

    ctx->module_state = state;

    syslog(LOG_INFO, "[%s] Initialized (ML=%s) threshold=%.2f\n",
           MODULE_NAME, state->larod ? "enabled" : "disabled", state->confidence_threshold);

    return AXIS_IS_MODULE_SUCCESS;
}

/**
 * Process frame: Run inference + extract metadata
 */
static int detection_process(ModuleContext* ctx, FrameData* frame) {
    DetectionState* state = (DetectionState*)ctx->module_state;
    if (!state) {
        return AXIS_IS_MODULE_NOT_READY;
    }

    int num_detections = 0;
    float inference_time_ms = 0.0f;

    // Run YOLOv5n inference if Larod is available
    if (state->larod) {
        LarodResult* result = Larod_Run_Inference(state->larod, frame->vdo_buffer);
        if (result) {
            // Add detections to metadata
            for (int i = 0; i < result->num_detections; i++) {
                Detection det = {
                    .class_id = result->detections[i].class_id,
                    .confidence = result->detections[i].confidence,
                    .x = result->detections[i].x,
                    .y = result->detections[i].y,
                    .width = result->detections[i].width,
                    .height = result->detections[i].height
                };
                metadata_add_detection(frame->metadata, det);
            }
            num_detections = result->num_detections;
            inference_time_ms = result->inference_time_ms;
            Larod_Free_Result(result);
        } else {
            syslog(LOG_WARNING, "[%s] Inference failed\n", MODULE_NAME);
        }
    }

    // Compute scene hash (works without ML)
    if (frame->frame_data) {
        uint32_t scene_hash = 0;
        size_t frame_size = frame->width * frame->height * 3 / 2;  // YUV420 size
        compute_scene_hash((unsigned char*)frame->frame_data, frame_size, &scene_hash);
        frame->metadata->scene_hash = scene_hash;

        // Compute motion score (works without ML)
        frame->metadata->motion_score = compute_motion_score(state,
                                                              (unsigned char*)frame->frame_data,
                                                              frame_size);
    }

    // Add detection module data to custom metadata
    cJSON* detection_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(detection_data, "inference_time_ms", inference_time_ms);
    cJSON_AddNumberToObject(detection_data, "num_detections", num_detections);
    cJSON_AddNumberToObject(detection_data, "confidence_threshold", state->confidence_threshold);
    cJSON_AddBoolToObject(detection_data, "ml_enabled", state->larod != NULL);
    cJSON_AddItemToObject(frame->metadata->custom_data, "detection", detection_data);

    return AXIS_IS_MODULE_SUCCESS;
}

/**
 * Module cleanup
 *
 * NOTE: We do NOT call Larod_Cleanup() here because the Larod context
 * is owned by core. Core will clean it up when the application shuts down.
 */
static void detection_cleanup(ModuleContext* ctx) {
    DetectionState* state = (DetectionState*)ctx->module_state;
    if (!state) return;

    syslog(LOG_INFO, "[%s] Cleaning up\n", MODULE_NAME);

    // Note: Don't cleanup state->larod - it's borrowed from core
    // Core owns the Larod context and will clean it up

    if (state->last_frame_data) {
        free(state->last_frame_data);
    }

    free(state);
    ctx->module_state = NULL;
}

/**
 * Register detection module
 */
MODULE_REGISTER(detection_module, MODULE_NAME, MODULE_VERSION, MODULE_PRIORITY,
                detection_init, detection_process, detection_cleanup);
