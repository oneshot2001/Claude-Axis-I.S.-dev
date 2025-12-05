/**
 * metadata.c
 *
 * Metadata extraction implementation for Axis I.S. POC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include "metadata.h"
#include "vdo-buffer.h"

#define LOG_ERR(fmt, args...) { syslog(LOG_ERR, fmt, ## args); fprintf(stderr, fmt, ## args); }

/**
 * Simple hash function for scene detection (not cryptographic)
 * Samples frame data to generate a hash for change detection
 */
static void compute_scene_hash(const unsigned char* data, size_t size, char* hash_out) {
    unsigned long hash = 5381;

    // Sample every 1000th byte to create hash
    for (size_t i = 0; i < size; i += 1000) {
        hash = ((hash << 5) + hash) + data[i];
    }

    snprintf(hash_out, 32, "%016lx", hash);
}

/**
 * Simple motion detection via frame differencing
 * Compares current frame with previous frame
 */
static float compute_motion_score(MetadataContext* ctx, const unsigned char* frame_data, size_t size) {
    if (!ctx->last_frame_data) {
        // First frame - initialize storage
        ctx->last_frame_data = (unsigned char*)malloc(size);
        if (!ctx->last_frame_data) {
            LOG_ERR("Failed to allocate frame buffer for motion detection\n");
            return 0.0;
        }
        ctx->frame_data_size = size;
        memcpy(ctx->last_frame_data, frame_data, size);
        return 0.0;
    }

    int diff_count = 0;
    int threshold = 30;  // Pixel difference threshold
    size_t sample_count = 0;

    // Sample-based comparison (every 100 pixels for performance)
    size_t min_size = (size < ctx->frame_data_size) ? size : ctx->frame_data_size;
    for (size_t i = 0; i < min_size; i += 100) {
        int diff = abs((int)frame_data[i] - (int)ctx->last_frame_data[i]);
        if (diff > threshold) {
            diff_count++;
        }
        sample_count++;
    }

    // Update last frame
    if (size != ctx->frame_data_size) {
        free(ctx->last_frame_data);
        ctx->last_frame_data = (unsigned char*)malloc(size);
        if (!ctx->last_frame_data) {
            LOG_ERR("Failed to reallocate frame buffer\n");
            return 0.0;
        }
        ctx->frame_data_size = size;
    }
    memcpy(ctx->last_frame_data, frame_data, size);

    // Return motion score (0-1)
    return sample_count > 0 ? (float)diff_count / (float)sample_count : 0.0;
}

MetadataContext* Metadata_Init(void) {
    MetadataContext* ctx = (MetadataContext*)calloc(1, sizeof(MetadataContext));
    if (!ctx) {
        LOG_ERR("Failed to allocate Metadata context\n");
        return NULL;
    }

    ctx->frame_counter = 0;
    memset(ctx->last_scene_hash, 0, sizeof(ctx->last_scene_hash));

    return ctx;
}

MetadataFrame* Metadata_Extract(MetadataContext* ctx, VdoBuffer* buffer, LarodResult* result) {
    if (!ctx || !buffer || !result) {
        LOG_ERR("Invalid parameters to Metadata_Extract\n");
        return NULL;
    }

    MetadataFrame* metadata = (MetadataFrame*)calloc(1, sizeof(MetadataFrame));
    if (!metadata) {
        LOG_ERR("Failed to allocate MetadataFrame\n");
        return NULL;
    }

    // Get timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    metadata->timestamp_ms = (tv.tv_sec * 1000LL) + (tv.tv_usec / 1000);

    // Increment frame counter
    metadata->frame_number = ctx->frame_counter++;

    // Get frame data directly from VDO buffer (ACAP SDK doesn't have VdoFrame)
    void* frame_data = vdo_buffer_get_data(buffer);
    // Estimate frame size based on YUV420 format: width * height * 1.5
    // This should be passed from caller or stored in a context
    size_t frame_size = 416 * 416 * 3 / 2;  // Default for 416x416 YUV

    if (frame_data && frame_size > 0) {
        // Compute scene hash
        compute_scene_hash((unsigned char*)frame_data, frame_size, metadata->scene_hash);

        // Check if scene changed
        if (strlen(ctx->last_scene_hash) > 0) {
            metadata->scene_changed = (strcmp(metadata->scene_hash, ctx->last_scene_hash) != 0);
        } else {
            metadata->scene_changed = 1;  // First frame is always a "change"
        }
        strcpy(ctx->last_scene_hash, metadata->scene_hash);

        // Compute motion score
        metadata->motion_score = compute_motion_score(ctx, (unsigned char*)frame_data, frame_size);
    }
    // Note: No vdo_frame_unref needed - buffer release handled by caller

    // Copy detection results
    metadata->object_count = result->num_detections;
    metadata->num_objects = result->num_detections;
    metadata->inference_time_ms = result->inference_time_ms;

    if (result->num_detections > 0) {
        metadata->objects = (Detection*)calloc(result->num_detections, sizeof(Detection));
        if (metadata->objects) {
            memcpy(metadata->objects, result->detections, result->num_detections * sizeof(Detection));
        } else {
            LOG_ERR("Failed to allocate detection array\n");
            metadata->object_count = 0;
            metadata->num_objects = 0;
        }
    }

    // Store as last frame (for HTTP endpoint access)
    if (ctx->last_frame) {
        Metadata_Free(ctx->last_frame);
    }

    // Allocate copy for context storage
    MetadataFrame* stored_metadata = (MetadataFrame*)calloc(1, sizeof(MetadataFrame));
    if (stored_metadata) {
        memcpy(stored_metadata, metadata, sizeof(MetadataFrame));
        if (metadata->objects && metadata->num_objects > 0) {
            stored_metadata->objects = (Detection*)calloc(metadata->num_objects, sizeof(Detection));
            if (stored_metadata->objects) {
                memcpy(stored_metadata->objects, metadata->objects, metadata->num_objects * sizeof(Detection));
            }
        }
        ctx->last_frame = stored_metadata;
    }

    return metadata;
}

cJSON* Metadata_To_JSON(MetadataFrame* metadata) {
    if (!metadata) {
        LOG_ERR("Invalid metadata parameter\n");
        return NULL;
    }

    cJSON* json = cJSON_CreateObject();
    if (!json) {
        LOG_ERR("Failed to create JSON object\n");
        return NULL;
    }

    // Message envelope
    cJSON_AddStringToObject(json, "version", "1.0");
    cJSON_AddStringToObject(json, "msg_type", "metadata");
    cJSON_AddNumberToObject(json, "seq", metadata->sequence);
    cJSON_AddNumberToObject(json, "timestamp", metadata->timestamp_ms);
    cJSON_AddNumberToObject(json, "frame_id", metadata->frame_number);

    // Scene information
    cJSON* scene = cJSON_CreateObject();
    cJSON_AddStringToObject(scene, "hash", metadata->scene_hash);
    cJSON_AddBoolToObject(scene, "changed", metadata->scene_changed);
    cJSON_AddItemToObject(json, "scene", scene);

    // Inference timing
    cJSON* inference = cJSON_CreateObject();
    cJSON_AddNumberToObject(inference, "time_ms", metadata->inference_time_ms);
    cJSON_AddItemToObject(json, "inference", inference);

    // Object detections
    cJSON* detections = cJSON_CreateArray();
    for (int i = 0; i < metadata->object_count && i < metadata->num_objects; i++) {
        cJSON* det = cJSON_CreateObject();
        cJSON_AddNumberToObject(det, "class_id", metadata->objects[i].class_id);
        cJSON_AddNumberToObject(det, "confidence", metadata->objects[i].confidence);
        cJSON_AddNumberToObject(det, "x", metadata->objects[i].x);
        cJSON_AddNumberToObject(det, "y", metadata->objects[i].y);
        cJSON_AddNumberToObject(det, "width", metadata->objects[i].width);
        cJSON_AddNumberToObject(det, "height", metadata->objects[i].height);
        cJSON_AddItemToArray(detections, det);
    }
    cJSON_AddItemToObject(json, "detections", detections);

    // Motion information
    cJSON* motion = cJSON_CreateObject();
    cJSON_AddNumberToObject(motion, "score", metadata->motion_score);
    cJSON_AddBoolToObject(motion, "detected", metadata->motion_score > 0.1);
    cJSON_AddItemToObject(json, "motion", motion);

    return json;
}

void Metadata_Free(MetadataFrame* metadata) {
    if (!metadata) return;

    if (metadata->objects) {
        free(metadata->objects);
    }

    free(metadata);
}

void Metadata_Cleanup(MetadataContext* ctx) {
    if (!ctx) return;

    if (ctx->last_frame) {
        Metadata_Free(ctx->last_frame);
        ctx->last_frame = NULL;
    }

    if (ctx->last_frame_data) {
        free(ctx->last_frame_data);
        ctx->last_frame_data = NULL;
    }

    free(ctx);
}
