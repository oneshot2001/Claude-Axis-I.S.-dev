/**
 * larod_handler.c
 *
 * Larod (ML inference) handler implementation for AXION POC
 * Executes YOLOv5n INT8 model on DLPU hardware
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include "larod_handler.h"
#include "vdo-frame.h"

#define LOG(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERR(fmt, args...) { syslog(LOG_ERR, fmt, ## args); fprintf(stderr, fmt, ## args); }

#define YOLO_INPUT_WIDTH 416
#define YOLO_INPUT_HEIGHT 416
#define YOLO_NUM_CLASSES 80
#define YOLO_MAX_DETECTIONS 100

/**
 * Parse YOLO output tensor
 * YOLOv5n output shape: [1, 25200, 85]
 * 25200 = (80x80 + 40x40 + 20x20) x 3 anchors
 * 85 = (x, y, w, h, objectness, 80 class scores)
 */
static void parse_yolo_output(LarodContext* ctx, float* output_data,
                              Detection* detections, int* num_detections) {
    *num_detections = 0;

    for (int i = 0; i < 25200 && *num_detections < YOLO_MAX_DETECTIONS; i++) {
        float* detection = &output_data[i * 85];
        float objectness = detection[4];

        if (objectness < ctx->confidence_threshold) continue;

        // Find class with highest score
        int best_class = 0;
        float best_score = detection[5];
        for (int c = 1; c < YOLO_NUM_CLASSES; c++) {
            if (detection[5 + c] > best_score) {
                best_score = detection[5 + c];
                best_class = c;
            }
        }

        // Combined confidence
        float final_confidence = objectness * best_score;
        if (final_confidence < ctx->confidence_threshold) continue;

        Detection* det = &detections[*num_detections];
        det->class_id = best_class;
        det->confidence = final_confidence;
        // Normalize coordinates to 0-1
        det->x = detection[0] / (float)YOLO_INPUT_WIDTH;
        det->y = detection[1] / (float)YOLO_INPUT_HEIGHT;
        det->width = detection[2] / (float)YOLO_INPUT_WIDTH;
        det->height = detection[3] / (float)YOLO_INPUT_HEIGHT;

        (*num_detections)++;
    }
}

LarodContext* Larod_Init(const char* model_path, float confidence_threshold) {
    LarodContext* ctx = (LarodContext*)calloc(1, sizeof(LarodContext));
    if (!ctx) {
        LOG_ERR("Failed to allocate Larod context\n");
        return NULL;
    }

    ctx->confidence_threshold = confidence_threshold;
    larodError* error = NULL;

    // Connect to Larod
    ctx->conn = larodConnect(NULL, &error);
    if (error) {
        LOG_ERR("Failed to connect to Larod: %s\n", error->msg);
        larodClearError(&error);
        free(ctx);
        return NULL;
    }

    // Load model
    ctx->model = larodLoadModel(ctx->conn, model_path, LAROD_ACCESS_PRIVATE,
                                 "tflite", LAROD_CHIP_DLPU, &error);
    if (error) {
        LOG_ERR("Failed to load model %s: %s\n", model_path, error->msg);
        larodClearError(&error);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    // Create input and output tensors
    ctx->input_tensors = larodCreateModelInputs(ctx->model, &ctx->num_inputs, &error);
    if (error) {
        LOG_ERR("Failed to create input tensors: %s\n", error->msg);
        larodClearError(&error);
        larodDestroyModel(&ctx->model);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    ctx->output_tensors = larodCreateModelOutputs(ctx->model, &ctx->num_outputs, &error);
    if (error) {
        LOG_ERR("Failed to create output tensors: %s\n", error->msg);
        larodClearError(&error);
        larodDestroyTensors(ctx->input_tensors, ctx->num_inputs);
        larodDestroyModel(&ctx->model);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    LOG("Larod initialized: Model=%s Inputs=%zu Outputs=%zu Threshold=%.2f\n",
        model_path, ctx->num_inputs, ctx->num_outputs, confidence_threshold);
    return ctx;
}

LarodResult* Larod_Run_Inference(LarodContext* ctx, VdoBuffer* vdo_buffer) {
    if (!ctx || !vdo_buffer) {
        LOG_ERR("Invalid parameters to Larod_Run_Inference\n");
        return NULL;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Get frame data from VDO buffer
    VdoFrame* frame = vdo_buffer_get_frame(vdo_buffer);
    if (!frame) {
        LOG_ERR("Failed to get VDO frame\n");
        return NULL;
    }

    void* frame_data = vdo_frame_get_data(frame);
    if (!frame_data) {
        LOG_ERR("Failed to get frame data\n");
        vdo_frame_unref(frame);
        return NULL;
    }

    // Copy frame data to input tensor
    // NOTE: In production, this would include YUV→RGB conversion and normalization
    void* input_data = larodGetTensorData(ctx->input_tensors[0]);
    size_t tensor_size = larodGetTensorDataSize(ctx->input_tensors[0]);
    size_t frame_size = vdo_frame_get_size(frame);

    if (tensor_size > frame_size) {
        LOG_ERR("Tensor size (%zu) > frame size (%zu)\n", tensor_size, frame_size);
        vdo_frame_unref(frame);
        return NULL;
    }

    memcpy(input_data, frame_data, tensor_size);
    vdo_frame_unref(frame);

    // Run inference
    larodError* error = NULL;
    if (!larodRunInference(ctx->conn, ctx->model, ctx->input_tensors,
                           ctx->output_tensors, &error)) {
        LOG_ERR("Inference failed: %s\n", error ? error->msg : "Unknown error");
        if (error) larodClearError(&error);
        return NULL;
    }

    gettimeofday(&end, NULL);
    int inference_ms = (int)(((end.tv_sec - start.tv_sec) * 1000) +
                             ((end.tv_usec - start.tv_usec) / 1000));

    // Parse output tensor
    LarodResult* result = (LarodResult*)calloc(1, sizeof(LarodResult));
    if (!result) {
        LOG_ERR("Failed to allocate result\n");
        return NULL;
    }

    result->detections = (Detection*)calloc(YOLO_MAX_DETECTIONS, sizeof(Detection));
    if (!result->detections) {
        LOG_ERR("Failed to allocate detections\n");
        free(result);
        return NULL;
    }

    result->inference_time_ms = inference_ms;

    // Get output data and parse YOLO format
    float* output_data = (float*)larodGetTensorData(ctx->output_tensors[0]);
    parse_yolo_output(ctx, output_data, result->detections, &result->num_detections);

    // Update statistics
    ctx->total_inferences++;
    ctx->total_time_ms += inference_ms;

    return result;
}

void Larod_Free_Result(LarodResult* result) {
    if (!result) return;
    if (result->detections) {
        free(result->detections);
    }
    free(result);
}

int Larod_Get_Avg_Time(LarodContext* ctx) {
    if (!ctx || ctx->total_inferences == 0) return 0;
    return ctx->total_time_ms / ctx->total_inferences;
}

void Larod_Cleanup(LarodContext* ctx) {
    if (!ctx) return;

    LOG("Larod cleanup: Inferences=%d AvgTime=%dms\n",
        ctx->total_inferences, Larod_Get_Avg_Time(ctx));

    if (ctx->input_tensors) {
        larodDestroyTensors(ctx->input_tensors, ctx->num_inputs);
    }
    if (ctx->output_tensors) {
        larodDestroyTensors(ctx->output_tensors, ctx->num_outputs);
    }
    if (ctx->model) {
        larodDestroyModel(&ctx->model);
    }
    if (ctx->conn) {
        larodDisconnect(&ctx->conn, NULL);
    }

    free(ctx);
}
