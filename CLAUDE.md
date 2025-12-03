# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Axis I.S. Platform** - Edge-cloud AI camera surveillance combining on-camera object detection with cloud-based AI scene analysis.

**Architecture:** Camera Edge (ACAP/C) → MQTT → Cloud Service (Python/FastAPI) → AI Analysis (Claude/Gemini)

## Build Commands

### Camera-Side (ACAP Native Application)

```bash
# Build using Docker (recommended - handles SDK and cross-compilation)
cd poc/camera
./build.sh

# Creates: Axis_I.S._POC_-_Edge_AI_Surveillance_(Native)_2_0_0_aarch64.eap

# Deploy to camera
cd poc
./deploy.sh <camera-ip> <username> <password>

# Direct build (inside ACAP SDK container)
cd poc/camera/app
make clean && make
```

### Cloud Service

```bash
# Start all services (Mosquitto, PostgreSQL, Redis, FastAPI)
cd cloud-service
docker-compose up -d

# View logs
docker-compose logs -f cloud-service

# Local development (without Docker)
cd cloud-service
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
python main.py
```

## Architecture

### Zero-Copy Frame Pipeline (Camera)

All modules share the same VDO buffer - critical for performance:

```c
// core.c captures once, all modules process same buffer
VdoBuffer* buffer = Vdo_Get_Frame(vdo_ctx);
for (each module) {
    module->process(ctx, frame_data);  // Shared buffer - no copying
}
Vdo_Release_Frame(vdo_ctx, buffer);
```

### Edge-Cloud Data Flow

1. **Normal Operation (99% bandwidth reduction):**
   - Camera publishes metadata only (2-8KB @ 10 FPS)
   - Topic: `axis-is/camera/{camera_id}/metadata`

2. **Triggered Frame Request:**
   - Cloud triggers: motion > 0.7, vehicle detection, scene change
   - Cloud publishes to: `axis-is/camera/{camera_id}/frame_request`
   - Camera responds with JPEG frame (~100KB) to: `axis-is/camera/{camera_id}/frame`

3. **AI Analysis:**
   - Scene memory (Redis) stores last 30 frames per camera
   - AI agent analyzes with visual + metadata context
   - Executive summary stored in PostgreSQL

### Module Registration System

Modules self-register at compile time using the `MODULE_REGISTER` macro in [poc/camera/app/module.h](poc/camera/app/module.h):

```c
MODULE_REGISTER(my_module, "my_module", "1.0.0", 40,
                my_module_init, my_module_process, my_module_cleanup);
```

Priority determines execution order (lower = earlier): Detection (10), Custom modules (20-40+).

## Adding a Camera Module

1. Create `poc/camera/app/your_module.c` implementing:
   - `module_init()` - Initialize with config from `settings/{module}.json`
   - `module_process()` - Process each frame (access detections, metadata, raw pixels)
   - `module_cleanup()` - Release resources

2. Add configuration `poc/camera/app/settings/your_module.json`

3. Update `poc/camera/app/Makefile`:
   ```makefile
   MODULE_OBJS = detection_module.o frame_publisher.o your_module.o
   ```

4. Rebuild: `cd poc/camera && ./build.sh`

**Frame data access in modules:**
- `frame->vdo_buffer` - Zero-copy VDO buffer
- `frame->frame_data` - Raw YUV pixels (416x416)
- `frame->metadata->detections` - Array of Detection objects
- `frame->metadata->motion_score` - Motion score (0-1)
- `cJSON_AddItemToObject(frame->metadata->custom_data, "my_module", data)` - Add custom data

## Cloud Service AI Agents

The cloud service supports multiple AI providers via factory pattern in [cloud-service/ai_factory.py](cloud-service/ai_factory.py):

**Configuration (.env):**
```bash
AI_PROVIDER=claude  # or "gemini"
ANTHROPIC_API_KEY=sk-ant-...
CLAUDE_MODEL=claude-3-5-sonnet-20241022
# or
GEMINI_API_KEY=AIza-...
GEMINI_MODEL=gemini-3-pro
```

All agents implement `analyze_scene(camera_id, context)` → executive summary.

To modify AI analysis: Edit [cloud-service/claude_agent.py](cloud-service/claude_agent.py) or [cloud-service/gemini_agent.py](cloud-service/gemini_agent.py).

To adjust triggers: Edit `should_request_frame()` in [cloud-service/scene_memory.py](cloud-service/scene_memory.py).

## Testing Endpoints

### Camera HTTP API
```bash
curl http://<camera-ip>/local/axis_is_poc/status
curl http://<camera-ip>/local/axis_is_poc/metadata
```

### MQTT Testing
```bash
mosquitto_sub -h <mqtt-broker-ip> -t "axis-is/camera/#" -v
```

### Cloud Service API
```bash
curl http://localhost:8000/health
curl http://localhost:8000/stats
curl http://localhost:8000/cameras
curl http://localhost:8000/cameras/{camera_id}/analyses?limit=10
curl -X POST http://localhost:8000/cameras/{camera_id}/request-frame?reason=manual
```

## Key Configuration Files

**Camera:** `poc/camera/app/settings/`
- `core.json` - camera_id, fps, mqtt_broker
- `detection.json` - confidence threshold
- `frame_publisher.json` - JPEG quality, rate limiting
- `mqtt.json` - MQTT connection details

**Cloud:** `cloud-service/.env`
- AI provider and API keys
- Thresholds: MOTION_THRESHOLD, VEHICLE_CONFIDENCE_THRESHOLD
- Rate limiting: FRAME_REQUEST_COOLDOWN, MAX_CONCURRENT_ANALYSES

## Module Development Guidelines

**Return codes:**
- `AXIS_IS_MODULE_SUCCESS` (0) - Success
- `AXIS_IS_MODULE_ERROR` (-1) - Fatal error
- `AXIS_IS_MODULE_SKIP` (1) - Skip frame processing

**Constraints:**
- Module callbacks run in core's main thread
- Avoid blocking operations in `process()` (max 100ms)
- Always free allocated memory in `module_cleanup()`
- Each module needs a corresponding `settings/{module}.json`
