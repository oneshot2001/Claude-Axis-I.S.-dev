# Axis I.S. Platform API Specifications

**Version:** 1.0.0
**Date:** November 23, 2025
**Status:** Production-Ready Design

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Camera ACAP HTTP API (FastCGI)](#2-camera-acap-http-api-fastcgi)
3. [MQTT Message Schemas](#3-mqtt-message-schemas)
4. [Cloud Services REST API](#4-cloud-services-rest-api)
5. [Claude Integration Internal API](#5-claude-integration-internal-api)
6. [WebSocket API](#6-websocket-api)
7. [Error Handling Standards](#7-error-handling-standards)
8. [Versioning Strategy](#8-versioning-strategy)
9. [Security & Authentication](#9-security--authentication)
10. [Performance Guidelines](#10-performance-guidelines)

---

## 1. Architecture Overview

### System Components

```
┌─────────────────┐         ┌──────────────────┐         ┌─────────────────┐
│  Axis Camera    │  MQTT   │  Cloud Platform  │  HTTP   │  Claude API     │
│  (ACAP)         │◄───────►│  (Coordinator)   │◄───────►│  (Analysis)     │
│                 │         │                  │         │                 │
│  - VDO Capture  │         │  - MQTT Broker   │         │  - Vision       │
│  - Larod ML     │         │  - Redis State   │         │  - Context      │
│  - Metadata     │         │  - PostgreSQL    │         │  - Reasoning    │
│  - FastCGI API  │         │  - REST API      │         │                 │
└─────────────────┘         └──────────────────┘         └─────────────────┘
         ▲                           ▲
         │                           │
         │ HTTP (Config/Control)     │ WebSocket (Real-time)
         │                           │
         ▼                           ▼
    ┌─────────────────────────────────────┐
    │      Dashboard / Client Apps        │
    └─────────────────────────────────────┘
```

### Communication Patterns

| Pattern | Protocol | Use Case | Frequency |
|---------|----------|----------|-----------|
| Metadata Streaming | MQTT | Continuous metadata from camera | 5-10 FPS |
| Event Notification | MQTT | Significant events/alerts | As needed |
| Configuration | HTTP/FastCGI | Camera settings updates | On demand |
| Frame Requests | MQTT | Request specific frames | As needed |
| Dashboard Updates | WebSocket | Real-time UI updates | Continuous |
| Claude Analysis | HTTP | AI vision analysis | On demand |

---

## 2. Camera ACAP HTTP API (FastCGI)

### 2.1 Overview

The Camera ACAP exposes HTTP endpoints via FastCGI integration with the Axis device's Apache web server. All endpoints are authenticated using the device's user pool (same as VAPIX).

**Base Path:** `/local/axis-is/`

**Authentication:** HTTP Basic Auth (Axis device credentials)

**Access Level:** `viewer` (read-only operations) or `operator` (configuration changes)

### 2.2 Endpoints

#### 2.2.1 GET /app - Application Information

Retrieve application manifest, version, and capabilities.

**URL:** `/local/axis-is/app`

**Method:** `GET`

**Auth Required:** Yes (viewer)

**Rate Limit:** 10 requests/minute

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "name": "Axis I.S.",
    "version": "1.0.0",
    "camera_id": "axis-camera-001",
    "capabilities": {
      "dlpu": true,
      "yolov5": true,
      "mqtt": true,
      "max_fps": 10
    },
    "uptime_seconds": 86400,
    "manifest": {
      "appId": "com.axis.axion",
      "vendor": "Axis Communications",
      "architecture": "aarch64"
    },
    "system": {
      "chip": "artpec9",
      "firmware": "11.11.75",
      "dlpu_available": true
    }
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Status Codes:**

- `200 OK` - Success
- `401 Unauthorized` - Invalid credentials
- `503 Service Unavailable` - ACAP not running

**Example Request:**

```bash
curl -u user:pass http://192.168.1.100/local/axis-is/app
```

---

#### 2.2.2 GET /settings - Current Configuration

Retrieve current Axis I.S. configuration including MQTT, ML model, and streaming settings.

**URL:** `/local/axis-is/settings`

**Method:** `GET`

**Auth Required:** Yes (viewer)

**Rate Limit:** 20 requests/minute

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "mqtt": {
      "enabled": true,
      "broker": "192.168.1.50",
      "port": 1883,
      "client_id": "axion-camera-001",
      "use_tls": true,
      "qos": 1,
      "keepalive": 60,
      "topics": {
        "metadata": "axis-is/camera/axis-camera-001/metadata",
        "events": "axis-is/camera/axis-camera-001/event",
        "status": "axis-is/camera/axis-camera-001/status"
      }
    },
    "model": {
      "name": "yolov5n_int8.tflite",
      "input_size": [640, 640],
      "confidence_threshold": 0.25,
      "iou_threshold": 0.45,
      "classes": 80,
      "dlpu_enabled": true
    },
    "streaming": {
      "fps": 10,
      "resolution": [1920, 1080],
      "format": "yuv420",
      "metadata_fps": 10
    },
    "scene": {
      "hash_enabled": true,
      "motion_detection": true,
      "change_threshold": 0.15
    },
    "bandwidth": {
      "max_mbps": 5,
      "adaptive": true,
      "frame_quality": 85
    }
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Status Codes:**

- `200 OK` - Success
- `401 Unauthorized` - Invalid credentials
- `503 Service Unavailable` - ACAP not running

---

#### 2.2.3 POST /settings - Update Configuration

Update Axis I.S. configuration. Partial updates supported (only send changed fields).

**URL:** `/local/axis-is/settings`

**Method:** `POST`

**Auth Required:** Yes (operator)

**Rate Limit:** 5 requests/minute

**Content-Type:** `application/json`

**Request Body:**

```json
{
  "mqtt": {
    "broker": "192.168.1.60",
    "port": 8883,
    "use_tls": true
  },
  "model": {
    "confidence_threshold": 0.30,
    "iou_threshold": 0.50
  },
  "streaming": {
    "fps": 5,
    "metadata_fps": 5
  }
}
```

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "updated_fields": [
      "mqtt.broker",
      "mqtt.port",
      "model.confidence_threshold",
      "model.iou_threshold",
      "streaming.fps"
    ],
    "restart_required": true,
    "message": "Configuration updated. Restart ACAP to apply changes."
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Validation Rules:**

- `streaming.fps`: 1-30 (integer)
- `model.confidence_threshold`: 0.0-1.0 (float)
- `model.iou_threshold`: 0.0-1.0 (float)
- `mqtt.port`: 1-65535 (integer)
- `bandwidth.max_mbps`: 0.1-100 (float)

**Status Codes:**

- `200 OK` - Success
- `400 Bad Request` - Invalid parameters
- `401 Unauthorized` - Invalid credentials
- `403 Forbidden` - Insufficient permissions (need operator)
- `422 Unprocessable Entity` - Validation failed

**Error Response Example:**

```json
{
  "status": "error",
  "error": {
    "code": "VALIDATION_ERROR",
    "message": "Invalid configuration parameters",
    "details": [
      {
        "field": "streaming.fps",
        "value": 50,
        "constraint": "Must be between 1 and 30",
        "expected_type": "integer"
      }
    ]
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

---

#### 2.2.4 GET /status - Health and Metrics

Retrieve real-time health status and performance metrics.

**URL:** `/local/axis-is/status`

**Method:** `GET`

**Auth Required:** Yes (viewer)

**Rate Limit:** 30 requests/minute

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "health": {
      "status": "healthy",
      "uptime_seconds": 86400,
      "last_restart": "2025-11-22T10:30:00Z"
    },
    "mqtt": {
      "connected": true,
      "last_publish": "2025-11-23T10:30:00Z",
      "messages_sent": 86400,
      "messages_failed": 3,
      "retry_queue_size": 0
    },
    "inference": {
      "dlpu_available": true,
      "model_loaded": true,
      "avg_inference_ms": 45,
      "inferences_completed": 86400,
      "inferences_failed": 0,
      "current_fps": 10.2
    },
    "streaming": {
      "frames_captured": 86400,
      "frames_dropped": 5,
      "current_fps": 10.0,
      "vdo_buffer_usage": 0.45
    },
    "dlpu_scheduler": {
      "assigned_slot_ms": 100,
      "avg_wait_ms": 15,
      "max_wait_ms": 250,
      "slot_conflicts": 2
    },
    "resources": {
      "cpu_percent": 25,
      "memory_mb": 512,
      "memory_percent": 35,
      "disk_usage_mb": 150
    },
    "errors": {
      "last_error": null,
      "error_count_24h": 0
    }
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Status Codes:**

- `200 OK` - Success
- `401 Unauthorized` - Invalid credentials
- `503 Service Unavailable` - ACAP not running

**Health Status Values:**

- `healthy` - All systems operational
- `degraded` - Some non-critical issues (e.g., MQTT reconnecting)
- `unhealthy` - Critical issues (e.g., DLPU unavailable, model load failed)

---

#### 2.2.5 GET /metadata - Latest Frame Metadata

Retrieve the most recent frame metadata without full frame data.

**URL:** `/local/axis-is/metadata`

**Method:** `GET`

**Auth Required:** Yes (viewer)

**Rate Limit:** 60 requests/minute

**Query Parameters:**

- `include_detections` (optional): boolean, default `true`
- `include_motion` (optional): boolean, default `false`

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "frame_id": 86400,
    "timestamp": "2025-11-23T10:30:00.123Z",
    "camera_id": "axis-camera-001",
    "scene_hash": "a1b2c3d4e5f6",
    "scene_changed": false,
    "inference": {
      "inference_time_ms": 42,
      "preprocessing_time_ms": 18,
      "total_time_ms": 60,
      "model": "yolov5n_int8"
    },
    "detections": [
      {
        "class": "person",
        "class_id": 0,
        "confidence": 0.87,
        "bbox": {
          "x": 0.45,
          "y": 0.32,
          "width": 0.12,
          "height": 0.35
        }
      },
      {
        "class": "car",
        "class_id": 2,
        "confidence": 0.92,
        "bbox": {
          "x": 0.75,
          "y": 0.60,
          "width": 0.18,
          "height": 0.22
        }
      }
    ],
    "motion": {
      "score": 0.23,
      "regions": [
        {"x": 0.4, "y": 0.3, "w": 0.2, "h": 0.4}
      ]
    },
    "stream": {
      "resolution": [1920, 1080],
      "fps": 10.1,
      "format": "yuv420"
    }
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Status Codes:**

- `200 OK` - Success
- `401 Unauthorized` - Invalid credentials
- `404 Not Found` - No metadata available yet
- `503 Service Unavailable` - ACAP not running

---

#### 2.2.6 GET /frame/{timestamp} - Request Specific Frame

Request a full frame by timestamp. Returns JPEG-encoded frame.

**URL:** `/local/axis-is/frame/{timestamp}`

**Method:** `GET`

**Auth Required:** Yes (viewer)

**Rate Limit:** 10 requests/minute (expensive operation)

**Path Parameters:**

- `timestamp`: ISO 8601 timestamp or frame_id (integer)

**Query Parameters:**

- `quality` (optional): integer 1-100, default 85
- `include_metadata` (optional): boolean, default `true`
- `include_overlays` (optional): boolean, default `false` (draw bounding boxes)
- `max_age_seconds` (optional): integer, default 300 (only return if frame is cached)

**Response Format (with metadata):**

```json
{
  "status": "success",
  "data": {
    "frame_id": 86400,
    "timestamp": "2025-11-23T10:30:00.123Z",
    "camera_id": "axis-camera-001",
    "image": {
      "format": "jpeg",
      "quality": 85,
      "size_bytes": 245600,
      "resolution": [1920, 1080],
      "data_base64": "/9j/4AAQSkZJRgABAQAA..."
    },
    "metadata": {
      "scene_hash": "a1b2c3d4e5f6",
      "detections": [ /* same as /metadata */ ]
    }
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Alternative: Binary Response**

If `Accept: image/jpeg` header is sent, returns raw JPEG binary:

```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Length: 245600
X-Frame-Id: 86400
X-Timestamp: 2025-11-23T10:30:00.123Z
X-Camera-Id: axis-camera-001

[JPEG binary data]
```

**Status Codes:**

- `200 OK` - Success
- `401 Unauthorized` - Invalid credentials
- `404 Not Found` - Frame not available (expired from cache)
- `410 Gone` - Frame too old, not in cache
- `429 Too Many Requests` - Rate limit exceeded
- `503 Service Unavailable` - ACAP not running

**Cache Behavior:**

- Frames are cached for 5 minutes (300 seconds) by default
- Configurable via `max_age_seconds` parameter
- LRU eviction when memory limit reached

---

#### 2.2.7 POST /control - Control Commands

Send control commands to the ACAP (pause, resume, quality adjustment, etc.).

**URL:** `/local/axis-is/control`

**Method:** `POST`

**Auth Required:** Yes (operator)

**Rate Limit:** 20 requests/minute

**Content-Type:** `application/json`

**Request Body:**

```json
{
  "command": "pause_streaming",
  "parameters": {}
}
```

**Supported Commands:**

| Command | Parameters | Description |
|---------|------------|-------------|
| `pause_streaming` | none | Pause metadata streaming (keep MQTT connected) |
| `resume_streaming` | none | Resume metadata streaming |
| `restart` | none | Restart ACAP application |
| `reload_model` | `{"model_path": "/path/to/model.tflite"}` | Reload ML model |
| `adjust_fps` | `{"fps": 5}` | Dynamically adjust streaming FPS |
| `adjust_quality` | `{"quality": 80}` | Adjust frame quality (1-100) |
| `clear_cache` | none | Clear frame cache |
| `test_mqtt` | none | Send test message to MQTT broker |
| `force_frame_request` | `{"timestamp": "2025-11-23T10:30:00Z"}` | Force capture specific frame |

**Response Format:**

```json
{
  "status": "success",
  "data": {
    "command": "pause_streaming",
    "executed": true,
    "message": "Streaming paused successfully"
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Error Response Example:**

```json
{
  "status": "error",
  "error": {
    "code": "INVALID_COMMAND",
    "message": "Unknown command: invalid_cmd",
    "supported_commands": [
      "pause_streaming",
      "resume_streaming",
      "restart",
      "reload_model",
      "adjust_fps",
      "adjust_quality",
      "clear_cache",
      "test_mqtt",
      "force_frame_request"
    ]
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

**Status Codes:**

- `200 OK` - Command executed successfully
- `202 Accepted` - Command accepted, executing asynchronously
- `400 Bad Request` - Invalid command or parameters
- `401 Unauthorized` - Invalid credentials
- `403 Forbidden` - Insufficient permissions
- `503 Service Unavailable` - ACAP not running

---

### 2.3 FastCGI Manifest Configuration

**manifest.json excerpt:**

```json
{
  "acapPackageConf": {
    "configuration": {
      "httpConfig": [
        {
          "access": "viewer",
          "name": "app",
          "type": "fastCgi"
        },
        {
          "access": "viewer",
          "name": "settings",
          "type": "fastCgi"
        },
        {
          "access": "viewer",
          "name": "status",
          "type": "fastCgi"
        },
        {
          "access": "viewer",
          "name": "metadata",
          "type": "fastCgi"
        },
        {
          "access": "viewer",
          "name": "frame/*",
          "type": "fastCgi"
        },
        {
          "access": "operator",
          "name": "control",
          "type": "fastCgi"
        }
      ]
    }
  }
}
```

---

## 3. MQTT Message Schemas

### 3.1 Overview

MQTT is used for high-frequency, asynchronous communication between cameras and the cloud platform.

**Broker:** Eclipse Mosquitto 2.0+

**Protocol:** MQTT 3.1.1 or 5.0

**Transport:** TCP (1883) or TLS (8883)

**Persistence:** QoS 1 messages persisted on broker

### 3.2 Topic Structure

**Pattern:** `axis-is/{message_type}/{camera_id}/{sub_type}`

**Examples:**

- `axis-is/camera/axis-camera-001/metadata`
- `axis-is/camera/axis-camera-001/event`
- `axis-is/cloud/axis-camera-001/config`

### 3.3 Camera → Cloud Messages

#### 3.3.1 Metadata Stream

**Topic:** `axis-is/camera/{camera_id}/metadata`

**QoS:** 0 (fire-and-forget, high frequency)

**Retained:** No

**Frequency:** 5-10 FPS (configurable)

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "metadata",
  "camera_id": "axis-camera-001",
  "seq": 86400,
  "timestamp": "2025-11-23T10:30:00.123Z",
  "frame_id": 86400,
  "scene": {
    "hash": "a1b2c3d4e5f6",
    "changed": false,
    "confidence": 0.95
  },
  "inference": {
    "model": "yolov5n_int8",
    "time_ms": 42,
    "preprocessing_ms": 18,
    "fps": 10.1
  },
  "detections": [
    {
      "class": "person",
      "class_id": 0,
      "confidence": 0.87,
      "bbox": [0.45, 0.32, 0.12, 0.35]
    },
    {
      "class": "car",
      "class_id": 2,
      "confidence": 0.92,
      "bbox": [0.75, 0.60, 0.18, 0.22]
    }
  ],
  "motion": {
    "score": 0.23,
    "detected": true
  },
  "system": {
    "cpu": 25,
    "memory_mb": 512,
    "dlpu_wait_ms": 15
  }
}
```

**Field Descriptions:**

- `version`: Schema version for backwards compatibility
- `msg_type`: Always "metadata"
- `seq`: Monotonic sequence number (detects dropped messages)
- `timestamp`: ISO 8601 with milliseconds
- `scene.hash`: 12-char hex hash of scene signature
- `scene.changed`: Boolean indicating significant scene change
- `detections`: Array of detected objects (empty if none)
- `motion.score`: 0.0-1.0 motion intensity

**Sequence Numbering:**

- Starts at 0 on camera boot
- Increments by 1 per message
- Wraps at 2^32 - 1
- Cloud detects gaps and logs warnings

---

#### 3.3.2 Event Notification

**Topic:** `axis-is/camera/{camera_id}/event`

**QoS:** 1 (at-least-once delivery)

**Retained:** No

**Frequency:** As needed (triggered by significant events)

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "event",
  "camera_id": "axis-camera-001",
  "event_id": "evt-12345",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "event_type": "scene_change",
  "severity": "info",
  "description": "Significant scene change detected",
  "data": {
    "old_hash": "a1b2c3d4e5f6",
    "new_hash": "b2c3d4e5f6a1",
    "confidence": 0.88,
    "frame_id": 86400,
    "detections_before": 2,
    "detections_after": 5
  },
  "metadata": {
    "frame_available": true,
    "frame_expires_at": "2025-11-23T10:35:00Z"
  }
}
```

**Event Types:**

| Event Type | Severity | Description |
|------------|----------|-------------|
| `scene_change` | info | Significant scene change detected |
| `new_object` | info | New object class detected |
| `object_disappeared` | info | Previously tracked object disappeared |
| `motion_detected` | info | Motion above threshold |
| `inference_slow` | warning | Inference time exceeded threshold |
| `dlpu_contention` | warning | DLPU wait time high |
| `mqtt_reconnect` | warning | MQTT connection restored |
| `model_reload` | info | ML model reloaded |

**Severity Levels:**

- `debug`: Verbose debugging info
- `info`: Informational events
- `warning`: Non-critical issues
- `error`: Errors that don't stop operation
- `critical`: Critical errors requiring intervention

---

#### 3.3.3 Alert Notification

**Topic:** `axis-is/camera/{camera_id}/alert`

**QoS:** 2 (exactly-once delivery)

**Retained:** No

**Frequency:** As needed (critical alerts only)

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "alert",
  "camera_id": "axis-camera-001",
  "alert_id": "alert-67890",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "alert_type": "dlpu_unavailable",
  "severity": "critical",
  "title": "DLPU Hardware Unavailable",
  "description": "DLPU hardware acceleration unavailable. Falling back to CPU inference.",
  "data": {
    "error_code": "LAROD_ERROR_DEVICE_BUSY",
    "impact": "Inference time increased from 45ms to 250ms",
    "action_required": "Check DLPU scheduler conflicts"
  },
  "requires_ack": true,
  "ack_by": "2025-11-23T11:00:00Z"
}
```

**Alert Types:**

| Alert Type | Severity | Requires Ack |
|------------|----------|--------------|
| `dlpu_unavailable` | critical | yes |
| `mqtt_disconnected` | critical | yes |
| `model_load_failed` | critical | yes |
| `disk_full` | critical | yes |
| `memory_critical` | error | no |
| `frame_drops_high` | error | no |
| `inference_timeout` | error | no |

---

#### 3.3.4 Status Update

**Topic:** `axis-is/camera/{camera_id}/status`

**QoS:** 1 (at-least-once delivery)

**Retained:** Yes (last status always available)

**Frequency:** Every 60 seconds + on significant changes

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "status",
  "camera_id": "axis-camera-001",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "state": "running",
  "health": "healthy",
  "uptime_seconds": 86400,
  "mqtt": {
    "connected": true,
    "messages_sent": 86400,
    "messages_failed": 3,
    "last_error": null
  },
  "inference": {
    "model_loaded": true,
    "model_name": "yolov5n_int8",
    "avg_inference_ms": 45,
    "current_fps": 10.1,
    "dlpu_available": true
  },
  "resources": {
    "cpu_percent": 25,
    "memory_mb": 512,
    "disk_mb": 150
  },
  "config_version": "1.0.0",
  "firmware_version": "11.11.75"
}
```

**State Values:**

- `starting` - ACAP starting up
- `running` - Normal operation
- `paused` - Streaming paused by user
- `error` - Error state, trying to recover
- `stopping` - Shutting down

**Health Values:**

- `healthy` - All systems operational
- `degraded` - Some non-critical issues
- `unhealthy` - Critical issues

---

#### 3.3.5 Heartbeat

**Topic:** `axis-is/camera/{camera_id}/heartbeat`

**QoS:** 0 (fire-and-forget)

**Retained:** No

**Frequency:** Every 30 seconds

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "heartbeat",
  "camera_id": "axis-camera-001",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "seq": 2880,
  "uptime_seconds": 86400
}
```

**Purpose:**

- Detect camera offline/online status
- Minimal payload for keepalive
- Cloud marks camera offline if no heartbeat for 90 seconds

---

### 3.4 Cloud → Camera Messages

#### 3.4.1 Configuration Update

**Topic:** `axis-is/cloud/{camera_id}/config`

**QoS:** 1 (at-least-once delivery)

**Retained:** No

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "config_update",
  "config_id": "cfg-12345",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "updates": {
    "streaming": {
      "fps": 5,
      "metadata_fps": 5
    },
    "model": {
      "confidence_threshold": 0.30
    }
  },
  "apply_mode": "immediate"
}
```

**Apply Modes:**

- `immediate` - Apply without restart (dynamic changes only)
- `on_restart` - Apply on next restart
- `scheduled` - Apply at specified time (future enhancement)

**Acknowledgment:**

Camera sends event message confirming config applied:

```json
{
  "event_type": "config_applied",
  "data": {
    "config_id": "cfg-12345",
    "applied_at": "2025-11-23T10:30:01.000Z",
    "restart_required": false
  }
}
```

---

#### 3.4.2 DLPU Schedule Assignment

**Topic:** `axis-is/cloud/{camera_id}/dlpu_schedule`

**QoS:** 1 (at-least-once delivery)

**Retained:** Yes (last schedule always available)

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "dlpu_schedule",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "schedule_id": "sched-67890",
  "camera_id": "axis-camera-001",
  "slot_assignment": {
    "slot_number": 1,
    "slot_duration_ms": 100,
    "slot_offset_ms": 100,
    "total_slots": 5
  },
  "effective_at": "2025-11-23T10:30:05.000Z",
  "expires_at": "2025-11-23T11:30:00.000Z"
}
```

**Slot Assignment Logic:**

For 5 cameras @ 5 FPS with 50ms inference:

```
Camera 1: Offset 0ms   (inference at 0ms, 200ms, 400ms, ...)
Camera 2: Offset 100ms (inference at 100ms, 300ms, 500ms, ...)
Camera 3: Offset 200ms (inference at 200ms, 400ms, 600ms, ...)
Camera 4: Offset 300ms (inference at 300ms, 500ms, 700ms, ...)
Camera 5: Offset 400ms (inference at 400ms, 600ms, 800ms, ...)
```

**Acknowledgment:**

Camera sends status update with current slot assignment.

---

#### 3.4.3 Frame Request

**Topic:** `axis-is/cloud/{camera_id}/frame_request`

**QoS:** 1 (at-least-once delivery)

**Retained:** No

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "frame_request",
  "request_id": "req-11111",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "frame_selector": {
    "type": "timestamp",
    "value": "2025-11-23T10:29:55.123Z"
  },
  "options": {
    "quality": 85,
    "include_overlays": false,
    "format": "jpeg"
  },
  "delivery": {
    "method": "http_upload",
    "endpoint": "https://cloud.axion.com/api/v1/frames/upload",
    "auth_token": "Bearer eyJhbGciOiJIUzI1NiIs..."
  }
}
```

**Frame Selector Types:**

- `timestamp` - ISO 8601 timestamp
- `frame_id` - Integer frame ID
- `latest` - Most recent frame
- `offset_seconds` - Relative offset (e.g., -5 for 5 seconds ago)

**Delivery Methods:**

- `http_upload` - Camera uploads to specified endpoint
- `mqtt_chunk` - Send via MQTT in chunks (for small frames)
- `local_cache` - Keep in camera cache, retrieve via HTTP

**Response:**

Camera sends event message with frame delivery status:

```json
{
  "event_type": "frame_delivered",
  "data": {
    "request_id": "req-11111",
    "status": "success",
    "frame_id": 86350,
    "size_bytes": 245600,
    "delivery_method": "http_upload",
    "upload_url": "https://cloud.axion.com/api/v1/frames/86350"
  }
}
```

---

#### 3.4.4 Control Command

**Topic:** `axis-is/cloud/{camera_id}/command`

**QoS:** 1 (at-least-once delivery)

**Retained:** No

**Payload Schema:**

```json
{
  "version": "1.0",
  "msg_type": "command",
  "command_id": "cmd-22222",
  "timestamp": "2025-11-23T10:30:00.123Z",
  "command": "pause_streaming",
  "parameters": {},
  "timeout_seconds": 10
}
```

**Supported Commands:**

Same as HTTP `/control` endpoint (see section 2.2.7)

**Acknowledgment:**

Camera sends event message confirming command execution:

```json
{
  "event_type": "command_executed",
  "data": {
    "command_id": "cmd-22222",
    "command": "pause_streaming",
    "status": "success",
    "executed_at": "2025-11-23T10:30:00.500Z"
  }
}
```

---

### 3.5 MQTT Implementation Details

#### 3.5.1 Connection Management

**Camera-Side (Paho MQTT C):**

```c
// Connection options
MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
conn_opts.keepAliveInterval = 60;
conn_opts.cleansession = 1;
conn_opts.automaticReconnect = 1;
conn_opts.minRetryInterval = 5;
conn_opts.maxRetryInterval = 60;
conn_opts.connectTimeout = 30;

// Last Will and Testament (LWT)
MQTTAsync_willOptions will_opts = MQTTAsync_willOptions_initializer;
will_opts.topicName = "axis-is/camera/axis-camera-001/status";
will_opts.message = "{\"state\":\"offline\",\"timestamp\":\"2025-11-23T10:30:00Z\"}";
will_opts.qos = 1;
will_opts.retained = 1;
conn_opts.will = &will_opts;
```

**Cloud-Side (Python/asyncio-mqtt):**

```python
import asyncio_mqtt as aiomqtt

async def mqtt_loop():
    async with aiomqtt.Client("192.168.1.50") as client:
        await client.subscribe("axis-is/camera/+/metadata")
        await client.subscribe("axis-is/camera/+/event")

        async for message in client.messages:
            await handle_message(message.topic, message.payload)
```

#### 3.5.2 Message Retry Logic

**Camera-Side:**

```c
int publish_with_retry(const char* topic, const char* payload, int qos) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (MQTT_Publish(topic, payload, qos, 0)) {
            return 1;  // Success
        }
        sleep(1 << attempt);  // Exponential backoff: 1s, 2s, 4s
    }

    // Failed - add to persistent queue
    queue_message(topic, payload, qos);
    return 0;
}
```

**Queue Behavior:**

- Max queue size: 1000 messages
- QoS 0 messages dropped when queue full
- QoS 1/2 messages persisted to disk
- Queue processed when connection restored

#### 3.5.3 Sequence Number Validation

**Cloud-Side:**

```python
class SequenceValidator:
    def __init__(self):
        self.last_seq = {}

    def validate(self, camera_id, seq):
        if camera_id not in self.last_seq:
            self.last_seq[camera_id] = seq
            return True

        expected = (self.last_seq[camera_id] + 1) % (2**32)
        if seq != expected:
            gap = (seq - expected) % (2**32)
            logger.warning(f"Sequence gap for {camera_id}: expected {expected}, got {seq}, gap={gap}")
            # Log dropped messages for monitoring
            metrics.increment('mqtt.messages.dropped', gap)

        self.last_seq[camera_id] = seq
        return True
```

---

## 4. Cloud Services REST API

### 4.1 Overview

The Cloud Services REST API provides programmatic access to camera management, analysis queries, alerts, and system monitoring.

**Base URL:** `https://api.axion.com/v1`

**Authentication:** JWT Bearer Token or API Key

**Rate Limiting:** 1000 requests/hour per API key

**Response Format:** JSON

### 4.2 Authentication

#### 4.2.1 API Key Authentication

**Header:**

```http
Authorization: Bearer axion_sk_live_abc123def456...
```

**API Key Formats:**

- Production: `axion_sk_live_*`
- Testing: `axion_sk_test_*`

#### 4.2.2 JWT Authentication

**Login Endpoint:** `POST /auth/login`

**Request:**

```json
{
  "email": "user@example.com",
  "password": "secret123"
}
```

**Response:**

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "refresh_token": "eyJhbGciOiJIUzI1NiIs...",
  "expires_in": 3600,
  "token_type": "Bearer"
}
```

**Usage:**

```http
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
```

---

### 4.3 Camera Management

#### 4.3.1 List Cameras

**Endpoint:** `GET /cameras`

**Query Parameters:**

- `status` (optional): Filter by status (online, offline, error)
- `limit` (optional): Results per page (default 20, max 100)
- `offset` (optional): Pagination offset (default 0)
- `sort` (optional): Sort field (name, created_at, last_seen)
- `order` (optional): Sort order (asc, desc)

**Response:**

```json
{
  "data": [
    {
      "id": "cam_abc123",
      "camera_id": "axis-camera-001",
      "name": "Front Entrance",
      "status": "online",
      "health": "healthy",
      "model": "AXIS P1378",
      "firmware": "11.11.75",
      "location": {
        "site": "Building A",
        "floor": 1,
        "zone": "Lobby"
      },
      "capabilities": {
        "dlpu": true,
        "resolution_max": [1920, 1080],
        "fps_max": 30
      },
      "last_seen": "2025-11-23T10:30:00Z",
      "created_at": "2025-11-01T00:00:00Z",
      "updated_at": "2025-11-23T10:30:00Z"
    }
  ],
  "pagination": {
    "total": 45,
    "limit": 20,
    "offset": 0,
    "has_more": true
  }
}
```

---

#### 4.3.2 Get Camera Details

**Endpoint:** `GET /cameras/{camera_id}`

**Response:**

```json
{
  "data": {
    "id": "cam_abc123",
    "camera_id": "axis-camera-001",
    "name": "Front Entrance",
    "status": "online",
    "health": "healthy",
    "model": "AXIS P1378",
    "firmware": "11.11.75",
    "ip_address": "192.168.1.100",
    "mac_address": "00:40:8c:12:34:56",
    "location": {
      "site": "Building A",
      "floor": 1,
      "zone": "Lobby",
      "coordinates": {
        "lat": 37.7749,
        "lon": -122.4194
      }
    },
    "configuration": {
      "streaming": {
        "fps": 10,
        "resolution": [1920, 1080]
      },
      "model": {
        "name": "yolov5n_int8",
        "confidence_threshold": 0.25
      }
    },
    "statistics": {
      "uptime_seconds": 86400,
      "frames_processed": 864000,
      "detections_total": 15234,
      "alerts_total": 12
    },
    "last_seen": "2025-11-23T10:30:00Z",
    "created_at": "2025-11-01T00:00:00Z",
    "updated_at": "2025-11-23T10:30:00Z"
  }
}
```

---

#### 4.3.3 Register Camera

**Endpoint:** `POST /cameras`

**Request:**

```json
{
  "camera_id": "axis-camera-005",
  "name": "Parking Lot Exit",
  "model": "AXIS P1378",
  "ip_address": "192.168.1.105",
  "location": {
    "site": "Building A",
    "floor": 0,
    "zone": "Parking"
  },
  "configuration": {
    "streaming": {
      "fps": 5,
      "resolution": [1920, 1080]
    }
  }
}
```

**Response:**

```json
{
  "data": {
    "id": "cam_xyz789",
    "camera_id": "axis-camera-005",
    "name": "Parking Lot Exit",
    "status": "pending",
    "api_credentials": {
      "username": "axion_cam_005",
      "password": "generated_secret_123",
      "mqtt_topic_prefix": "axis-is/camera/axis-camera-005"
    },
    "setup_url": "https://192.168.1.105/index.html#apps",
    "created_at": "2025-11-23T10:30:00Z"
  }
}
```

---

#### 4.3.4 Update Camera

**Endpoint:** `PATCH /cameras/{camera_id}`

**Request:**

```json
{
  "name": "Updated Camera Name",
  "location": {
    "zone": "Updated Zone"
  },
  "configuration": {
    "streaming": {
      "fps": 8
    }
  }
}
```

**Response:**

```json
{
  "data": {
    "id": "cam_abc123",
    "camera_id": "axis-camera-001",
    "name": "Updated Camera Name",
    "updated_at": "2025-11-23T10:31:00Z"
  }
}
```

---

#### 4.3.5 Delete Camera

**Endpoint:** `DELETE /cameras/{camera_id}`

**Response:**

```json
{
  "data": {
    "id": "cam_abc123",
    "deleted": true,
    "deleted_at": "2025-11-23T10:32:00Z"
  }
}
```

---

### 4.4 Analysis Queries

#### 4.4.1 Query Recent Analyses

**Endpoint:** `GET /analyses`

**Query Parameters:**

- `camera_id` (optional): Filter by camera
- `start_time` (optional): ISO 8601 timestamp
- `end_time` (optional): ISO 8601 timestamp
- `event_type` (optional): Filter by event type
- `limit` (optional): Results per page (default 20, max 100)
- `offset` (optional): Pagination offset

**Response:**

```json
{
  "data": [
    {
      "id": "analysis_12345",
      "camera_id": "axis-camera-001",
      "timestamp": "2025-11-23T10:30:00.123Z",
      "frame_id": 86400,
      "analysis_type": "claude_vision",
      "results": {
        "summary": "Two people entering building, one carrying briefcase",
        "confidence": 0.92,
        "entities": [
          {
            "type": "person",
            "count": 2,
            "attributes": ["adult", "business_attire"]
          },
          {
            "type": "object",
            "name": "briefcase",
            "confidence": 0.88
          }
        ],
        "scene_description": "Indoor lobby area, well-lit, two individuals entering through automatic doors",
        "risk_assessment": "low",
        "suggested_action": null
      },
      "processing_time_ms": 850,
      "created_at": "2025-11-23T10:30:01Z"
    }
  ],
  "pagination": {
    "total": 1234,
    "limit": 20,
    "offset": 0,
    "has_more": true
  }
}
```

---

#### 4.4.2 Get Analysis Details

**Endpoint:** `GET /analyses/{analysis_id}`

**Response:**

```json
{
  "data": {
    "id": "analysis_12345",
    "camera_id": "axis-camera-001",
    "timestamp": "2025-11-23T10:30:00.123Z",
    "frame_id": 86400,
    "analysis_type": "claude_vision",
    "input": {
      "frame_url": "https://storage.axion.com/frames/86400.jpg",
      "metadata": {
        "detections": [
          {"class": "person", "confidence": 0.87}
        ]
      },
      "context": {
        "previous_scene": "empty_lobby",
        "time_of_day": "morning"
      }
    },
    "results": {
      "summary": "Two people entering building, one carrying briefcase",
      "confidence": 0.92,
      "entities": [/* ... */],
      "scene_description": "Indoor lobby area, well-lit, two individuals entering through automatic doors",
      "risk_assessment": "low",
      "suggested_action": null,
      "raw_response": "Full Claude API response..."
    },
    "processing_time_ms": 850,
    "tokens_used": 1250,
    "created_at": "2025-11-23T10:30:01Z"
  }
}
```

---

#### 4.4.3 Request New Analysis

**Endpoint:** `POST /analyses`

**Request:**

```json
{
  "camera_id": "axis-camera-001",
  "frame_selector": {
    "type": "timestamp",
    "value": "2025-11-23T10:30:00.123Z"
  },
  "analysis_type": "claude_vision",
  "options": {
    "include_context": true,
    "context_frames": 5,
    "prompt_template": "security_alert"
  }
}
```

**Response:**

```json
{
  "data": {
    "id": "analysis_12346",
    "status": "processing",
    "estimated_completion": "2025-11-23T10:30:05Z",
    "created_at": "2025-11-23T10:30:00Z"
  }
}
```

**Status Values:**

- `queued` - Waiting for processing
- `processing` - Analysis in progress
- `completed` - Analysis finished
- `failed` - Analysis failed

---

### 4.5 Alert Management

#### 4.5.1 List Alerts

**Endpoint:** `GET /alerts`

**Query Parameters:**

- `camera_id` (optional): Filter by camera
- `severity` (optional): Filter by severity (info, warning, error, critical)
- `status` (optional): Filter by status (open, acknowledged, resolved)
- `start_time` (optional): ISO 8601 timestamp
- `end_time` (optional): ISO 8601 timestamp
- `limit` (optional): Results per page (default 20, max 100)
- `offset` (optional): Pagination offset

**Response:**

```json
{
  "data": [
    {
      "id": "alert_67890",
      "alert_id": "alert-67890",
      "camera_id": "axis-camera-001",
      "timestamp": "2025-11-23T10:30:00.123Z",
      "alert_type": "dlpu_unavailable",
      "severity": "critical",
      "status": "open",
      "title": "DLPU Hardware Unavailable",
      "description": "DLPU hardware acceleration unavailable. Falling back to CPU inference.",
      "data": {
        "error_code": "LAROD_ERROR_DEVICE_BUSY",
        "impact": "Inference time increased from 45ms to 250ms"
      },
      "requires_ack": true,
      "ack_by": "2025-11-23T11:00:00Z",
      "acknowledged_at": null,
      "acknowledged_by": null,
      "resolved_at": null,
      "created_at": "2025-11-23T10:30:00Z"
    }
  ],
  "pagination": {
    "total": 23,
    "limit": 20,
    "offset": 0,
    "has_more": true
  }
}
```

---

#### 4.5.2 Acknowledge Alert

**Endpoint:** `POST /alerts/{alert_id}/acknowledge`

**Request:**

```json
{
  "note": "Investigating DLPU scheduler conflicts"
}
```

**Response:**

```json
{
  "data": {
    "id": "alert_67890",
    "status": "acknowledged",
    "acknowledged_at": "2025-11-23T10:35:00Z",
    "acknowledged_by": "user@example.com"
  }
}
```

---

#### 4.5.3 Resolve Alert

**Endpoint:** `POST /alerts/{alert_id}/resolve`

**Request:**

```json
{
  "resolution": "DLPU scheduler adjusted, conflicts resolved",
  "action_taken": "Updated DLPU time slot assignments"
}
```

**Response:**

```json
{
  "data": {
    "id": "alert_67890",
    "status": "resolved",
    "resolved_at": "2025-11-23T10:45:00Z",
    "resolved_by": "user@example.com"
  }
}
```

---

### 4.6 System Monitoring

#### 4.6.1 Get System Metrics

**Endpoint:** `GET /metrics`

**Query Parameters:**

- `metric` (optional): Specific metric name
- `start_time` (optional): ISO 8601 timestamp
- `end_time` (optional): ISO 8601 timestamp
- `interval` (optional): Aggregation interval (1m, 5m, 1h, 1d)

**Response:**

```json
{
  "data": {
    "timestamp": "2025-11-23T10:30:00Z",
    "cameras": {
      "total": 45,
      "online": 43,
      "offline": 2,
      "healthy": 41,
      "degraded": 2
    },
    "mqtt": {
      "messages_per_second": 450,
      "messages_dropped": 3,
      "active_subscriptions": 215
    },
    "inference": {
      "avg_inference_ms": 48,
      "p95_inference_ms": 85,
      "p99_inference_ms": 120,
      "total_inferences_24h": 3888000
    },
    "claude_api": {
      "requests_per_hour": 120,
      "avg_response_ms": 850,
      "tokens_used_24h": 1500000,
      "errors_24h": 2
    },
    "storage": {
      "frames_stored": 12500,
      "storage_used_gb": 45.2,
      "storage_limit_gb": 500
    },
    "alerts": {
      "open": 5,
      "acknowledged": 8,
      "resolved_24h": 23
    }
  }
}
```

---

#### 4.6.2 Get System Health

**Endpoint:** `GET /health`

**Response:**

```json
{
  "status": "healthy",
  "timestamp": "2025-11-23T10:30:00Z",
  "components": {
    "mqtt_broker": {
      "status": "healthy",
      "uptime_seconds": 864000,
      "last_check": "2025-11-23T10:30:00Z"
    },
    "redis": {
      "status": "healthy",
      "memory_used_mb": 256,
      "last_check": "2025-11-23T10:30:00Z"
    },
    "postgresql": {
      "status": "healthy",
      "connections": 15,
      "last_check": "2025-11-23T10:30:00Z"
    },
    "claude_api": {
      "status": "healthy",
      "latency_ms": 820,
      "last_check": "2025-11-23T10:30:00Z"
    },
    "storage": {
      "status": "healthy",
      "usage_percent": 9,
      "last_check": "2025-11-23T10:30:00Z"
    }
  }
}
```

**Overall Status Values:**

- `healthy` - All components operational
- `degraded` - Some non-critical issues
- `unhealthy` - Critical component failure

---

### 4.7 Configuration Management

#### 4.7.1 Get System Configuration

**Endpoint:** `GET /config`

**Response:**

```json
{
  "data": {
    "system": {
      "name": "Axis I.S. Production",
      "environment": "production",
      "version": "1.0.0"
    },
    "mqtt": {
      "broker": "192.168.1.50",
      "port": 8883,
      "use_tls": true
    },
    "claude_api": {
      "model": "claude-sonnet-4.5",
      "max_tokens": 2000,
      "temperature": 0.7
    },
    "storage": {
      "retention_days": 30,
      "frame_cache_ttl_seconds": 300
    },
    "alerting": {
      "email_enabled": true,
      "webhook_url": "https://hooks.example.com/axion"
    }
  }
}
```

---

#### 4.7.2 Update System Configuration

**Endpoint:** `PATCH /config`

**Request:**

```json
{
  "claude_api": {
    "max_tokens": 2500
  },
  "storage": {
    "retention_days": 45
  }
}
```

**Response:**

```json
{
  "data": {
    "updated_fields": [
      "claude_api.max_tokens",
      "storage.retention_days"
    ],
    "updated_at": "2025-11-23T10:30:00Z"
  }
}
```

---

### 4.8 OpenAPI Specification

**Full OpenAPI 3.0 spec available at:** `/openapi.json`

**Interactive Documentation (Swagger UI):** `https://api.axion.com/docs`

**Redoc Documentation:** `https://api.axion.com/redoc`

---

## 5. Claude Integration Internal API

### 5.1 Overview

Internal API for Claude Agent integration (not exposed externally).

**Communication:** Internal HTTP or function calls

**Purpose:** Frame analysis, context retrieval, pattern detection

### 5.2 Interfaces

#### 5.2.1 Ingest Metadata

**Function:** `ingest_metadata(metadata: dict) -> None`

**Input:**

```python
{
    "camera_id": "axis-camera-001",
    "frame_id": 86400,
    "timestamp": "2025-11-23T10:30:00.123Z",
    "scene_hash": "a1b2c3d4e5f6",
    "detections": [
        {"class": "person", "confidence": 0.87}
    ]
}
```

**Purpose:** Store metadata in time-series database for context retrieval

---

#### 5.2.2 Request Frame Analysis

**Function:** `analyze_frame(frame_request: dict) -> dict`

**Input:**

```python
{
    "camera_id": "axis-camera-001",
    "frame_id": 86400,
    "frame_data": "base64_encoded_jpeg",
    "metadata": {
        "detections": [/* ... */],
        "scene_hash": "a1b2c3d4e5f6"
    },
    "context": {
        "previous_frames": 5,
        "scene_history": [/* ... */]
    },
    "prompt_template": "security_alert"
}
```

**Output:**

```python
{
    "analysis_id": "analysis_12345",
    "summary": "Two people entering building, one carrying briefcase",
    "confidence": 0.92,
    "entities": [/* ... */],
    "suggested_action": None,
    "processing_time_ms": 850,
    "tokens_used": 1250
}
```

---

#### 5.2.3 Retrieve Scene Context

**Function:** `get_scene_context(camera_id: str, timespan: int) -> dict`

**Input:**

- `camera_id`: Camera identifier
- `timespan`: Seconds of history to retrieve

**Output:**

```python
{
    "camera_id": "axis-camera-001",
    "timespan_seconds": 300,
    "frames_analyzed": 3,
    "scene_hashes": [
        {"hash": "a1b2c3d4e5f6", "first_seen": "2025-11-23T10:25:00Z", "duration_s": 300}
    ],
    "dominant_objects": [
        {"class": "person", "count": 2, "frequency": 0.8}
    ],
    "activity_level": "moderate"
}
```

---

#### 5.2.4 Store Analysis Result

**Function:** `store_analysis(analysis: dict) -> str`

**Input:**

```python
{
    "camera_id": "axis-camera-001",
    "frame_id": 86400,
    "timestamp": "2025-11-23T10:30:00.123Z",
    "analysis_type": "claude_vision",
    "results": {/* ... */},
    "processing_time_ms": 850
}
```

**Output:**

```python
"analysis_12345"  # Analysis ID
```

---

## 6. WebSocket API

### 6.1 Overview

Real-time WebSocket API for live dashboard updates.

**Endpoint:** `wss://api.axion.com/v1/ws`

**Protocol:** WebSocket (RFC 6455)

**Authentication:** JWT token in connection URL

**Connection URL:** `wss://api.axion.com/v1/ws?token=eyJhbGciOiJIUzI1NiIs...`

### 6.2 Connection Flow

1. Client connects with JWT token
2. Server validates token and sends connection confirmation
3. Client subscribes to channels
4. Server sends real-time updates
5. Client can send commands
6. Either side can close connection

### 6.3 Message Format

**All messages are JSON:**

```json
{
  "type": "message_type",
  "channel": "channel_name",
  "data": {/* ... */},
  "timestamp": "2025-11-23T10:30:00.123Z"
}
```

### 6.4 Client → Server Messages

#### 6.4.1 Subscribe to Channel

```json
{
  "type": "subscribe",
  "channel": "camera.axis-camera-001.metadata",
  "options": {
    "fps": 5
  }
}
```

**Response:**

```json
{
  "type": "subscribed",
  "channel": "camera.axis-camera-001.metadata",
  "subscription_id": "sub_12345"
}
```

#### 6.4.2 Unsubscribe from Channel

```json
{
  "type": "unsubscribe",
  "subscription_id": "sub_12345"
}
```

**Response:**

```json
{
  "type": "unsubscribed",
  "subscription_id": "sub_12345"
}
```

### 6.5 Server → Client Messages

#### 6.5.1 Metadata Update

```json
{
  "type": "metadata",
  "channel": "camera.axis-camera-001.metadata",
  "data": {
    "frame_id": 86400,
    "timestamp": "2025-11-23T10:30:00.123Z",
    "detections": [/* ... */]
  }
}
```

#### 6.5.2 Alert Notification

```json
{
  "type": "alert",
  "channel": "alerts",
  "data": {
    "alert_id": "alert-67890",
    "severity": "critical",
    "title": "DLPU Hardware Unavailable",
    "camera_id": "axis-camera-001"
  }
}
```

#### 6.5.3 System Status Update

```json
{
  "type": "system_status",
  "channel": "system",
  "data": {
    "cameras_online": 43,
    "alerts_open": 5,
    "mqtt_messages_per_second": 450
  }
}
```

### 6.6 Available Channels

| Channel Pattern | Description | Update Frequency |
|----------------|-------------|------------------|
| `camera.{id}.metadata` | Live metadata from specific camera | 5-10 FPS |
| `camera.{id}.status` | Camera status changes | On change |
| `alerts` | New alerts across all cameras | On event |
| `system` | System-wide metrics | 1 Hz |
| `analyses` | New Claude analyses | On completion |

---

## 7. Error Handling Standards

### 7.1 HTTP Status Codes

#### Success Codes (2xx)

- `200 OK` - Request succeeded
- `201 Created` - Resource created
- `202 Accepted` - Request accepted, processing asynchronously
- `204 No Content` - Success, no response body

#### Client Error Codes (4xx)

- `400 Bad Request` - Invalid request syntax
- `401 Unauthorized` - Missing or invalid authentication
- `403 Forbidden` - Authenticated but insufficient permissions
- `404 Not Found` - Resource not found
- `409 Conflict` - Request conflicts with current state
- `422 Unprocessable Entity` - Validation failed
- `429 Too Many Requests` - Rate limit exceeded

#### Server Error Codes (5xx)

- `500 Internal Server Error` - Unexpected server error
- `502 Bad Gateway` - Upstream service error
- `503 Service Unavailable` - Service temporarily unavailable
- `504 Gateway Timeout` - Upstream service timeout

### 7.2 Error Response Format

**Standard Error Response:**

```json
{
  "status": "error",
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable error message",
    "details": [
      {
        "field": "field_name",
        "issue": "Specific issue description"
      }
    ],
    "request_id": "req_12345",
    "documentation_url": "https://docs.axion.com/errors/ERROR_CODE"
  },
  "timestamp": "2025-11-23T10:30:00Z"
}
```

### 7.3 Error Codes

#### Camera ACAP Errors (ACAP_*)

- `ACAP_NOT_RUNNING` - ACAP application not running
- `ACAP_DLPU_UNAVAILABLE` - DLPU hardware unavailable
- `ACAP_MODEL_LOAD_FAILED` - ML model failed to load
- `ACAP_VDO_ERROR` - Video capture error
- `ACAP_MQTT_DISCONNECTED` - MQTT connection lost

#### Cloud Service Errors (CLOUD_*)

- `CLOUD_DATABASE_ERROR` - Database operation failed
- `CLOUD_MQTT_BROKER_DOWN` - MQTT broker unavailable
- `CLOUD_REDIS_ERROR` - Redis cache error
- `CLOUD_STORAGE_FULL` - Storage capacity exceeded

#### Validation Errors (VALIDATION_*)

- `VALIDATION_ERROR` - General validation error
- `VALIDATION_MISSING_FIELD` - Required field missing
- `VALIDATION_INVALID_TYPE` - Invalid data type
- `VALIDATION_OUT_OF_RANGE` - Value out of allowed range

#### Authentication Errors (AUTH_*)

- `AUTH_INVALID_TOKEN` - Invalid JWT or API key
- `AUTH_TOKEN_EXPIRED` - Token expired
- `AUTH_INSUFFICIENT_PERMISSIONS` - Insufficient permissions

#### Rate Limit Errors (RATE_LIMIT_*)

- `RATE_LIMIT_EXCEEDED` - Too many requests
- `RATE_LIMIT_QUOTA_EXCEEDED` - Daily quota exceeded

### 7.4 Retry Strategies

#### Exponential Backoff

```python
def exponential_backoff(attempt: int) -> float:
    return min(2 ** attempt, 60)  # Max 60 seconds
```

#### Retry Logic

```python
def should_retry(status_code: int) -> bool:
    # Retry on 5xx errors and specific 4xx errors
    return status_code >= 500 or status_code in [408, 429]

max_retries = 3
for attempt in range(max_retries):
    response = make_request()

    if response.status_code < 400:
        return response

    if not should_retry(response.status_code):
        raise Exception("Non-retryable error")

    if attempt < max_retries - 1:
        time.sleep(exponential_backoff(attempt))
```

### 7.5 Circuit Breaker Pattern

```python
class CircuitBreaker:
    def __init__(self, failure_threshold=5, timeout=60):
        self.failure_count = 0
        self.failure_threshold = failure_threshold
        self.timeout = timeout
        self.state = "closed"  # closed, open, half_open
        self.last_failure_time = None

    def call(self, func):
        if self.state == "open":
            if time.time() - self.last_failure_time > self.timeout:
                self.state = "half_open"
            else:
                raise Exception("Circuit breaker is open")

        try:
            result = func()
            if self.state == "half_open":
                self.state = "closed"
                self.failure_count = 0
            return result
        except Exception as e:
            self.failure_count += 1
            self.last_failure_time = time.time()

            if self.failure_count >= self.failure_threshold:
                self.state = "open"

            raise e
```

---

## 8. Versioning Strategy

### 8.1 API Versioning

**Version in URL:**

```
https://api.axion.com/v1/cameras
https://api.axion.com/v2/cameras
```

**Advantages:**

- Clear, explicit versioning
- Easy to route to different backends
- Widely used pattern

**Version Header (Alternative):**

```http
GET /cameras HTTP/1.1
Host: api.axion.com
Accept: application/vnd.axion.v1+json
```

### 8.2 Version Lifecycle

| Phase | Duration | Description |
|-------|----------|-------------|
| Beta | 3 months | Testing, feedback, breaking changes allowed |
| Stable | 2+ years | Production use, no breaking changes |
| Deprecated | 6 months | Warning in responses, migration guide published |
| End of Life | - | Version removed |

### 8.3 Deprecation Policy

**Deprecation Warning Header:**

```http
HTTP/1.1 200 OK
X-API-Deprecation-Warning: This API version is deprecated. Migrate to v2 by 2026-06-01.
X-API-Deprecation-Date: 2026-06-01
X-API-Migration-Guide: https://docs.axion.com/migration/v1-to-v2
```

**Deprecation Response Field:**

```json
{
  "data": {/* ... */},
  "deprecation": {
    "deprecated": true,
    "sunset_date": "2026-06-01",
    "migration_guide": "https://docs.axion.com/migration/v1-to-v2",
    "alternative": "GET /v2/cameras"
  }
}
```

### 8.4 Backwards Compatibility

**Non-Breaking Changes (Allowed):**

- Adding new endpoints
- Adding optional request parameters
- Adding new response fields
- Adding new error codes
- Relaxing validation rules

**Breaking Changes (Require New Version):**

- Removing endpoints
- Removing request parameters
- Removing response fields
- Changing field types
- Tightening validation rules
- Changing authentication method

### 8.5 Message Schema Versioning

**MQTT Message Version Field:**

```json
{
  "version": "1.0",
  "msg_type": "metadata",
  /* ... */
}
```

**Version Evolution:**

- `1.0` - Initial schema
- `1.1` - Added optional fields (backwards compatible)
- `2.0` - Breaking changes (clients must handle both)

**Graceful Degradation:**

```python
def handle_metadata(msg):
    version = msg.get("version", "1.0")

    if version.startswith("1."):
        return handle_v1_metadata(msg)
    elif version.startswith("2."):
        return handle_v2_metadata(msg)
    else:
        logger.warning(f"Unknown version: {version}")
        return handle_v1_metadata(msg)  # Fallback
```

---

## 9. Security & Authentication

### 9.1 Camera Authentication

**ACAP HTTP API:**

- HTTP Basic Auth using Axis device credentials
- Leverages existing Axis user pool
- Access levels: viewer (read-only), operator (config), admin (all)

**MQTT:**

- Username/password authentication
- Per-camera credentials generated during registration
- Stored securely in camera ACAP configuration

**TLS Certificates:**

- Mutual TLS (mTLS) for production
- Camera presents client certificate
- Server validates certificate against CA

### 9.2 Cloud API Authentication

**API Keys:**

- Format: `axion_sk_live_*` (production) or `axion_sk_test_*` (testing)
- Scoped permissions (read-only, write, admin)
- Rate limited per key
- Revocable

**JWT Tokens:**

- Short-lived access tokens (1 hour)
- Long-lived refresh tokens (30 days)
- Stored in HTTP-only cookies or Authorization header

**OAuth 2.0 (Future):**

- Third-party integrations
- Standard OAuth 2.0 flows

### 9.3 Network Security

**MQTT TLS:**

```
Broker: mosquitto:8883
Protocol: MQTT over TLS 1.3
Cipher: ECDHE-RSA-AES256-GCM-SHA384
Certificate: Let's Encrypt
```

**HTTPS:**

```
API: https://api.axion.com
Protocol: HTTPS/2
TLS: 1.3
HSTS: Enabled (max-age=31536000)
```

### 9.4 Data Privacy

**PII Handling:**

- No facial recognition stored
- Bounding boxes only, no identity
- Frame data encrypted at rest
- Frame data deleted after retention period

**GDPR Compliance:**

- Right to access: Export all camera data
- Right to erasure: Delete camera and all data
- Data minimization: Store only necessary data

---

## 10. Performance Guidelines

### 10.1 Rate Limits

**Camera ACAP HTTP API:**

| Endpoint | Rate Limit | Burst |
|----------|------------|-------|
| GET /status | 30/min | 60 |
| GET /metadata | 60/min | 100 |
| POST /settings | 5/min | 10 |
| POST /control | 20/min | 30 |
| GET /frame/* | 10/min | 15 |

**Cloud REST API:**

| Tier | Rate Limit | Burst |
|------|------------|-------|
| Free | 100/hour | 150 |
| Standard | 1000/hour | 1500 |
| Enterprise | 10000/hour | 15000 |

**Rate Limit Headers:**

```http
X-RateLimit-Limit: 1000
X-RateLimit-Remaining: 987
X-RateLimit-Reset: 1732363800
X-RateLimit-Retry-After: 3600
```

### 10.2 Caching

**HTTP Response Caching:**

```http
Cache-Control: public, max-age=60
ETag: "a1b2c3d4e5f6"
Last-Modified: Sat, 23 Nov 2025 10:30:00 GMT
```

**Redis Caching:**

- Camera status: 60 seconds TTL
- System metrics: 30 seconds TTL
- Frame metadata: 300 seconds TTL

### 10.3 Pagination

**Default Page Size:** 20

**Maximum Page Size:** 100

**Cursor-Based Pagination (Recommended):**

```http
GET /cameras?limit=20&cursor=eyJpZCI6ImNhbV9hYmMxMjMifQ==
```

**Response:**

```json
{
  "data": [/* ... */],
  "pagination": {
    "has_more": true,
    "next_cursor": "eyJpZCI6ImNhbV94eXo3ODkifQ=="
  }
}
```

### 10.4 Compression

**HTTP Compression:**

```http
Accept-Encoding: gzip, deflate, br
Content-Encoding: br
```

**MQTT Compression:**

- Payloads > 1KB: Consider zlib compression
- Trade-off: CPU vs bandwidth

### 10.5 Performance Targets

**Camera ACAP:**

- Metadata streaming: 5-10 FPS sustained
- Inference latency: < 100ms (p95)
- MQTT publish latency: < 50ms (p95)
- HTTP API response: < 200ms (p95)

**Cloud Services:**

- REST API response: < 300ms (p95)
- WebSocket message delivery: < 100ms (p95)
- Claude analysis: < 2000ms (p95)
- Database query: < 50ms (p95)

**SLO Targets:**

- API uptime: 99.9% (43 minutes downtime/month)
- MQTT broker uptime: 99.95% (21 minutes downtime/month)
- Data durability: 99.999999999% (11 nines)

---

## Appendix A: Example Integration Flow

### End-to-End Flow: Scene Change Detection

1. **Camera captures frame** (10 FPS)
2. **VDO provides frame buffer** (YUV420 format)
3. **Larod runs YOLOv5 inference** (~45ms on DLPU)
4. **Extract metadata** (detections, scene hash)
5. **Detect scene change** (hash comparison)
6. **Publish metadata via MQTT** (QoS 0, 10 FPS)
   - Topic: `axis-is/camera/axis-camera-001/metadata`
7. **Publish scene change event** (QoS 1, on change)
   - Topic: `axis-is/camera/axis-camera-001/event`
8. **Cloud ingests metadata** (Redis + PostgreSQL)
9. **Cloud decides frame request needed**
10. **Cloud publishes frame request** (QoS 1)
    - Topic: `axis-is/cloud/axis-camera-001/frame_request`
11. **Camera retrieves frame from cache**
12. **Camera uploads frame to cloud storage**
13. **Camera publishes frame delivered event**
14. **Cloud triggers Claude analysis**
15. **Claude analyzes frame with context**
16. **Cloud stores analysis result**
17. **Cloud publishes alert if needed** (WebSocket + MQTT)
18. **Dashboard displays real-time update** (WebSocket)

---

## Appendix B: Reference Implementations

### Camera-Side MQTT Publishing (C)

```c
// Publish metadata
cJSON* metadata = cJSON_CreateObject();
cJSON_AddStringToObject(metadata, "version", "1.0");
cJSON_AddStringToObject(metadata, "msg_type", "metadata");
cJSON_AddStringToObject(metadata, "camera_id", camera_id);
cJSON_AddNumberToObject(metadata, "seq", seq_num++);
cJSON_AddStringToObject(metadata, "timestamp", timestamp);
cJSON_AddNumberToObject(metadata, "frame_id", frame_id);

cJSON* scene = cJSON_CreateObject();
cJSON_AddStringToObject(scene, "hash", scene_hash);
cJSON_AddBoolToObject(scene, "changed", scene_changed);
cJSON_AddItemToObject(metadata, "scene", scene);

char* json = cJSON_PrintUnformatted(metadata);
MQTT_Publish("axis-is/camera/axis-camera-001/metadata", json, 0, 0);
free(json);
cJSON_Delete(metadata);
```

### Cloud-Side MQTT Subscriber (Python)

```python
import asyncio
import asyncio_mqtt as aiomqtt
import json

async def mqtt_handler():
    async with aiomqtt.Client("192.168.1.50") as client:
        await client.subscribe("axis-is/camera/+/metadata")

        async for message in client.messages:
            camera_id = message.topic.split('/')[2]
            payload = json.loads(message.payload)

            # Validate sequence number
            validator.validate(camera_id, payload['seq'])

            # Store metadata
            await store_metadata(camera_id, payload)

            # Check for scene change
            if payload['scene']['changed']:
                await handle_scene_change(camera_id, payload)

asyncio.run(mqtt_handler())
```

---

## Appendix C: Glossary

| Term | Definition |
|------|------------|
| ACAP | Axis Camera Application Platform |
| DLPU | Deep Learning Processing Unit (hardware accelerator) |
| FastCGI | Fast Common Gateway Interface (web server protocol) |
| Larod | Axis ML inference library |
| NMS | Non-Maximum Suppression (object detection post-processing) |
| QoS | Quality of Service (MQTT delivery guarantee) |
| VDO | Video Data Object (Axis video capture API) |
| YUV420 | Video color format (native camera output) |

---

**Document Version:** 1.0.0
**Last Updated:** November 23, 2025
**Status:** Production-Ready Design
**Next Review:** March 2026
