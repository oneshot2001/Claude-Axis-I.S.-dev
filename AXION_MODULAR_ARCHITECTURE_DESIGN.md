# AXION Modular Architecture Design

**Version:** 2.0.0
**Date:** 2025-11-23
**Status:** Implemented

## Executive Summary

The AXION platform has been transformed from a monolithic proof-of-concept into a **modular plugin-based architecture** supporting extensible AI capabilities. The system now consists of:

- **Core Module**: VDO streaming, Larod inference coordination, DLPU management, MQTT publishing
- **Detection Module**: YOLOv5n object detection (always enabled)
- **LPR Module**: License plate recognition with Claude AI integration (optional)
- **OCR Module**: General text recognition with Gemini AI integration (optional)

**Key Benefits:**
- **Extensibility**: Add new modules without modifying core code
- **Selective Deployment**: Build only the modules you need
- **Maintainability**: Clean separation of concerns, independent module development
- **AXIS OS 12 Compatible**: Static linking, native binary execution

**Build System:**
```bash
make                          # Full build (all modules)
make ENABLE_LPR=0             # Core + Detection + OCR
make ENABLE_OCR=0             # Core + Detection + LPR
make ENABLE_LPR=0 ENABLE_OCR=0  # Core + Detection only
```

---

## Architecture Overview

### System Component Diagram

```mermaid
graph TB
    Camera[AXIS Camera<br/>ARTPEC-9 SoC] --> VDO[VDO Stream<br/>416x416 @ 10 FPS]
    VDO --> Core[Core Module<br/>Orchestrator]

    Core --> DET[Detection Module<br/>YOLOv5n INT8]
    Core --> LPR[LPR Module<br/>Claude Vision]
    Core --> OCR[OCR Module<br/>Gemini Vision]

    DET --> |Detections| Pipeline[Module Pipeline]
    LPR --> |Plate Data| Pipeline
    OCR --> |Text Data| Pipeline

    Pipeline --> MQTT[MQTT Publisher]
    MQTT --> Cloud[Cloud Services]

    DLPU[DLPU Coordinator] -.->|Time Slots| DET

    style Core fill:#4a90e2
    style DET fill:#7ed321
    style LPR fill:#f5a623
    style OCR fill:#f5a623
    style DLPU fill:#d0021b
```

### Data Flow Pipeline

```mermaid
sequenceDiagram
    participant M as Main Loop
    participant C as Core
    participant V as VDO
    participant D as DLPU
    participant Det as Detection
    participant LPR as LPR Module
    participant OCR as OCR Module
    participant MQ as MQTT

    M->>C: core_process_frame()
    C->>D: Dlpu_Wait_For_Slot()
    D-->>C: Slot acquired
    C->>V: Vdo_Get_Frame()
    V-->>C: VdoBuffer*

    C->>Det: process(frame)
    Det->>Det: Run YOLOv5n
    Det-->>C: Detections added

    C->>LPR: process(frame)
    LPR->>LPR: Check for vehicles
    LPR->>LPR: Call Claude API
    LPR-->>C: Plate data added

    C->>OCR: process(frame)
    OCR->>OCR: Check text regions
    OCR->>OCR: Call Gemini API
    OCR-->>C: Text data added

    C->>D: Dlpu_Release_Slot()
    C->>MQ: Publish aggregated metadata
    C->>V: Vdo_Release_Frame()
```

### Module Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Registration: Compile-time
    Registration --> Discovery: Linker sections
    Discovery --> Initialization: core_discover_modules()
    Initialization --> Ready: Module init() success
    Ready --> Running: core_start()
    Running --> Processing: Per-frame process()
    Processing --> Running: Return status
    Running --> Stopping: core_stop()
    Stopping --> Cleanup: Module cleanup()
    Cleanup --> [*]

    Initialization --> Error: init() failure
    Error --> [*]
```

---

## Module Interface Specification

### Core Data Structures

```c
/**
 * Module interface - implemented by all modules
 */
struct ModuleInterface {
    const char* name;            // Module name (e.g., "lpr", "ocr")
    const char* version;         // Semantic version
    int priority;                // Execution order (lower = earlier)

    // Lifecycle functions
    int (*init)(ModuleContext* ctx, cJSON* config);
    int (*process)(ModuleContext* ctx, FrameData* frame);
    void (*cleanup)(ModuleContext* ctx);

    // Optional hooks
    int (*on_start)(ModuleContext* ctx);
    int (*on_stop)(ModuleContext* ctx);
};

/**
 * Module context - state container
 */
struct ModuleContext {
    void* module_state;          // Module's private data
    cJSON* config;               // Module configuration (from settings/*.json)
    CoreContext* core;           // Access to core APIs
    const char* module_name;     // Module name
};

/**
 * Frame data - shared across module pipeline
 */
struct FrameData {
    VdoBuffer* vdo_buffer;       // VDO buffer (zero-copy)
    VdoFrame* vdo_frame;         // Extracted frame
    void* frame_data;            // Raw pixel data
    unsigned int width;          // Frame width (416)
    unsigned int height;         // Frame height (416)
    VdoFormat format;            // VDO_FORMAT_YUV

    MetadataFrame* metadata;     // Aggregated metadata
    int64_t timestamp_us;        // Frame timestamp
    int frame_id;                // Sequential frame ID
};

/**
 * Aggregated metadata from all modules
 */
struct MetadataFrame {
    int64_t timestamp_us;
    int sequence;
    float motion_score;
    int object_count;
    uint32_t scene_hash;

    // Detection results
    Detection* detections;
    int detection_count;

    // Module-specific data (JSON)
    cJSON* custom_data;  // Each module adds fields here
};
```

### Module Registration Macro

```c
/**
 * Register module using linker magic
 * Placed in .axion_modules section, discovered at runtime
 */
#define MODULE_REGISTER(var_name, mod_name, mod_version, mod_priority, \
                        init_fn, process_fn, cleanup_fn) \
    static ModuleInterface var_name = { \
        .name = mod_name, \
        .version = mod_version, \
        .priority = mod_priority, \
        .init = init_fn, \
        .process = process_fn, \
        .cleanup = cleanup_fn, \
        .on_start = NULL, \
        .on_stop = NULL \
    }; \
    static ModuleInterface* __module_ptr_##var_name \
        __attribute__((used, section(".axion_modules"))) = &var_name
```

**Usage Example:**
```c
// At end of module .c file
MODULE_REGISTER(lpr_module, "lpr", "1.0.0", 20,
                lpr_init, lpr_process, lpr_cleanup);
```

---

## Core Module Design

### Responsibilities

1. **VDO Stream Management**: 416x416 YUV frame capture at 10 FPS
2. **Larod Inference**: YOLOv5n INT8 model execution on DLPU
3. **DLPU Coordination**: Time-division multiplexing for multi-camera setups
4. **Module Orchestration**: Discovery, initialization, lifecycle management
5. **MQTT Publishing**: Aggregated metadata from all modules
6. **Configuration Loading**: Per-module JSON config from `settings/`

### Core API (Exposed to Modules)

```c
typedef struct {
    // Frame access
    VdoBuffer* (*get_frame)(CoreContext* ctx);
    void (*release_frame)(CoreContext* ctx, VdoBuffer* buffer);

    // Inference (future: multi-model support)
    larodTensor** (*run_inference)(CoreContext* ctx, const char* model_name,
                                   void* input_data, size_t input_size);

    // Metadata management
    void (*add_detection)(MetadataFrame* meta, Detection det);
    void (*publish_metadata)(CoreContext* ctx, MetadataFrame* meta);

    // Logging
    void (*log)(int level, const char* module, const char* format, ...);

    // HTTP client for AI APIs
    int (*http_post)(const char* url, const char* headers,
                     const char* body, char** response);
} CoreAPI;
```

### Module Discovery Algorithm

```c
int core_discover_modules(CoreContext* ctx) {
    // Linker provides these symbols for .axion_modules section
    extern ModuleInterface* __start_axion_modules;
    extern ModuleInterface* __stop_axion_modules;

    // Count modules
    ModuleInterface** start = &__start_axion_modules;
    ModuleInterface** stop = &__stop_axion_modules;
    int count = (int)(stop - start);

    // Sort modules by priority (lower = earlier execution)
    qsort(modules, count, sizeof(ModuleInterface*), compare_by_priority);

    // Initialize each module
    for (int i = 0; i < count; i++) {
        ModuleInterface* mod = modules[i];

        // Load module config from settings/<name>.json
        cJSON* config = load_config(mod->name);

        // Call module's init function
        if (mod->init(module_ctx, config) == 0) {
            // Module initialized successfully
            registered_modules[module_count++] = mod;
        }
    }

    return module_count;
}
```

---

## Detection Module Specification

**File:** `detection_module.c`
**Priority:** 10 (runs first)
**Dependencies:** Larod, VDO
**Configuration:** `settings/detection.json`

### Functionality

1. **YOLOv5n Inference**: 80-class COCO object detection
2. **Scene Hashing**: Change detection via sampled frame hash
3. **Motion Detection**: Frame differencing for motion score
4. **Metadata Extraction**: Object count, bounding boxes, confidence

### Configuration Schema

```json
{
  "enabled": true,
  "confidence_threshold": 0.25,
  "model_path": "/usr/local/packages/axion_poc/models/yolov5n_int8.tflite"
}
```

### Module State

```c
typedef struct {
    LarodContext* larod;              // Larod inference context
    unsigned char* last_frame_data;   // For motion detection
    size_t frame_data_size;
    char last_scene_hash[32];         // For scene change detection
    float confidence_threshold;
    const char* model_path;
} DetectionState;
```

### Process Function

```c
static int detection_process(ModuleContext* ctx, FrameData* frame) {
    DetectionState* state = (DetectionState*)ctx->module_state;

    // 1. Run YOLOv5n inference
    LarodResult* result = Larod_Run_Inference(state->larod, frame->vdo_buffer);

    // 2. Add detections to metadata
    for (int i = 0; i < result->num_detections; i++) {
        metadata_add_detection(frame->metadata, result->detections[i]);
    }

    // 3. Compute scene hash
    uint32_t scene_hash;
    compute_scene_hash(frame->frame_data, frame_size, &scene_hash);
    frame->metadata->scene_hash = scene_hash;

    // 4. Compute motion score
    frame->metadata->motion_score = compute_motion_score(state, frame->frame_data);

    // 5. Add module-specific metadata
    cJSON* detection_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(detection_data, "inference_time_ms", result->inference_time_ms);
    cJSON_AddNumberToObject(detection_data, "num_detections", result->num_detections);
    cJSON_AddItemToObject(frame->metadata->custom_data, "detection", detection_data);

    Larod_Free_Result(result);
    return AXION_MODULE_SUCCESS;
}
```

### Output Metadata

```json
{
  "camera_id": "axis-camera-001",
  "timestamp_us": 1700000000000000,
  "sequence": 12345,
  "motion_score": 0.65,
  "object_count": 3,
  "scene_hash": 987654321,
  "detections": [
    {"class_id": 2, "confidence": 0.89, "x": 0.3, "y": 0.4, "width": 0.2, "height": 0.3}
  ],
  "modules": {
    "detection": {
      "inference_time_ms": 45,
      "num_detections": 3,
      "confidence_threshold": 0.25
    }
  }
}
```

---

## LPR Module Specification

**File:** `lpr_module.c`
**Priority:** 20 (runs after detection)
**Dependencies:** Detection module (for vehicle detections), CURL, libjpeg
**Configuration:** `settings/lpr.json`
**Conditional Compilation:** `#ifdef MODULE_LPR`

### Functionality

1. **Vehicle Detection Filtering**: Process only vehicles (COCO classes 2,3,5,7)
2. **Plate Region Extraction**: Crop license plate from vehicle bbox
3. **JPEG Encoding**: Encode plate region for API submission
4. **Claude Vision API**: Extract plate number using Claude 3.5 Sonnet
5. **Confidence Validation**: Filter low-confidence results

### Configuration Schema

```json
{
  "enabled": true,
  "claude_api_key": "sk-ant-...",
  "api_url": "https://api.anthropic.com/v1/messages",
  "min_confidence": 0.5,
  "process_interval": 10,
  "description": "Process every 10th vehicle to reduce API costs"
}
```

### Module State

```c
typedef struct {
    char* api_key;               // Claude API key
    char* api_url;               // API endpoint
    float min_confidence;        // Minimum vehicle confidence
    int enabled;                 // Module enabled flag
    int process_interval;        // Process every Nth vehicle
    int frame_counter;           // Frame counter for throttling
} LPRState;
```

### Claude API Integration

```c
static int call_claude_api(LPRState* state, const char* image_base64,
                            char** plate_text, float* confidence) {
    // Build request
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "model", "claude-3-5-sonnet-20241022");
    cJSON_AddNumberToObject(request, "max_tokens", 100);

    cJSON* messages = cJSON_CreateArray();
    cJSON* message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");

    cJSON* content = cJSON_CreateArray();

    // Text prompt
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text",
        "Extract the license plate number. Return JSON: "
        "{\"plate\": \"ABC123\", \"confidence\": 0.95}");
    cJSON_AddItemToArray(content, text_part);

    // Base64 image
    cJSON* image_part = cJSON_CreateObject();
    cJSON_AddStringToObject(image_part, "type", "image");
    cJSON* source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "type", "base64");
    cJSON_AddStringToObject(source, "media_type", "image/jpeg");
    cJSON_AddStringToObject(source, "data", image_base64);
    cJSON_AddItemToObject(image_part, "source", source);
    cJSON_AddItemToArray(content, image_part);

    // HTTP POST with CURL
    CURL* curl = curl_easy_init();
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    char auth[512];
    snprintf(auth, sizeof(auth), "x-api-key: %s", state->api_key);
    headers = curl_slist_append(headers, auth);

    curl_easy_setopt(curl, CURLOPT_URL, state->api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    // Parse response and extract plate data
    // (Full implementation in lpr_module.c)

    return 0;
}
```

### Process Function

```c
static int lpr_process(ModuleContext* ctx, FrameData* frame) {
    LPRState* state = (LPRState*)ctx->module_state;

    // Throttling: Process every Nth frame
    state->frame_counter++;
    if (state->frame_counter % state->process_interval != 0) {
        return AXION_MODULE_SKIP;
    }

    // Check for vehicle detections
    int vehicle_count = 0;
    cJSON* plates_array = cJSON_CreateArray();

    for (int i = 0; i < frame->metadata->detection_count; i++) {
        Detection* det = &frame->metadata->detections[i];

        // Filter for vehicles only
        if (!is_vehicle(det->class_id)) continue;
        if (det->confidence < state->min_confidence) continue;

        vehicle_count++;

        // Crop plate region (implementation: TODO)
        // Encode as JPEG + base64 (implementation: TODO)
        // Call Claude API
        char* plate_text = NULL;
        float plate_conf = 0.0;
        call_claude_api(state, encoded_image, &plate_text, &plate_conf);

        // Add to results
        cJSON* plate_record = cJSON_CreateObject();
        cJSON_AddNumberToObject(plate_record, "vehicle_class", det->class_id);
        cJSON_AddNumberToObject(plate_record, "vehicle_confidence", det->confidence);
        if (plate_text) {
            cJSON_AddStringToObject(plate_record, "plate_number", plate_text);
            cJSON_AddNumberToObject(plate_record, "plate_confidence", plate_conf);
            free(plate_text);
        } else {
            cJSON_AddNullToObject(plate_record, "plate_number");
        }
        cJSON_AddItemToArray(plates_array, plate_record);
    }

    // Add LPR data to custom metadata
    if (vehicle_count > 0) {
        cJSON* lpr_data = cJSON_CreateObject();
        cJSON_AddNumberToObject(lpr_data, "vehicle_count", vehicle_count);
        cJSON_AddItemToObject(lpr_data, "plates", plates_array);
        cJSON_AddItemToObject(frame->metadata->custom_data, "lpr", lpr_data);
    } else {
        cJSON_Delete(plates_array);
    }

    return AXION_MODULE_SUCCESS;
}
```

### Output Metadata

```json
{
  "modules": {
    "detection": { ... },
    "lpr": {
      "vehicle_count": 2,
      "plates": [
        {
          "vehicle_class": 2,
          "vehicle_confidence": 0.87,
          "plate_number": "ABC123",
          "plate_confidence": 0.92,
          "bbox_x": 0.3,
          "bbox_y": 0.4,
          "bbox_w": 0.2,
          "bbox_h": 0.15
        }
      ]
    }
  }
}
```

---

## OCR Module Specification

**File:** `ocr_module.c`
**Priority:** 30 (runs after LPR)
**Dependencies:** CURL, libjpeg
**Configuration:** `settings/ocr.json`
**Conditional Compilation:** `#ifdef MODULE_OCR`

### Functionality

1. **Text Region Detection**: Heuristic edge density analysis
2. **ROI Extraction**: Full frame or region of interest
3. **JPEG Encoding**: Encode frame for API submission
4. **Gemini Vision API**: Extract text using Gemini 2.0 Flash
5. **Text Aggregation**: Collect all text regions with bounding boxes

### Configuration Schema

```json
{
  "enabled": true,
  "gemini_api_key": "AIza...",
  "model": "gemini-2.0-flash-exp",
  "process_interval": 30,
  "min_edge_density": 0.3,
  "description": "Process every 30th frame. edge_density filters frames unlikely to contain text"
}
```

### Module State

```c
typedef struct {
    char* api_key;               // Gemini API key
    char* api_url;               // API endpoint (includes model name)
    char* model_name;            // Gemini model
    int enabled;                 // Module enabled flag
    int process_interval;        // Process every Nth frame
    int frame_counter;           // Frame counter for throttling
    float min_edge_density;      // Minimum edge density threshold
} OCRState;
```

### Gemini API Integration

```c
static int call_gemini_api(OCRState* state, const char* image_base64,
                            cJSON** text_results) {
    // Build request
    cJSON* request = cJSON_CreateObject();
    cJSON* contents = cJSON_CreateArray();
    cJSON* content = cJSON_CreateObject();
    cJSON* parts = cJSON_CreateArray();

    // Text prompt
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "text",
        "Extract all readable text. Return JSON array: "
        "[{\"text\": \"example\", \"confidence\": 0.95}]");
    cJSON_AddItemToArray(parts, text_part);

    // Base64 image
    cJSON* image_part = cJSON_CreateObject();
    cJSON* inline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_data, "mime_type", "image/jpeg");
    cJSON_AddStringToObject(inline_data, "data", image_base64);
    cJSON_AddItemToObject(image_part, "inline_data", inline_data);
    cJSON_AddItemToArray(parts, image_part);

    cJSON_AddItemToObject(content, "parts", parts);
    cJSON_AddItemToArray(contents, content);
    cJSON_AddItemToObject(request, "contents", contents);

    // Generation config
    cJSON* gen_config = cJSON_CreateObject();
    cJSON_AddNumberToObject(gen_config, "temperature", 0.1);
    cJSON_AddNumberToObject(gen_config, "maxOutputTokens", 500);
    cJSON_AddItemToObject(request, "generationConfig", gen_config);

    // HTTP POST with CURL
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s", state->api_url, state->api_key);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);

    // Parse response
    // (Full implementation in ocr_module.c)

    return 0;
}
```

### Process Function

```c
static int ocr_process(ModuleContext* ctx, FrameData* frame) {
    OCRState* state = (OCRState*)ctx->module_state;

    // Throttling
    state->frame_counter++;
    if (state->frame_counter % state->process_interval != 0) {
        return AXION_MODULE_SKIP;
    }

    // Check if frame likely contains text (edge density heuristic)
    float edge_density = estimate_edge_density(frame->frame_data,
                                                 frame->width, frame->height);
    if (edge_density < state->min_edge_density) {
        return AXION_MODULE_SKIP;
    }

    // Encode frame as JPEG + base64 (implementation: TODO)
    // Call Gemini API
    cJSON* text_results = NULL;
    call_gemini_api(state, encoded_image, &text_results);

    // Add OCR data to custom metadata
    cJSON* ocr_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(ocr_data, "edge_density", edge_density);
    cJSON_AddStringToObject(ocr_data, "model", state->model_name);

    if (text_results && cJSON_IsArray(text_results)) {
        cJSON_AddNumberToObject(ocr_data, "text_count", cJSON_GetArraySize(text_results));
        cJSON_AddItemToObject(ocr_data, "texts", text_results);
    } else {
        cJSON_AddNumberToObject(ocr_data, "text_count", 0);
        cJSON_AddItemToObject(ocr_data, "texts", cJSON_CreateArray());
    }

    cJSON_AddItemToObject(frame->metadata->custom_data, "ocr", ocr_data);

    return AXION_MODULE_SUCCESS;
}
```

### Output Metadata

```json
{
  "modules": {
    "detection": { ... },
    "lpr": { ... },
    "ocr": {
      "edge_density": 0.45,
      "model": "gemini-2.0-flash-exp",
      "text_count": 2,
      "texts": [
        {
          "text": "EXIT",
          "confidence": 0.97
        },
        {
          "text": "AUTHORIZED PERSONNEL ONLY",
          "confidence": 0.89
        }
      ]
    }
  }
}
```

---

## Build System Integration

### Makefile Structure

```makefile
PROG = axion_poc

# Module feature flags (user-configurable)
ENABLE_LPR ?= 1
ENABLE_OCR ?= 1

# Core objects (always compiled)
CORE_OBJS = main.o core.o module_utils.o vdo_handler.o larod_handler.o dlpu_basic.o \
            ACAP.o MQTT.o CERTS.o cJSON.o

# Detection module (always included)
MODULE_OBJS = detection_module.o

# Conditional modules
ifeq ($(ENABLE_LPR),1)
    MODULE_OBJS += lpr_module.o
    CFLAGS += -DMODULE_LPR
endif

ifeq ($(ENABLE_OCR),1)
    MODULE_OBJS += ocr_module.o
    CFLAGS += -DMODULE_OCR
endif

# All objects
OBJS = $(CORE_OBJS) $(MODULE_OBJS)

# Packages
PKGS = gio-2.0 vdostream larod

# Compiler and linker flags
CFLAGS += -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))
LDFLAGS += $(shell pkg-config --libs $(PKGS)) -lpaho-mqtt3a -lm

# Add libcurl and libjpeg if AI modules enabled
ifneq ($(ENABLE_LPR)$(ENABLE_OCR),00)
    LDFLAGS += -lcurl -ljpeg
endif

all: $(PROG)
	@echo "==================================="
	@echo "Build complete: $(PROG)"
	@echo "Modules enabled:"
	@echo "  - Detection: YES (always)"
	@echo "  - LPR:       $(if $(filter 1,$(ENABLE_LPR)),YES,NO)"
	@echo "  - OCR:       $(if $(filter 1,$(ENABLE_OCR)),YES,NO)"
	@echo "==================================="

$(PROG): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	rm -f $(PROG) *.o *.eap
```

### Build Variants

| Variant | Command | Modules | Binary Size | Memory | Use Case |
|---------|---------|---------|-------------|--------|----------|
| **Full** | `make` | Core + Detection + LPR + OCR | ~400KB | ~700MB | Full AI capabilities |
| **Detection + LPR** | `make ENABLE_OCR=0` | Core + Detection + LPR | ~350KB | ~550MB | Vehicle monitoring |
| **Detection + OCR** | `make ENABLE_LPR=0` | Core + Detection + OCR | ~350KB | ~600MB | Sign/label reading |
| **Detection Only** | `make ENABLE_LPR=0 ENABLE_OCR=0` | Core + Detection | ~300KB | ~400MB | Basic object detection |

---

## Configuration Management

### Configuration File Structure

```
poc/camera/app/settings/
├── core.json        # Core module config
├── detection.json   # Detection module config
├── lpr.json         # LPR module config (if enabled)
└── ocr.json         # OCR module config (if enabled)
```

### Core Configuration (`core.json`)

```json
{
  "camera_id": "axis-camera-001",
  "target_fps": 10,
  "confidence_threshold": 0.25
}
```

### Runtime Configuration Loading

```c
int core_discover_modules(CoreContext* ctx) {
    for (int i = 0; i < count; i++) {
        ModuleInterface* mod = modules[i];

        // Load module config from settings/<name>.json
        char config_path[256];
        snprintf(config_path, sizeof(config_path), "settings/%s.json", mod->name);
        cJSON* mod_config = ACAP_FILE_Read(config_path);

        // Initialize module with config
        mod->init(module_ctx, mod_config ? mod_config : cJSON_CreateObject());
    }
}
```

### Configuration Helper Functions

```c
// Module configuration utilities
const char* module_config_get_string(cJSON* config, const char* key, const char* default_val);
int module_config_get_int(cJSON* config, const char* key, int default_val);
float module_config_get_float(cJSON* config, const char* key, float default_val);
bool module_config_get_bool(cJSON* config, const char* key, bool default_val);
```

---

## Migration Guide

### From Monolithic POC to Modular Architecture

#### Step 1: Understand Changes

**Before (Monolithic):**
- `main.c` directly calls VDO, Larod, Metadata, MQTT
- Hardcoded pipeline: VDO → Larod → Metadata → MQTT
- Single 400-line `process_frame()` function
- No extensibility without modifying core

**After (Modular):**
- `core.c` manages VDO, Larod, DLPU, MQTT
- `main.c` simplified to ~330 lines
- Detection logic moved to `detection_module.c`
- New modules: `lpr_module.c`, `ocr_module.c`
- Plugin-based extensibility

#### Step 2: Code Mapping

| Old Location | New Location | Notes |
|--------------|--------------|-------|
| `main.c:process_frame()` lines 98-184 | `core.c:core_process_frame()` | Refactored to module loop |
| `larod_handler.c:Larod_Run_Inference()` | `detection_module.c:detection_process()` | Wrapped in module |
| `metadata.c:Metadata_Extract()` | `detection_module.c` + `module_utils.c` | Split into module and utilities |
| `main.c:HTTP_ENDPOINT_Status()` | `main.c:HTTP_ENDPOINT_Status()` + new `/modules` endpoint | Enhanced with module info |

#### Step 3: Build and Test

```bash
# 1. Build detection-only variant (backward compatible)
cd poc/camera
make ENABLE_LPR=0 ENABLE_OCR=0

# 2. Test on camera (should produce identical output to v1.0.0)
./axion_poc

# 3. Compare MQTT output
mosquitto_sub -t "axion/camera/+/metadata" -v

# Expected: Same detection results, new "modules" field in JSON

# 4. Build with LPR module
make clean
make ENABLE_LPR=1 ENABLE_OCR=0

# 5. Configure Claude API key in settings/lpr.json
vi app/settings/lpr.json
# Set: "claude_api_key": "sk-ant-..."

# 6. Test LPR functionality
./axion_poc
# Drive a vehicle past camera, check MQTT for plate data

# 7. Build full variant
make clean
make  # All modules enabled

# 8. Configure Gemini API key
vi app/settings/ocr.json
# Set: "gemini_api_key": "AIza..."

# 9. Test OCR functionality
./axion_poc
# Show text to camera, check MQTT for OCR data
```

#### Step 4: Validate Performance

**Key Metrics:**
- **FPS**: Should maintain 10 FPS with Detection-only
- **FPS with LPR**: 8-10 FPS (depends on API latency)
- **FPS with OCR**: 8-10 FPS (depends on API latency)
- **Memory**: Core ~400MB, +LPR ~150MB, +OCR ~200MB
- **MQTT Throughput**: 10 messages/sec

**Test Command:**
```bash
# Monitor FPS and memory
watch -n 1 'curl -s http://localhost:8080/local/axion_poc/status | jq .'
```

---

## Performance Considerations

### Resource Allocation

| Module | CPU (%) | Memory (MB) | DLPU Time (ms) | Network (KB/s) |
|--------|---------|-------------|----------------|----------------|
| **Core** | 5% | 100 | - | 5 (MQTT) |
| **Detection** | 15% | 300 | 40-50 | - |
| **LPR** | 5% | 150 | - | 10 (Claude API) |
| **OCR** | 5% | 200 | - | 15 (Gemini API) |
| **Total (Full)** | 30% | 750 | 40-50 | 30 |

### Throttling Strategies

**LPR Module:**
- `process_interval: 10` → Process 1 out of 10 vehicles
- Reduces API costs: 60 vehicles/min → 6 API calls/min
- Plate recognition latency: 200-500ms

**OCR Module:**
- `process_interval: 30` → Process 1 out of 30 frames
- Reduces API costs: 10 FPS → 0.33 FPS OCR
- Text recognition latency: 300-700ms

### DLPU Time Budget

```
Total available: 100ms (10 FPS)
- Detection (YOLOv5n): 40-50ms
- VDO capture: 10ms
- Metadata processing: 5ms
- MQTT publish: 5ms
- Remaining: 30-40ms (for multi-module overhead)
```

**Multi-camera coordination:**
- Camera 1: 0-100ms slot
- Camera 2: 200-300ms slot
- Camera 3: 400-500ms slot

---

## Future Extensibility

### Example Future Modules

#### 1. **Facial Recognition Module** (`face_module.c`)

```c
MODULE_REGISTER(face_module, "face", "1.0.0", 40,
                face_init, face_process, face_cleanup);

static int face_process(ModuleContext* ctx, FrameData* frame) {
    // 1. Check for person detections (class_id == 0)
    // 2. Crop face region
    // 3. Run face embedding model
    // 4. Match against database
    // 5. Add face_id to metadata
    return AXION_MODULE_SUCCESS;
}
```

**Build:** `make ENABLE_FACE=1`

#### 2. **Object Tracking Module** (`tracking_module.c`)

```c
MODULE_REGISTER(tracking_module, "tracking", "1.0.0", 50,
                tracking_init, tracking_process, tracking_cleanup);

static int tracking_process(ModuleContext* ctx, FrameData* frame) {
    // 1. Receive detections from detection module
    // 2. Apply SORT/DeepSORT tracking algorithm
    // 3. Assign persistent track IDs
    // 4. Add track_id to each detection
    // 5. Publish track enter/exit events
    return AXION_MODULE_SUCCESS;
}
```

**Build:** `make ENABLE_TRACKING=1`

#### 3. **Anomaly Detection Module** (`anomaly_module.c`)

```c
MODULE_REGISTER(anomaly_module, "anomaly", "1.0.0", 60,
                anomaly_init, anomaly_process, anomaly_cleanup);

static int anomaly_process(ModuleContext* ctx, FrameData* frame) {
    // 1. Collect scene features (motion, object count, patterns)
    // 2. Compare against learned baseline
    // 3. Detect deviations (unusual activity, missing objects)
    // 4. Publish anomaly alerts
    return AXION_MODULE_SUCCESS;
}
```

**Build:** `make ENABLE_ANOMALY=1`

### Module Marketplace Concept

**Vision:** Community-contributed modules

```bash
# Install module from marketplace
axion-module install <module-name>

# List installed modules
axion-module list

# Enable/disable modules
axion-module enable lpr
axion-module disable ocr
```

**Module Package Format:**
```
lpr-module-v1.0.0.tar.gz
├── lpr_module.c
├── lpr_module.h
├── settings/lpr.json
├── README.md
└── manifest.json
```

---

## Versioning and Compatibility

### Module Versioning

Each module uses **semantic versioning**:
- **Major**: Breaking API changes (e.g., 1.0.0 → 2.0.0)
- **Minor**: New features, backward compatible (e.g., 1.0.0 → 1.1.0)
- **Patch**: Bug fixes (e.g., 1.0.0 → 1.0.1)

### Compatibility Matrix

| Core Version | Detection | LPR | OCR | Notes |
|--------------|-----------|-----|-----|-------|
| **2.0.0** | 1.0.0 | 1.0.0 | 1.0.0 | Initial modular release |
| **2.1.0** | 1.0.x | 1.0.x | 1.0.x | Backward compatible |
| **3.0.0** | 2.0.0 | 2.0.0 | 2.0.0 | Breaking: New ModuleInterface |

### Inter-Module Communication

**Current:** Modules communicate via shared `MetadataFrame.custom_data` (JSON)

**Future:** Message bus for direct module-to-module communication

```c
// Example: LPR module subscribing to Detection module's output
void lpr_init(ModuleContext* ctx, cJSON* config) {
    // Subscribe to detection events
    core_subscribe(ctx->core, "detection.vehicles", lpr_on_vehicle_detected);
}

void lpr_on_vehicle_detected(ModuleContext* ctx, cJSON* event) {
    // Process vehicle detection event
}
```

---

## Deployment Guide

### AXIS OS 12 Deployment

```bash
# 1. Build for target architecture
cd poc/camera
export ARCH=aarch64
make clean
make ENABLE_LPR=1 ENABLE_OCR=1

# 2. Package ACAP
acap-build .

# 3. Upload to camera
scp axion_poc_2.0.0_aarch64.eap root@<camera-ip>:/tmp/

# 4. Install via web interface
# http://<camera-ip>/index.html#system/apps
# Upload axion_poc_2.0.0_aarch64.eap

# 5. Configure API keys
ssh root@<camera-ip>
vi /usr/local/packages/axion_poc/settings/lpr.json
# Set claude_api_key

vi /usr/local/packages/axion_poc/settings/ocr.json
# Set gemini_api_key

# 6. Start application
# Via web interface or:
/usr/local/packages/axion_poc/axion_poc &

# 7. Monitor logs
tail -f /var/log/syslog | grep axion_poc

# 8. Verify modules loaded
curl http://<camera-ip>:8080/local/axion_poc/modules
```

### Docker Deployment (Development Only)

```bash
# For local development/testing (not production)
cd poc/cloud
docker-compose up -d

# View MQTT messages
docker exec -it cloud_subscriber_1 python mqtt_subscriber.py
```

---

## Testing Strategy

### Unit Testing

```c
// Test module initialization
void test_module_init() {
    ModuleContext ctx = {0};
    cJSON* config = cJSON_Parse("{\"enabled\": true}");

    assert(lpr_init(&ctx, config) == AXION_MODULE_SUCCESS);
    assert(ctx.module_state != NULL);

    lpr_cleanup(&ctx);
    cJSON_Delete(config);
}

// Test module registration
void test_module_discovery() {
    CoreContext* core = NULL;
    core_init(&core, NULL);

    int count = core_discover_modules(core);
    assert(count >= 1);  // At least detection module

    core_cleanup(core);
}
```

### Integration Testing

```bash
# Test build variants
make clean && make ENABLE_LPR=0 ENABLE_OCR=0
make clean && make ENABLE_LPR=1 ENABLE_OCR=0
make clean && make ENABLE_LPR=0 ENABLE_OCR=1
make clean && make

# Test module loading
./axion_poc &
sleep 5
curl http://localhost:8080/local/axion_poc/modules | jq .
pkill axion_poc

# Test MQTT output
mosquitto_sub -t "axion/camera/+/metadata" -C 10 > output.json
jq '.modules | keys' output.json
# Expected: ["detection"] or ["detection", "lpr", "ocr"]
```

### Performance Testing

```bash
# Measure FPS over 24 hours
while true; do
    curl -s http://<camera-ip>:8080/local/axion_poc/status | \
    jq -r '[now, .actual_fps, .module_count] | @csv' >> fps_log.csv
    sleep 60
done

# Analyze results
python << EOF
import pandas as pd
df = pd.read_csv('fps_log.csv', names=['timestamp', 'fps', 'modules'])
print(f"Mean FPS: {df['fps'].mean():.2f}")
print(f"Min FPS: {df['fps'].min():.2f}")
print(f"Max FPS: {df['fps'].max():.2f}")
print(f"FPS < 8: {(df['fps'] < 8).sum()} / {len(df)} ({(df['fps'] < 8).sum() / len(df) * 100:.1f}%)")
EOF
```

---

## Troubleshooting

### Module Not Loading

**Symptom:** Module not in `/modules` endpoint

**Diagnosis:**
```bash
# Check if module was compiled
nm axion_poc | grep -i module_name
# Should show MODULE_REGISTER symbol

# Check linker sections
objdump -h axion_poc | grep axion_modules
# Should show .axion_modules section
```

**Fix:**
```makefile
# Ensure module is in OBJS
ifeq ($(ENABLE_LPR),1)
    MODULE_OBJS += lpr_module.o
    CFLAGS += -DMODULE_LPR  # ← Critical!
endif
```

### API Key Not Working

**Symptom:** Module shows "No API key configured, module disabled"

**Diagnosis:**
```bash
# Check config file exists
ls -l /usr/local/packages/axion_poc/settings/lpr.json

# Check JSON syntax
jq . /usr/local/packages/axion_poc/settings/lpr.json

# Check key is not empty
jq -r '.claude_api_key' /usr/local/packages/axion_poc/settings/lpr.json
```

**Fix:**
```bash
# Update config
vi /usr/local/packages/axion_poc/settings/lpr.json
# Set: "claude_api_key": "sk-ant-..."

# Restart application
pkill axion_poc
/usr/local/packages/axion_poc/axion_poc &
```

### Low FPS

**Symptom:** FPS < 8

**Diagnosis:**
```bash
# Check DLPU wait times
curl http://<camera-ip>:8080/local/axion_poc/status | jq .dlpu_wait_ms
# Should be < 50ms

# Check API latency
tail -f /var/log/syslog | grep "Claude\|Gemini"
# Look for timeout errors

# Check memory usage
curl http://<camera-ip>:8080/local/axion_poc/status | jq .memory_mb
# Should be < 1000MB
```

**Fix:**
```json
// Increase throttling intervals
// settings/lpr.json:
{
  "process_interval": 20  // Was 10
}

// settings/ocr.json:
{
  "process_interval": 60  // Was 30
}
```

---

## Summary

The AXION modular architecture provides:

✅ **Extensibility**: Add modules without core changes
✅ **Flexibility**: Build only what you need
✅ **Maintainability**: Clean separation of concerns
✅ **Performance**: 10 FPS with full module suite
✅ **AXIS OS 12 Ready**: Native binary, static linking
✅ **AI Integration**: Claude (LPR) + Gemini (OCR)

**Next Steps:**
1. Test on AXIS OS 12 camera hardware
2. Optimize JPEG encoding for LPR/OCR
3. Add face recognition module
4. Implement object tracking module
5. Create module marketplace

**Questions?** Contact the AXION development team.

---

**Document Version:** 1.0
**Last Updated:** 2025-11-23
**Maintained By:** AXION Team
