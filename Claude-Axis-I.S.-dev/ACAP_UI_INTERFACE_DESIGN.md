# ACAP UI Interface Design Document
## Project Sentient Edge - Camera-Side Interface

**Version:** 1.0
**Date:** November 24, 2024
**Status:** Design Specification
**Target Platform:** AXIS ACAP Native SDK on ARTPEC-8/9 SoC

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Context](#architecture-context)
3. [UI Components Overview](#ui-components-overview)
4. [Detailed Component Specifications](#detailed-component-specifications)
5. [HTTP API Endpoints](#http-api-endpoints)
6. [CGI Configuration Interface](#cgi-configuration-interface)
7. [WebSocket Real-Time Updates](#websocket-real-time-updates)
8. [Mobile-Responsive Design](#mobile-responsive-design)
9. [Implementation Technologies](#implementation-technologies)
10. [Security Considerations](#security-considerations)
11. [Performance Constraints](#performance-constraints)
12. [Future Enhancements](#future-enhancements)

---

## Executive Summary

This document defines the camera-side UI interface for the Axis I.S. ACAP application, designed to provide:

1. **Real-time monitoring** of edge AI processing (detections, classifications, anomalies)
2. **Configuration management** for detection parameters and cloud integration
3. **Diagnostic information** for system health and performance
4. **Preview capability** showing what the AI "sees" with annotated bounding boxes

**Key Design Principles:**
- **Lightweight**: Minimal resource consumption on edge device
- **Browser-based**: Accessible via camera's web interface (no app installation)
- **Responsive**: Works on desktop, tablet, and mobile
- **Integrated**: Seamlessly fits within AXIS OS web UI
- **Zero-latency preview**: Live view of AI detections without cloud round-trip

---

## Architecture Context

### System Overview

```
┌─────────────────────────────────────────────────┐
│         AXIS Camera (ARTPEC-9)                  │
├─────────────────────────────────────────────────┤
│                                                 │
│  ┌──────────────┐      ┌──────────────┐       │
│  │ VDO Stream   │─────>│ Detection    │       │
│  │ (10 FPS)     │      │ Module       │       │
│  └──────────────┘      └──────┬───────┘       │
│                               │                 │
│                               v                 │
│  ┌──────────────────────────────────────┐     │
│  │    HTTP/CGI Server (Built-in)        │     │
│  │  ┌────────────────────────────────┐  │     │
│  │  │  /local/axis_is_poc/           │  │     │
│  │  │  ├─ status.cgi                 │  │     │
│  │  │  ├─ detections.cgi (WebSocket) │  │     │
│  │  │  ├─ preview.jpg (MJPEG)        │  │     │
│  │  │  ├─ config.cgi                 │  │     │
│  │  │  └─ index.html (UI)            │  │     │
│  │  └────────────────────────────────┘  │     │
│  └──────────────────────────────────────┘     │
│                                                 │
│  User accesses: http://camera-ip/local/axis_is_poc/
│                                                 │
└─────────────────────────────────────────────────┘
```

### Integration with AXIS OS

The ACAP UI will be accessible through:
1. **Direct URL**: `http://<camera-ip>/local/axis_is_poc/`
2. **AXIS OS Apps Menu**: Appears as "Axis I.S. - Sentient Edge"
3. **Configuration Page**: Settings accessible via camera's native web UI

---

## UI Components Overview

### Component Hierarchy

```
┌──────────────────────────────────────────────────────────┐
│                    Main Dashboard                         │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Header Bar                                        │  │
│  │  - Logo | Status Indicator | Settings Icon        │  │
│  └────────────────────────────────────────────────────┘  │
│                                                           │
│  ┌─────────────────────┬─────────────────────────────┐  │
│  │                     │                              │  │
│  │  Live Preview Pane  │   Detection Feed Sidebar    │  │
│  │  (416x416 canvas)   │   (Recent detections list)  │  │
│  │  + Bounding boxes   │                              │  │
│  │  + Labels           │   ┌────────────────────┐    │  │
│  │  + Confidence       │   │ [Person] 89%       │    │  │
│  │                     │   │ @2024-11-24 10:15  │    │  │
│  │  ┌──────┐           │   └────────────────────┘    │  │
│  │  │Person│           │                              │  │
│  │  │ 92% │            │   ┌────────────────────┐    │  │
│  │  └──────┘           │   │ [Vehicle] 76%      │    │  │
│  │         ┌─────┐     │   │ @2024-11-24 10:14  │    │  │
│  │         │Car  │     │   └────────────────────┘    │  │
│  │         │ 88% │     │                              │  │
│  │         └─────┘     │   [Scroll...]              │  │
│  │                     │                              │  │
│  └─────────────────────┴─────────────────────────────┘  │
│                                                           │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Statistics Panel (Bottom)                         │  │
│  │  FPS: 10.2 | Detections/min: 47 | Uptime: 5d 3h   │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

---

## Detailed Component Specifications

### 1. Header Bar

**Purpose:** Application identity, status, and navigation

**Elements:**
- **Logo**: Axis I.S. branding with Sentient Edge tagline
- **Status Indicator**:
  - 🟢 Green: All systems operational
  - 🟡 Yellow: Cloud disconnected (edge-only mode)
  - 🔴 Red: Critical error
- **Cloud Sync Icon**: Shows when syncing to cloud service
- **Settings Gear Icon**: Opens configuration modal

**Visual Design:**
```html
┌───────────────────────────────────────────────────┐
│ 📹 Axis I.S. Sentient Edge    🟢 Active   ⚙️     │
└───────────────────────────────────────────────────┘
```

**Implementation:**
- Fixed position header (always visible)
- Height: 60px
- Background: Dark gray (#2c3e50)
- Text: White with blue accents

---

### 2. Live Preview Pane

**Purpose:** Real-time visual feedback of AI detections on camera feed

**Features:**
- **Canvas Rendering**: 416x416px detection frame
- **Bounding Boxes**: Color-coded by class
  - Person: Blue (#3498db)
  - Vehicle: Red (#e74c3c)
  - Animal: Green (#2ecc71)
  - Unknown: Gray (#95a5a6)
- **Labels**: Object class + confidence percentage
- **Motion Indicator**: Visual overlay showing motion score (0-100%)
- **Refresh Rate**: Updates at detection FPS (typically 10 FPS)

**Interaction:**
- Click on bounding box → Shows detailed object info modal
- Hover over detection → Highlights and shows timestamp
- Double-click → Freezes frame for inspection

**Technical Implementation:**
```javascript
// Canvas-based rendering for performance
const canvas = document.getElementById('preview-canvas');
const ctx = canvas.getContext('2d');

// WebSocket stream for real-time updates
const ws = new WebSocket('ws://camera-ip/local/axis_is_poc/detections');
ws.onmessage = (event) => {
    const frame = JSON.parse(event.data);
    renderDetections(frame);
};

function renderDetections(frame) {
    // Draw frame image
    ctx.drawImage(frame.image, 0, 0, 416, 416);

    // Overlay bounding boxes
    frame.detections.forEach(det => {
        ctx.strokeStyle = getClassColor(det.class_id);
        ctx.strokeRect(
            det.x * 416,
            det.y * 416,
            det.width * 416,
            det.height * 416
        );

        // Draw label
        ctx.fillStyle = 'rgba(0,0,0,0.7)';
        ctx.fillRect(det.x * 416, det.y * 416 - 20, 80, 20);
        ctx.fillStyle = 'white';
        ctx.fillText(
            `${det.class_name} ${Math.round(det.confidence * 100)}%`,
            det.x * 416 + 5,
            det.y * 416 - 5
        );
    });
}
```

---

### 3. Detection Feed Sidebar

**Purpose:** Scrollable list of recent detections with metadata

**Layout:**
```
┌─────────────────────────┐
│ Detection Feed          │
│ ┌─────────────────────┐ │
│ │ [Icon] Person       │ │
│ │ Confidence: 92%     │ │
│ │ Location: Zone A    │ │
│ │ Time: 10:15:32      │ │
│ │ [Track] [Details]   │ │
│ └─────────────────────┘ │
│                         │
│ ┌─────────────────────┐ │
│ │ [Icon] Vehicle      │ │
│ │ Confidence: 88%     │ │
│ │ Color: Blue         │ │
│ │ Time: 10:14:58      │ │
│ │ [Track] [Details]   │ │
│ └─────────────────────┘ │
│                         │
│ [Load More...]          │
└─────────────────────────┘
```

**Features:**
- **Auto-scroll**: New detections appear at top
- **Filtering**: Filter by class, confidence, time range
- **Action Buttons**:
  - "Track": Highlight object in live preview
  - "Details": Open detailed view with full metadata
  - "Send to Cloud": Manually trigger cloud analysis

**Data Structure:**
```json
{
  "id": 12345,
  "timestamp_us": 1700000000000000,
  "class_id": 0,
  "class_name": "person",
  "confidence": 0.92,
  "bbox": {"x": 0.3, "y": 0.4, "width": 0.15, "height": 0.3},
  "motion_score": 0.75,
  "zone": "entrance",
  "metadata": {
    "color": null,
    "direction": "north",
    "duration_seconds": 12
  }
}
```

---

### 4. Statistics Panel

**Purpose:** System health and performance metrics

**Metrics Displayed:**
- **Current FPS**: Actual processing rate
- **Detections/Minute**: Rolling average
- **System Uptime**: Application runtime
- **CPU Usage**: % utilization
- **Memory**: Used/Available
- **MQTT Status**: Connected/Disconnected
- **Cloud Sync**: Last successful sync timestamp
- **Frame Queue**: Pending frames for processing

**Visual Design:**
```
┌────────────────────────────────────────────────────┐
│ FPS: 10.2 │ Det/min: 47 │ Uptime: 5d 3h 12m       │
│ CPU: 22%  │ RAM: 412MB  │ MQTT: ✓ │ Cloud: ✓     │
└────────────────────────────────────────────────────┘
```

**Color Coding:**
- Green: Normal operation
- Yellow: Warning threshold (>80% resource usage)
- Red: Critical (>95% or service down)

---

### 5. Configuration Modal

**Purpose:** Manage detection settings and cloud integration

**Tabs:**
1. **Detection Settings**
2. **Cloud Configuration**
3. **Zones & Triggers**
4. **Advanced**

#### Tab 1: Detection Settings

```
┌──────────────────────────────────────┐
│ Detection Settings                   │
├──────────────────────────────────────┤
│                                      │
│ Target FPS:         [10    ] ▼      │
│ Confidence Threshold: [0.25] ━━●━━  │
│                                      │
│ Enabled Classes:                     │
│ ☑ Person                             │
│ ☑ Vehicle (Car, Truck, Bus)          │
│ ☑ Bicycle / Motorcycle               │
│ ☐ Animal                             │
│ ☐ All 80 COCO Classes                │
│                                      │
│ [Save] [Cancel] [Reset to Default]  │
└──────────────────────────────────────┘
```

#### Tab 2: Cloud Configuration

```
┌──────────────────────────────────────┐
│ Cloud Integration                    │
├──────────────────────────────────────┤
│                                      │
│ MQTT Broker:  [10.0.0.3        ]    │
│ Port:         [1883            ]    │
│ Camera ID:    [axis-camera-001 ]    │
│                                      │
│ ☑ Enable Cloud Sync                 │
│ ☑ Send Metadata (Always)             │
│ ☑ Send Frames (On Trigger)           │
│                                      │
│ Trigger Conditions:                  │
│ ☑ Motion Score > [0.7] ━━━●━         │
│ ☑ Vehicle Detected                   │
│ ☑ Scene Change                       │
│                                      │
│ Frame Rate Limit:                    │
│   Max 1 frame per [60] seconds       │
│                                      │
│ [Test Connection] [Save]             │
└──────────────────────────────────────┘
```

#### Tab 3: Zones & Triggers

```
┌──────────────────────────────────────┐
│ Zones & Intelligent Triggers         │
├──────────────────────────────────────┤
│                                      │
│ Define Detection Zones:              │
│ ┌────────────────────────────┐      │
│ │   [Live Preview Canvas]    │      │
│ │   (Draw zones with mouse)  │      │
│ │                            │      │
│ │   Zone A: Entrance ■       │      │
│ │   Zone B: Parking  ■       │      │
│ └────────────────────────────┘      │
│                                      │
│ [Add Zone] [Delete Selected]         │
│                                      │
│ Trigger Rules:                       │
│ IF [Person] in [Zone A]              │
│    THEN [Send Alert]                 │
│ [+ Add Rule]                         │
│                                      │
│ [Save Zones & Rules]                 │
└──────────────────────────────────────┘
```

#### Tab 4: Advanced

```
┌──────────────────────────────────────┐
│ Advanced Settings                    │
├──────────────────────────────────────┤
│                                      │
│ Hardware Acceleration:               │
│ ● DLPU (ARTPEC-9)                    │
│ ○ CPU Only (Fallback)                │
│                                      │
│ Logging Level:                       │
│ [INFO        ] ▼                     │
│                                      │
│ Data Retention:                      │
│ Keep detections for [7] days         │
│                                      │
│ Factory Reset:                       │
│ [Reset All Settings] (Requires PW)  │
│                                      │
│ Diagnostics:                         │
│ [Download Logs] [Run Self-Test]     │
│                                      │
│ [Save]                               │
└──────────────────────────────────────┘
```

---

## HTTP API Endpoints

### Base Path
All endpoints are served under: `/local/axis_is_poc/`

### Endpoint Specifications

#### 1. `GET /status`
**Purpose:** Application health and system status

**Response:**
```json
{
  "app": "axis_is_poc",
  "version": "2.0.0",
  "status": "running",
  "uptime_seconds": 432000,
  "fps": {
    "target": 10,
    "actual": 10.2
  },
  "resources": {
    "cpu_percent": 22.5,
    "memory_mb": 412,
    "memory_total_mb": 1024
  },
  "services": {
    "vdo": "connected",
    "larod": "connected",
    "mqtt": "connected",
    "cloud": "synced"
  },
  "detections": {
    "total_count": 142573,
    "rate_per_minute": 47
  }
}
```

#### 2. `GET /detections` (WebSocket)
**Purpose:** Real-time stream of detection events

**Protocol:** WebSocket (`ws://camera-ip/local/axis_is_poc/detections`)

**Message Format:**
```json
{
  "frame_id": 12345,
  "timestamp_us": 1700000000000000,
  "motion_score": 0.75,
  "detections": [
    {
      "class_id": 0,
      "class_name": "person",
      "confidence": 0.92,
      "bbox": {"x": 0.3, "y": 0.4, "width": 0.15, "height": 0.3}
    }
  ],
  "scene_hash": 3849582734
}
```

**Connection:**
```javascript
const ws = new WebSocket('ws://camera-ip/local/axis_is_poc/detections');

ws.onopen = () => {
    console.log('Connected to detection stream');
};

ws.onmessage = (event) => {
    const frame = JSON.parse(event.data);
    updateUI(frame);
};

ws.onerror = (error) => {
    console.error('WebSocket error:', error);
    reconnect();
};
```

#### 3. `GET /preview.jpg` (MJPEG Stream)
**Purpose:** Live annotated video feed with bounding boxes

**Response:** MJPEG stream (multipart/x-mixed-replace)

**Usage:**
```html
<img src="/local/axis_is_poc/preview.jpg"
     alt="Live Preview"
     style="width: 416px; height: 416px;">
```

**Frame Rate:** Matches detection FPS (typically 10 FPS)

#### 4. `GET /config`
**Purpose:** Retrieve current configuration

**Response:**
```json
{
  "detection": {
    "target_fps": 10,
    "confidence_threshold": 0.25,
    "enabled_classes": [0, 1, 2, 3, 5, 7]
  },
  "mqtt": {
    "broker": "10.0.0.3",
    "port": 1883,
    "camera_id": "axis-camera-001"
  },
  "cloud": {
    "enabled": true,
    "send_metadata": true,
    "send_frames_on_trigger": true,
    "motion_threshold": 0.7,
    "frame_rate_limit_seconds": 60
  },
  "zones": []
}
```

#### 5. `POST /config`
**Purpose:** Update configuration settings

**Request Body:** (Same structure as GET /config response)

**Response:**
```json
{
  "status": "success",
  "message": "Configuration updated",
  "requires_restart": false
}
```

#### 6. `GET /history?start=<timestamp>&end=<timestamp>&class=<class_id>`
**Purpose:** Query historical detection data

**Query Parameters:**
- `start`: Unix timestamp (microseconds)
- `end`: Unix timestamp (microseconds)
- `class`: Filter by class ID (optional)
- `limit`: Max results (default: 100)

**Response:**
```json
{
  "count": 47,
  "detections": [
    {
      "id": 12345,
      "timestamp_us": 1700000000000000,
      "class_id": 0,
      "class_name": "person",
      "confidence": 0.92,
      "bbox": {"x": 0.3, "y": 0.4, "width": 0.15, "height": 0.3},
      "metadata": {}
    }
  ]
}
```

#### 7. `POST /trigger-frame`
**Purpose:** Manually request cloud analysis of current frame

**Response:**
```json
{
  "status": "requested",
  "frame_id": 12346,
  "message": "Frame sent to cloud for analysis"
}
```

---

## CGI Configuration Interface

### AXIS OS Integration

The ACAP must provide configuration parameters accessible through AXIS OS web interface.

**File:** `poc/camera/app/param.conf`

```bash
# Axis I.S. Configuration Parameters
root.AxisIS.Detection.TargetFPS=10
root.AxisIS.Detection.ConfidenceThreshold=0.25
root.AxisIS.Detection.EnabledClasses=0,1,2,3,5,7

root.AxisIS.MQTT.Broker=10.0.0.3
root.AxisIS.MQTT.Port=1883
root.AxisIS.MQTT.CameraID=axis-camera-001

root.AxisIS.Cloud.Enabled=yes
root.AxisIS.Cloud.MotionThreshold=0.7
root.AxisIS.Cloud.FrameRateLimitSeconds=60
```

**Access via AXIS Parameter API:**
```bash
# Get parameter
curl "http://camera-ip/axis-cgi/param.cgi?action=list&group=root.AxisIS"

# Set parameter
curl "http://camera-ip/axis-cgi/param.cgi?action=update&root.AxisIS.Detection.TargetFPS=15"
```

---

## WebSocket Real-Time Updates

### Connection Management

**Heartbeat Protocol:**
```json
// Client → Server (every 30 seconds)
{"type": "ping", "timestamp": 1700000000}

// Server → Client
{"type": "pong", "timestamp": 1700000000}
```

**Subscription Model:**
```json
// Client requests specific data streams
{
  "action": "subscribe",
  "streams": ["detections", "statistics", "alerts"]
}

// Server confirms
{
  "status": "subscribed",
  "streams": ["detections", "statistics", "alerts"]
}
```

### Message Types

1. **Detection Event:**
```json
{
  "type": "detection",
  "data": { /* detection object */ }
}
```

2. **Statistics Update:**
```json
{
  "type": "statistics",
  "data": {
    "fps": 10.2,
    "cpu_percent": 22.5,
    "detections_per_minute": 47
  }
}
```

3. **Alert:**
```json
{
  "type": "alert",
  "severity": "warning",
  "message": "High CPU usage detected",
  "timestamp": 1700000000
}
```

---

## Mobile-Responsive Design

### Breakpoints

```css
/* Desktop */
@media (min-width: 1024px) {
  .preview-pane { width: 60%; }
  .sidebar { width: 40%; }
}

/* Tablet */
@media (min-width: 768px) and (max-width: 1023px) {
  .preview-pane { width: 100%; }
  .sidebar { width: 100%; margin-top: 20px; }
}

/* Mobile */
@media (max-width: 767px) {
  .header { height: 50px; font-size: 14px; }
  .preview-pane { width: 100%; }
  .sidebar { display: none; } /* Accessible via tab/drawer */
  .statistics-panel {
    display: grid;
    grid-template-columns: 1fr 1fr;
    font-size: 12px;
  }
}
```

### Touch Interactions

- **Tap on detection**: Show details modal
- **Pinch-to-zoom**: On preview canvas
- **Swipe right**: Open detection sidebar
- **Pull-to-refresh**: Reload current view

---

## Implementation Technologies

### Frontend Stack

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Axis I.S. - Sentient Edge</title>

    <!-- Lightweight CSS Framework -->
    <link rel="stylesheet" href="https://unpkg.com/tailwindcss@2.2.19/dist/tailwind.min.css">

    <!-- Chart.js for statistics -->
    <script src="https://cdn.jsdelivr.net/npm/chart.js@3.9.1/dist/chart.min.js"></script>

    <!-- Custom CSS -->
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <!-- UI Components -->
    <div id="app"></div>

    <!-- Vanilla JS (No framework overhead) -->
    <script src="app.js"></script>
</body>
</html>
```

### Backend (ACAP C Code)

**CGI Handler:**
```c
// poc/camera/app/http_handler.c
#include <fcgi_stdio.h>
#include "cJSON.h"

void handle_status_request(void) {
    FCGI_puts("Content-Type: application/json\r\n\r\n");

    cJSON* status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "app", "axis_is_poc");
    cJSON_AddStringToObject(status, "status", "running");
    cJSON_AddNumberToObject(status, "uptime_seconds", get_uptime());

    char* json_str = cJSON_PrintUnformatted(status);
    FCGI_puts(json_str);

    free(json_str);
    cJSON_Delete(status);
}

void handle_detections_websocket(void) {
    // WebSocket implementation using libwebsockets
    // Streams detection_module output in real-time
}
```

**Configuration:**
`poc/camera/app/cgi.conf`
```
[cgi]
status.cgi=/usr/local/packages/axis_is_poc/http_handler status
config.cgi=/usr/local/packages/axis_is_poc/http_handler config
trigger-frame.cgi=/usr/local/packages/axis_is_poc/http_handler trigger
```

---

## Security Considerations

### Authentication

1. **AXIS OS Integration**: Inherits camera's authentication
2. **Session Management**: Uses camera's existing session tokens
3. **API Access**: Requires authentication header

```bash
curl -H "Authorization: Bearer <camera-session-token>" \
     http://camera-ip/local/axis_is_poc/status
```

### HTTPS

- All connections should use HTTPS when camera is configured with TLS
- WebSocket upgrades to WSS (WebSocket Secure)

### Input Validation

```c
// Validate all CGI parameters
int validate_fps_param(const char* value) {
    int fps = atoi(value);
    if (fps < 1 || fps > 30) {
        return -1; // Invalid
    }
    return fps;
}
```

### Rate Limiting

- Max 100 API requests per minute per client
- WebSocket: Max 1 connection per client IP
- Frame requests: Max 1 per 60 seconds (configurable)

---

## Performance Constraints

### Resource Budget

| Component | CPU | Memory | Network |
|-----------|-----|--------|---------|
| UI Rendering | 2% | 50MB | - |
| WebSocket Stream | 3% | 20MB | 100 KB/s |
| HTTP Server | 1% | 30MB | - |
| **Total UI Overhead** | **6%** | **100MB** | **100 KB/s** |

**Camera Total Budget:**
- CPU: 20% for AI detection + 6% for UI = 26%
- Memory: 400MB for AI + 100MB for UI = 500MB
- Network: 2-8 KB/s metadata + 100 KB/s UI = ~110 KB/s

### Optimization Strategies

1. **Lazy Loading**: Load UI assets only when accessed
2. **Canvas Rendering**: Use hardware-accelerated canvas instead of DOM manipulation
3. **Throttling**: Limit WebSocket message rate to 10 FPS
4. **Compression**: Gzip all HTTP responses
5. **Caching**: Browser cache for static assets (CSS, JS, images)

---

## Future Enhancements

### Phase 2 Features

1. **Natural Language Query Interface**
   - Text input: "Show me all people detected in the last hour"
   - Backend LLM translates to API query
   - Results displayed in UI

2. **Zone Drawing Tool**
   - Interactive canvas for defining detection zones
   - Save zones with semantic names ("Entrance", "Restricted Area")
   - Trigger alerts when objects enter/exit zones

3. **Historical Playback**
   - Timeline scrubber for reviewing past detections
   - Filter by class, confidence, time
   - Export clips with annotations

4. **Multi-Camera Dashboard**
   - Grid view of multiple cameras
   - Unified detection feed from all cameras
   - Cross-camera tracking

5. **Mobile App (PWA)**
   - Install as native app on iOS/Android
   - Push notifications for alerts
   - Offline mode with cached data

6. **AR Integration**
   - Point phone at physical space
   - Overlay recent detections in 3D space
   - "See" what the AI saw hours ago

---

## Implementation Checklist

### Phase 1: Minimum Viable Product (MVP)

- [ ] Basic HTML/CSS/JS structure
- [ ] HTTP status endpoint (`/status`)
- [ ] WebSocket detection stream (`/detections`)
- [ ] MJPEG preview stream (`/preview.jpg`)
- [ ] Configuration modal (read-only)
- [ ] Statistics panel
- [ ] Mobile-responsive layout

### Phase 2: Full Feature Set

- [ ] Configuration management (read-write)
- [ ] Historical query endpoint (`/history`)
- [ ] Zone drawing tool
- [ ] Alert system
- [ ] Manual frame trigger button
- [ ] Session management
- [ ] Localization (English, Swedish)

### Phase 3: Advanced Features

- [ ] Natural language query interface
- [ ] Multi-camera support
- [ ] AR mobile app prototype
- [ ] Integration with AXIS Video Motion Detection (VMD)
- [ ] Custom AI model upload interface

---

## Appendix: File Structure

```
poc/camera/app/
├── index.html              # Main UI entry point
├── style.css               # Custom styles
├── app.js                  # Frontend JavaScript
├── http_handler.c          # CGI request handler
├── websocket_handler.c     # WebSocket server
├── cgi.conf                # CGI routing configuration
├── manifest.json           # Updated with UI entry point
└── assets/
    ├── logo.png
    ├── favicon.ico
    └── icons/
        ├── person.svg
        ├── vehicle.svg
        └── alert.svg
```

---

## Conclusion

This UI design provides a **comprehensive, lightweight, and integrated interface** for the Axis I.S. ACAP application. It balances edge device constraints with modern web UX expectations, positioning the system for seamless integration with Project Sentient Edge's conversational AI vision.

**Next Steps:**
1. Review and approve design
2. Create HTML/CSS/JS prototypes
3. Implement CGI handlers in C
4. Test on AXIS P3285-LVE camera
5. Iterate based on user feedback

**Estimated Development Time:**
- MVP (Phase 1): 3-4 weeks
- Full Feature Set (Phase 2): 6-8 weeks
- Advanced Features (Phase 3): 10-12 weeks

---

**Document End**
