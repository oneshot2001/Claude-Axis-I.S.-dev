/**
 * larod_handler.h
 *
 * Larod (ML inference) handler for AXION POC
 * Executes YOLOv5n INT8 model on DLPU hardware
 */

#ifndef LAROD_HANDLER_H
#define LAROD_HANDLER_H

#include "vdo-types.h"
#include "larod.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Detection structure (YOLO output) */
typedef struct {
    int class_id;
    float confidence;
    float x, y, width, height;  // Normalized coordinates (0-1)
} Detection;

/* Larod inference result */
typedef struct {
    Detection* detections;
    int num_detections;
    int inference_time_ms;
} LarodResult;

/* Larod context */
typedef struct {
    larodConnection* conn;
    larodModel* model;
    larodTensor** input_tensors;
    larodTensor** output_tensors;
    size_t num_inputs;
    size_t num_outputs;
    float confidence_threshold;
    int total_inferences;
    int total_time_ms;
} LarodContext;

/**
 * Initialize Larod inference engine
 * @param model_path Path to TFLite model file
 * @param confidence_threshold Minimum confidence for detections (0-1)
 * @return LarodContext pointer on success, NULL on failure
 */
LarodContext* Larod_Init(const char* model_path, float confidence_threshold);

/**
 * Run inference on frame
 * @param ctx Larod context
 * @param vdo_buffer VDO frame buffer
 * @return LarodResult pointer on success, NULL on failure
 *
 * IMPORTANT: Caller must call Larod_Free_Result() when done
 */
LarodResult* Larod_Run_Inference(LarodContext* ctx, VdoBuffer* vdo_buffer);

/**
 * Free inference result
 * @param result LarodResult to free
 */
void Larod_Free_Result(LarodResult* result);

/**
 * Get average inference time
 * @param ctx Larod context
 * @return Average inference time in milliseconds
 */
int Larod_Get_Avg_Time(LarodContext* ctx);

/**
 * Cleanup Larod resources
 * @param ctx Larod context
 */
void Larod_Cleanup(LarodContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* LAROD_HANDLER_H */
