/**
 * metadata.h
 *
 * Metadata extraction for AXION POC
 * Processes frames to extract scene hashes, motion scores, and object information
 */

#ifndef METADATA_H
#define METADATA_H

#include "cJSON.h"
#include "larod_handler.h"
#include "vdo-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Metadata for a single frame */
typedef struct {
    unsigned long sequence;
    unsigned long frame_number;
    char scene_hash[32];
    int scene_changed;
    float motion_score;
    int object_count;
    Detection* objects;
    int num_objects;
    int inference_time_ms;
    long timestamp_ms;
} MetadataFrame;

/* Metadata extraction context */
typedef struct {
    MetadataFrame* last_frame;
    char last_scene_hash[32];
    unsigned char* last_frame_data;
    size_t frame_data_size;
    unsigned long frame_counter;
} MetadataContext;

/**
 * Initialize metadata extractor
 * @return MetadataContext pointer on success, NULL on failure
 */
MetadataContext* Metadata_Init(void);

/**
 * Extract metadata from frame and inference result
 * @param ctx Metadata context
 * @param buffer VDO buffer containing frame
 * @param result Larod inference result
 * @return MetadataFrame pointer on success, NULL on failure
 *
 * IMPORTANT: Caller must call Metadata_Free() when done
 */
MetadataFrame* Metadata_Extract(MetadataContext* ctx, VdoBuffer* buffer, LarodResult* result);

/**
 * Convert metadata to JSON for MQTT publishing
 * @param metadata Metadata frame
 * @return cJSON object on success, NULL on failure
 *
 * IMPORTANT: Caller must call cJSON_Delete() when done
 */
cJSON* Metadata_To_JSON(MetadataFrame* metadata);

/**
 * Free metadata frame
 * @param metadata Metadata frame to free
 */
void Metadata_Free(MetadataFrame* metadata);

/**
 * Cleanup metadata context
 * @param ctx Metadata context
 */
void Metadata_Cleanup(MetadataContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* METADATA_H */
