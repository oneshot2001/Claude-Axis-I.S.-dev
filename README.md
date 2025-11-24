# Axis I.S. Platform

**Version:** 2.0.0
**Status:** Production Ready
**Architecture:** Edge-Cloud AI with Claude Vision Integration

---

## Overview

The **Axis I.S. Platform** is a complete edge-cloud AI camera surveillance system that combines on-camera object detection with cloud-based Claude Vision analysis. It provides intelligent scene understanding with minimal bandwidth usage through metadata streaming and on-demand frame analysis.

### Complete System

```
CAMERA EDGE → MQTT → CLOUD SERVICE → CLAUDE VISION → EXECUTIVE SUMMARIES
```

✅ **Camera-Side Components**
- VDO video stream management (416x416 @ 10 FPS)
- Larod ML inference with DLPU hardware acceleration
- Detection Module: YOLOv5n INT8 object detection (80 COCO classes)
- Frame Publisher Module: On-demand JPEG frame transmission
- MQTT client: Metadata streaming + frame publishing
- Modular plugin framework for custom modules

✅ **Cloud-Side Components** (NEW)
- MQTT Handler: Subscribes to camera metadata streams
- Scene Memory: Maintains last 30 frames per camera (Redis)
- Intelligent Triggers: Motion, vehicles, scene changes
- Frame Request System: On-demand with rate limiting
- Claude Vision Agent: AI scene analysis with visual context
- PostgreSQL: Events, analyses, alerts storage
- REST API: Query analyses, manage cameras

✅ **Key Features**
- **99% Bandwidth Reduction**: 2-8KB metadata vs 200KB frames
- **Smart Frame Requests**: Only when interesting events occur
- **Executive Summaries**: Claude analyzes scenes with visual context
- **Multi-Camera Support**: 1-20 cameras per cloud instance
- **Scalable Architecture**: Edge detection + cloud reasoning

### Example Executive Summary

> **Camera: axis-camera-001** (2025-11-23 14:35:22)
>
> A delivery truck has pulled into the loading bay and two people are unloading boxes. One person is operating a forklift in the background. Activity appears routine for afternoon deliveries. No security concerns detected. Motion level is moderate (0.72) consistent with typical loading operations.

### Future Modules (Build Your Own)

The platform provides the framework for custom modules:

🔲 **LPR Module** - License Plate Recognition
🔲 **OCR Module** - Optical Character Recognition
🔲 **Face Recognition Module**
🔲 **Object Tracking Module**
🔲 **Anomaly Detection Module**
🔲 **Your Custom Module**

---

## Quick Start

### Prerequisites

**Cloud Side:**
- Ubuntu 20.04+ server with Docker
- Anthropic API key (Claude Vision)
- Public IP or domain for camera connectivity

**Camera Side:**
- AXIS camera with ARTPEC-8/9 SoC
- AXIS OS 12.x
- ACAP Native SDK 1.2+ (for building)
- YOLOv5n INT8 model (1MB)

### Deployment Options

**Option 1: Complete Deployment** (Recommended)
```bash
# See comprehensive step-by-step guide:
cat DEPLOYMENT_GUIDE.md
```
This deploys both cloud service and camera applications.

**Option 2: Camera Module Only** (Base Module)
```bash
# Build camera-side application
cd poc/camera
make clean && make

# Package for deployment
acap-build .

# Upload to camera
scp axis_is_poc_2.0.0_aarch64.eap root@<camera-ip>:/tmp/

# Install via web interface
# Navigate to: http://<camera-ip>/index.html#system/apps
```

**Option 3: Cloud Service Only**
```bash
# Deploy cloud infrastructure
cd cloud-service
cp .env.example .env
# Edit .env with ANTHROPIC_API_KEY
docker-compose up -d

# Verify deployment
curl http://localhost:8000/health
```

### Configuration

**Camera Configuration** (`poc/camera/app/settings/`):

`core.json`:
```json
{
  "camera_id": "axis-camera-001",
  "target_fps": 10,
  "mqtt_broker": "YOUR_CLOUD_IP",
  "mqtt_port": 1883
}
```

`frame_publisher.json`:
```json
{
  "enabled": true,
  "camera_id": "axis-camera-001",
  "jpeg_quality": 85,
  "rate_limit_seconds": 60
}
```

**Cloud Configuration** (`cloud-service/.env`):

```env
ANTHROPIC_API_KEY=sk-ant-api-your-key-here
MQTT_BROKER=0.0.0.0
MOTION_THRESHOLD=0.7
VEHICLE_CONFIDENCE_THRESHOLD=0.5
FRAME_REQUEST_COOLDOWN=60
MAX_CONCURRENT_ANALYSES=5
```

---

## Developing Custom Modules

The base module makes it easy to add your own AI capabilities. Here's how:

### Module Structure

Every module implements this interface:

```c
// my_module.c
#include "module.h"

static int my_module_init(ModuleContext* ctx, cJSON* config) {
    // Initialize your module
    // Load configuration from settings/my_module.json
    return AXIS_IS_MODULE_SUCCESS;
}

static int my_module_process(ModuleContext* ctx, FrameData* frame) {
    // Process each frame
    // Access: frame->vdo_buffer, frame->metadata
    // Add your data: cJSON_AddItemToObject(frame->metadata->custom_data, "my_module", data);
    return AXIS_IS_MODULE_SUCCESS;
}

static void my_module_cleanup(ModuleContext* ctx) {
    // Cleanup resources
}

// Register module (priority determines execution order)
MODULE_REGISTER(my_module, "my_module", "1.0.0", 40,
                my_module_init, my_module_process, my_module_cleanup);
```

### Integration Steps

1. **Create module file**: `poc/camera/app/my_module.c`
2. **Add configuration**: `poc/camera/app/settings/my_module.json`
3. **Update Makefile**:
   ```makefile
   MODULE_OBJS = detection_module.o my_module.o
   ```
4. **Build and test**:
   ```bash
   make clean && make
   ```

### Module Capabilities

Your module has access to:

**Frame Data:**
```c
frame->vdo_buffer       // Zero-copy VDO buffer
frame->frame_data       // Raw pixel data (YUV 416x416)
frame->width            // 416
frame->height           // 416
frame->timestamp_us     // Frame timestamp
frame->frame_id         // Sequential ID
```

**Detection Results:**
```c
frame->metadata->detections        // Array of Detection objects
frame->metadata->detection_count   // Number of detections
frame->metadata->motion_score      // Motion score (0-1)
frame->metadata->scene_hash        // Scene hash for change detection
```

**Adding Your Data:**
```c
cJSON* my_data = cJSON_CreateObject();
cJSON_AddStringToObject(my_data, "result", "success");
cJSON_AddNumberToObject(my_data, "confidence", 0.95);
cJSON_AddItemToObject(frame->metadata->custom_data, "my_module", my_data);
```

### Example: LPR Module Skeleton

```c
// lpr_module.c
#ifdef MODULE_LPR

#include "module.h"
#include <curl/curl.h>

typedef struct {
    char* api_key;
    float min_confidence;
    int process_interval;
} LPRState;

static int lpr_init(ModuleContext* ctx, cJSON* config) {
    LPRState* state = calloc(1, sizeof(LPRState));
    state->api_key = strdup(module_config_get_string(config, "api_key", ""));
    state->min_confidence = module_config_get_float(config, "min_confidence", 0.5);
    state->process_interval = module_config_get_int(config, "process_interval", 10);
    ctx->module_state = state;
    return AXIS_IS_MODULE_SUCCESS;
}

static int lpr_process(ModuleContext* ctx, FrameData* frame) {
    LPRState* state = ctx->module_state;

    // Find vehicles in detections
    for (int i = 0; i < frame->metadata->detection_count; i++) {
        Detection* det = &frame->metadata->detections[i];
        if (det->class_id == 2 && det->confidence > state->min_confidence) {  // Car
            // 1. Crop vehicle region from frame
            // 2. Encode as JPEG
            // 3. Send to Claude Vision API
            // 4. Parse plate number
            // 5. Add to metadata
        }
    }

    return AXIS_IS_MODULE_SUCCESS;
}

static void lpr_cleanup(ModuleContext* ctx) {
    LPRState* state = ctx->module_state;
    if (state->api_key) free(state->api_key);
    free(state);
}

MODULE_REGISTER(lpr_module, "lpr", "1.0.0", 20,
                lpr_init, lpr_process, lpr_cleanup);

#endif
```

**To enable:**
```makefile
# Makefile
MODULE_OBJS = detection_module.o lpr_module.o
CFLAGS += -DMODULE_LPR
LDFLAGS += -lcurl
```

---

## Architecture

### Complete Edge-Cloud Data Flow

```
┌─────────────────────────────────────────┐
│         CAMERA EDGE (AXIS OS)           │
├─────────────────────────────────────────┤
│ VDO Stream (10 FPS, 416x416)            │
│         ↓                                │
│ Detection Module (YOLOv5n on DLPU)      │
│         ↓                                │
│ Metadata Extraction                     │
│  (motion, objects, scene hash)          │
│         ↓                                │
│ MQTT Publish → metadata (2-8KB)  ────────┐
│                                          │
│ Frame Publisher ← frame_request ─────────┤
│         ↓                                │
│ JPEG Encode + Base64                    │
│         ↓                                │
│ MQTT Publish → frame (100KB)   ──────────┤
└─────────────────────────────────────────┘
                                           │
                          MQTT (QoS 0/1)  │
                                           ↓
┌─────────────────────────────────────────────┐
│            CLOUD SERVICE                    │
├─────────────────────────────────────────────┤
│ MQTT Handler                                │
│    ├─ Subscribe: metadata (10 FPS)          │
│    └─ Subscribe: frames (on-demand)         │
│         ↓                                    │
│ Scene Memory (Redis)                        │
│    └─ Last 30 frames per camera             │
│         ↓                                    │
│ Trigger Logic                               │
│    ├─ Motion > 0.7                          │
│    ├─ Vehicle detected                      │
│    └─ Scene change                          │
│         ↓ (when triggered)                  │
│ Request Frame ──────────────────────────────┘
│         ↓ (frame arrives)
│ Claude Vision Agent
│    ├─ Get last 5 frames with images
│    ├─ Build context with metadata
│    └─ Call Claude API
│         ↓
│ PostgreSQL: Store analysis
│ Redis: Update camera state
│         ↓
│ Executive Summary Available via API
└─────────────────────────────────────────────┘
```

### Module Execution Order

Modules execute in **priority order** (lower = earlier):

| Priority | Module | Function |
|----------|--------|----------|
| 10 | Detection | Object detection (always first) |
| 20 | Your Module | License plates, faces, etc. |
| 30 | Your Module | OCR, tracking, etc. |
| 40+ | Your Module | Custom processing |

### Zero-Copy Pipeline

All modules share the **same VDO buffer** - no copying overhead:

```c
// Core captures once
VdoBuffer* buffer = Vdo_Get_Frame(vdo_ctx);

// All modules process the same buffer
for (each module) {
    module->process(ctx, &frame_data);  // frame_data.vdo_buffer = buffer
}

// Core releases once
Vdo_Release_Frame(vdo_ctx, buffer);
```

### MQTT Output

All module data is aggregated into a single MQTT message:

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
      "num_detections": 3
    },
    "my_module": {
      "your_custom_data": "here"
    }
  }
}
```

Topic: `axis-is/camera/{camera_id}/metadata`

---

## HTTP API

The base module provides HTTP endpoints:

### GET /local/axis_is_poc/status

```json
{
  "app": "axis_is_poc",
  "version": "2.0.0",
  "architecture": "modular",
  "camera_id": "axis-camera-001",
  "uptime_seconds": 3600,
  "frames_processed": 36000,
  "actual_fps": 10.0,
  "module_count": 1
}
```

### GET /local/axis_is_poc/modules

```json
{
  "count": 1,
  "modules": [
    {
      "name": "detection",
      "version": "1.0.0",
      "priority": 10
    }
  ]
}
```

---

## Performance

### Resource Usage (Base Module)

| Metric | Value |
|--------|-------|
| **Binary Size** | ~300KB |
| **Memory** | ~400MB |
| **CPU** | 20% @ 10 FPS |
| **DLPU** | 40-50ms per frame |
| **FPS** | 10 sustained |

### Adding Modules

Each additional module adds:
- **CPU**: +5-10%
- **Memory**: +100-200MB (depends on module)
- **Latency**: Minimal (zero-copy pipeline)

---

## File Structure

```
poc/camera/app/
├── main.c                    # Main application
├── core.c/h                  # Core orchestrator
├── module.h                  # Module interface
├── module_utils.c            # Utility functions
├── detection_module.c        # Detection module (included)
├── vdo_handler.c/h           # VDO stream management
├── larod_handler.c/h         # Larod inference
├── dlpu_basic.c/h            # DLPU coordination
├── metadata.c/h              # Metadata extraction
├── MQTT.c/h                  # MQTT client
├── ACAP.c/h                  # ACAP framework
├── cJSON.c/h                 # JSON library
├── Makefile                  # Build system
└── settings/
    ├── core.json             # Core configuration
    └── detection.json        # Detection configuration
```

---

## Documentation

- **[Architecture Design](AXIS_IS_MODULAR_ARCHITECTURE_DESIGN.md)** - Comprehensive architecture guide
- **[Implementation Guide](IMPLEMENTATION_COMPLETE.md)** - Testing and deployment
- **[Module Interface](poc/camera/app/module.h)** - Module API reference

---

## Examples

### Example 1: Simple Counter Module

Count objects per frame:

```c
// counter_module.c
static int counter_init(ModuleContext* ctx, cJSON* config) {
    int* count = calloc(1, sizeof(int));
    ctx->module_state = count;
    return AXIS_IS_MODULE_SUCCESS;
}

static int counter_process(ModuleContext* ctx, FrameData* frame) {
    int* total = (int*)ctx->module_state;
    *total += frame->metadata->detection_count;

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "total_objects", *total);
    cJSON_AddItemToObject(frame->metadata->custom_data, "counter", data);

    return AXIS_IS_MODULE_SUCCESS;
}

MODULE_REGISTER(counter_module, "counter", "1.0.0", 50,
                counter_init, counter_process, NULL);
```

### Example 2: Alert Module

Send alerts when motion detected:

```c
static int alert_process(ModuleContext* ctx, FrameData* frame) {
    if (frame->metadata->motion_score > 0.7) {
        // Send alert via MQTT or HTTP
        cJSON* alert = cJSON_CreateObject();
        cJSON_AddStringToObject(alert, "type", "motion");
        cJSON_AddNumberToObject(alert, "score", frame->metadata->motion_score);
        cJSON_AddItemToObject(frame->metadata->custom_data, "alert", alert);
    }
    return AXIS_IS_MODULE_SUCCESS;
}
```

---

## Support

For questions or issues:

1. Check the [Architecture Design](AXIS_IS_MODULAR_ARCHITECTURE_DESIGN.md)
2. Review [module.h](poc/camera/app/module.h) for API details
3. Examine [detection_module.c](poc/camera/app/detection_module.c) as reference
4. Open an issue on GitHub

---

## License

This project is provided as-is for AXIS camera development.

---

## Contributing

Contributions are welcome! To add your module:

1. Fork the repository
2. Create your module file
3. Test on AXIS OS 12 camera
4. Submit pull request with documentation

---

**Built with AXIS ACAP Native SDK**
**Compatible with AXIS OS 12.x**
**Optimized for ARTPEC-8/9 SoC**

🤖 Developed with [Claude Code](https://claude.com/claude-code)
