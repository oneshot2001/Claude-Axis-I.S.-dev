/**
 * larod_handler.c
 *
 * Larod (ML inference) handler implementation for Axis I.S. POC
 * Executes YOLOv5n INT8 model on DLPU hardware
 *
 * Uses Larod API v3 with larodListDevices() for proper device detection
 * Supports ARTPEC-8, ARTPEC-9, and CPU fallback
 *
 * Reference: https://developer.axis.com/acap/api/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "larod_handler.h"
#include "vdo-buffer.h"

/* Undefine system LOG macros */
#ifdef LOG_ERR
#undef LOG_ERR
#endif

#define LOG(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERR(fmt, args...) { syslog(3, fmt, ## args); fprintf(stderr, fmt, ## args); }

// YOLOv5n model dimensions - using 640x640 to match Axis Model Zoo
// See: https://github.com/AxisCommunications/axis-model-zoo
#define YOLO_INPUT_WIDTH 640
#define YOLO_INPUT_HEIGHT 640
#define YOLO_NUM_CLASSES 80
#define YOLO_MAX_DETECTIONS 100

// Device name patterns for DLPU detection
// ARTPEC-8 uses "axis-a8-dlpu-tflite", ARTPEC-9 uses "a9-dlpu-tflite"
// Use partial match pattern to support both
#define DLPU_A9_DEVICE_NAME "a9-dlpu-tflite"
#define DLPU_A8_DEVICE_NAME "a8-dlpu-tflite"
#define CPU_DEVICE_NAME "cpu-tflite"

/**
 * Parse YOLO output tensor
 * YOLOv5n 640x640 output shape: [1, 25200, 85]
 * 25200 = (80x80 + 40x40 + 20x20) x 3 anchors (for 640x640 input)
 * 85 = (x, y, w, h, objectness, 80 class scores)
 *
 * Note: For INT8 quantized models from Axis Model Zoo, output may need
 * dequantization depending on model export settings.
 */
static void parse_yolo_output(LarodContext* ctx, float* output_data,
                              Detection* detections, int* num_detections) {
    *num_detections = 0;

    // 25200 anchors for 640x640: (80x80 + 40x40 + 20x20) x 3
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

/**
 * Static storage for device list to avoid use-after-free
 * larodListDevices returns borrowed references that become invalid when freed
 */
static larodDevice** g_device_list = NULL;
static size_t g_num_devices = 0;

/**
 * Initialize device list once at startup using Larod API v3
 * Must be called before find_device_by_name()
 */
static int init_device_list(larodConnection* conn) {
    if (g_device_list) return 1;  // Already initialized

    larodError* error = NULL;
    g_device_list = larodListDevices(conn, &g_num_devices, &error);
    if (error || !g_device_list) {
        if (error) {
            LOG("Larod: Failed to list devices: %s\n", error->msg);
            larodClearError(&error);
        }
        return 0;
    }

    LOG("Larod: Found %zu devices\n", g_num_devices);
    for (size_t i = 0; i < g_num_devices; i++) {
        const char* device_name = larodGetDeviceName(g_device_list[i], &error);
        if (error) {
            larodClearError(&error);
            continue;
        }
        LOG("Larod: Device[%zu]: %s\n", i, device_name);
    }

    return 1;
}

/**
 * Find device by name pattern using cached device list
 * Returns device on success, NULL if not found
 */
static larodDevice* find_device_by_name(larodConnection* conn, const char* name_pattern) {
    if (!g_device_list) {
        if (!init_device_list(conn)) return NULL;
    }

    larodError* error = NULL;
    larodDevice* found_device = NULL;

    // Search for matching device
    for (size_t i = 0; i < g_num_devices; i++) {
        const char* device_name = larodGetDeviceName(g_device_list[i], &error);
        if (error) {
            larodClearError(&error);
            continue;
        }

        if (strstr(device_name, name_pattern)) {
            found_device = g_device_list[i];
            LOG("Larod: Selected device: %s\n", device_name);
            break;  // Found it, no need to continue
        }
    }

    return found_device;
}

/**
 * Cleanup device list (call during shutdown)
 */
static void cleanup_device_list(void) {
    if (g_device_list) {
        free(g_device_list);
        g_device_list = NULL;
        g_num_devices = 0;
    }
}

/**
 * Try to load model on a specific device
 * Returns model on success, NULL on failure
 */
static larodModel* try_load_model_v3(larodConnection* conn, const char* model_path,
                                      larodDevice* device, const char* device_desc) {
    larodError* error = NULL;

    int model_fd = open(model_path, O_RDONLY);
    if (model_fd < 0) {
        LOG("Larod: Cannot open model file: %s\n", model_path);
        return NULL;
    }

    LOG("Larod: Loading model %s on %s...\n", model_path, device_desc);

    // Use Larod API v3 model loading with device
    larodModel* model = larodLoadModel(conn, model_fd, device, LAROD_ACCESS_PRIVATE,
                                        "axis_is_yolov5n", NULL, &error);
    close(model_fd);

    if (model && !error) {
        LOG("Larod: Successfully loaded model on %s\n", device_desc);
        return model;
    }

    if (error) {
        LOG("Larod: Failed to load on %s: %s\n", device_desc, error->msg);
        larodClearError(&error);
    }
    return NULL;
}

LarodContext* Larod_Init(const char* model_path, float confidence_threshold) {
    LarodContext* ctx = (LarodContext*)calloc(1, sizeof(LarodContext));
    if (!ctx) {
        LOG_ERR("Failed to allocate Larod context\n");
        return NULL;
    }

    ctx->confidence_threshold = confidence_threshold;
    larodError* error = NULL;

    // Connect to Larod service
    if (!larodConnect(&ctx->conn, &error)) {
        LOG_ERR("Failed to connect to Larod: %s\n", error ? error->msg : "unknown");
        if (error) larodClearError(&error);
        free(ctx);
        return NULL;
    }

    LOG("Larod: Connected to larod service\n");
    LOG("Larod: Auto-detecting available inference devices...\n");

    // Determine model paths for ARTPEC-8 and ARTPEC-9
    // Both use the same DLPU device name but need different model files
    char artpec9_path[512] = {0};
    char artpec8_path[512] = {0};

    if (strstr(model_path, "artpec8")) {
        // Config specifies artpec8 - derive artpec9 path
        strncpy(artpec8_path, model_path, sizeof(artpec8_path) - 1);
        strncpy(artpec9_path, model_path, sizeof(artpec9_path) - 1);
        char* p = strstr(artpec9_path, "artpec8");
        if (p) memcpy(p, "artpec9", 7);
    } else if (strstr(model_path, "artpec9")) {
        // Config specifies artpec9 - derive artpec8 path
        strncpy(artpec9_path, model_path, sizeof(artpec9_path) - 1);
        strncpy(artpec8_path, model_path, sizeof(artpec8_path) - 1);
        char* p = strstr(artpec8_path, "artpec9");
        if (p) memcpy(p, "artpec8", 7);
    } else {
        // Generic path - use as-is for both
        strncpy(artpec9_path, model_path, sizeof(artpec9_path) - 1);
        strncpy(artpec8_path, model_path, sizeof(artpec8_path) - 1);
    }

    // Find available DLPU devices - ARTPEC-9 uses "a9-dlpu-tflite", ARTPEC-8 uses patterns with "a8-dlpu"
    larodDevice* dlpu_a9_device = find_device_by_name(ctx->conn, DLPU_A9_DEVICE_NAME);
    larodDevice* dlpu_a8_device = find_device_by_name(ctx->conn, DLPU_A8_DEVICE_NAME);
    larodDevice* cpu_device = find_device_by_name(ctx->conn, CPU_DEVICE_NAME);

    // Try ARTPEC-9 DLPU first (for P3285-LVE and other ARTPEC-9 cameras)
    if (dlpu_a9_device && access(artpec9_path, R_OK) == 0) {
        LOG("Larod: Trying ARTPEC-9 DLPU with model: %s\n", artpec9_path);
        ctx->model = try_load_model_v3(ctx->conn, artpec9_path, dlpu_a9_device, "DLPU (ARTPEC-9)");
    }

    // Try ARTPEC-8 DLPU
    if (!ctx->model && dlpu_a8_device && access(artpec8_path, R_OK) == 0) {
        LOG("Larod: Trying ARTPEC-8 DLPU with model: %s\n", artpec8_path);
        ctx->model = try_load_model_v3(ctx->conn, artpec8_path, dlpu_a8_device, "DLPU (ARTPEC-8)");
    }

    // Fallback to CPU with any available model
    if (!ctx->model && cpu_device) {
        if (access(artpec9_path, R_OK) == 0) {
            ctx->model = try_load_model_v3(ctx->conn, artpec9_path, cpu_device, "CPU (ARTPEC-9 model)");
        }
        if (!ctx->model && access(artpec8_path, R_OK) == 0) {
            ctx->model = try_load_model_v3(ctx->conn, artpec8_path, cpu_device, "CPU (ARTPEC-8 model)");
        }
        if (!ctx->model && access(model_path, R_OK) == 0) {
            ctx->model = try_load_model_v3(ctx->conn, model_path, cpu_device, "CPU (original model)");
        }
    }

    if (!ctx->model) {
        LOG_ERR("Failed to load model on any available device\n");
        LOG_ERR("Tried paths: %s, %s\n", artpec9_path, artpec8_path);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    // Allocate input and output tensors using Larod API v3 signatures
    // larodAllocModelInputs(conn, model, fdPropFlags, numTensors, params, error)
    ctx->input_tensors = larodAllocModelInputs(ctx->conn, ctx->model, 0, &ctx->num_inputs, NULL, &error);
    if (!ctx->input_tensors || error) {
        LOG_ERR("Failed to allocate input tensors: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        larodDestroyModel(&ctx->model);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    // larodAllocModelOutputs(conn, model, fdPropFlags, numTensors, params, error)
    ctx->output_tensors = larodAllocModelOutputs(ctx->conn, ctx->model, 0, &ctx->num_outputs, NULL, &error);
    if (!ctx->output_tensors || error) {
        LOG_ERR("Failed to allocate output tensors: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        larodDestroyTensors(ctx->conn, &ctx->input_tensors, ctx->num_inputs, NULL);
        larodDestroyModel(&ctx->model);
        larodDisconnect(&ctx->conn, NULL);
        free(ctx);
        return NULL;
    }

    LOG("Larod initialized: Inputs=%zu Outputs=%zu Threshold=%.2f\n",
        ctx->num_inputs, ctx->num_outputs, confidence_threshold);
    return ctx;
}

LarodResult* Larod_Run_Inference(LarodContext* ctx, VdoBuffer* vdo_buffer) {
    if (!ctx || !vdo_buffer) {
        LOG_ERR("Invalid parameters to Larod_Run_Inference\n");
        return NULL;
    }

    // Validate Larod context has tensors allocated
    if (!ctx->input_tensors || ctx->num_inputs == 0) {
        LOG_ERR("Larod context has no input tensors allocated\n");
        return NULL;
    }
    if (!ctx->output_tensors || ctx->num_outputs == 0) {
        LOG_ERR("Larod context has no output tensors allocated\n");
        return NULL;
    }
    if (!ctx->input_tensors[0]) {
        LOG_ERR("Larod input tensor[0] is NULL\n");
        return NULL;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Get frame data directly from VDO buffer
    void* frame_data = vdo_buffer_get_data(vdo_buffer);
    if (!frame_data) {
        LOG_ERR("Failed to get frame data from VDO buffer\n");
        return NULL;
    }

    larodError* error = NULL;

    // Get input tensor file descriptor for mmap access
    int input_fd = larodGetTensorFd(ctx->input_tensors[0], &error);
    if (input_fd < 0 || error) {
        LOG_ERR("Failed to get input tensor fd: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        return NULL;
    }

    // Get tensor size for memory mapping
    size_t tensor_size = 0;
    if (!larodGetTensorFdSize(ctx->input_tensors[0], &tensor_size, &error) || error) {
        LOG_ERR("Failed to get tensor size: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        return NULL;
    }

    // Map input tensor memory
    void* input_data = mmap(NULL, tensor_size, PROT_READ | PROT_WRITE, MAP_SHARED, input_fd, 0);
    if (input_data == MAP_FAILED) {
        LOG_ERR("Failed to mmap input tensor\n");
        return NULL;
    }

    // Copy frame data to input tensor
    // NOTE: In production, this would include YUV→RGB conversion and normalization
    memcpy(input_data, frame_data, tensor_size);

    // Unmap input tensor
    munmap(input_data, tensor_size);

    // Create and run job request
    larodJobRequest* req = larodCreateJobRequest(
        ctx->model,
        ctx->input_tensors,
        ctx->num_inputs,
        ctx->output_tensors,
        ctx->num_outputs,
        NULL,  // No crop map
        &error
    );

    if (!req || error) {
        LOG_ERR("Failed to create job request: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        return NULL;
    }

    // Run inference synchronously
    if (!larodRunJob(ctx->conn, req, &error)) {
        LOG_ERR("Inference failed: %s\n", error ? error->msg : "Unknown error");
        if (error) larodClearError(&error);
        larodDestroyJobRequest(&req);
        return NULL;
    }

    larodDestroyJobRequest(&req);

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

    // Validate output tensor before access
    if (!ctx->output_tensors[0]) {
        LOG_ERR("Larod output tensor[0] is NULL\n");
        free(result->detections);
        free(result);
        return NULL;
    }

    // Get output tensor file descriptor and map memory to read results
    int output_fd = larodGetTensorFd(ctx->output_tensors[0], &error);
    if (output_fd < 0 || error) {
        LOG_ERR("Failed to get output tensor fd: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        free(result->detections);
        free(result);
        return NULL;
    }

    size_t output_size = 0;
    if (!larodGetTensorFdSize(ctx->output_tensors[0], &output_size, &error) || error) {
        LOG_ERR("Failed to get output tensor size: %s\n", error ? error->msg : "Unknown");
        if (error) larodClearError(&error);
        free(result->detections);
        free(result);
        return NULL;
    }

    float* output_data = mmap(NULL, output_size, PROT_READ, MAP_SHARED, output_fd, 0);
    if (output_data == MAP_FAILED) {
        LOG_ERR("Failed to mmap output tensor\n");
        free(result->detections);
        free(result);
        return NULL;
    }

    // Parse YOLO output format
    parse_yolo_output(ctx, output_data, result->detections, &result->num_detections);

    // Unmap output tensor
    munmap(output_data, output_size);

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
        larodDestroyTensors(ctx->conn, &ctx->input_tensors, ctx->num_inputs, NULL);
    }
    if (ctx->output_tensors) {
        larodDestroyTensors(ctx->conn, &ctx->output_tensors, ctx->num_outputs, NULL);
    }
    if (ctx->model) {
        larodDestroyModel(&ctx->model);
    }
    if (ctx->conn) {
        larodDisconnect(&ctx->conn, NULL);
    }

    // Cleanup static device list
    cleanup_device_list();

    free(ctx);
}
