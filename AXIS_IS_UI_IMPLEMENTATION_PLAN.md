# Plan: Axis I.S. UI Implementation

## Overview

Implement web UIs for both the Edge Camera (ACAP) and Hub Dashboard following Project Sentient Edge architecture and Axis Communications brand colors.

**Color Palette:**
- Black `#000000` - Primary text, headers
- Yellow `#FFD200` - Primary accent, CTAs, highlights
- Red `#B22234` - Alerts, warnings, danger actions
- White `#FFFFFF` - Backgrounds
- Grays - Derived scale for borders/secondary text

---

## Part 1: Edge Camera UI (Vanilla HTML/CSS/JS)

### Technology
- Pure HTML5, CSS3, JavaScript (ES6+)
- No frameworks (ACAP memory constraints ~256-512MB)
- FastCGI served via existing ACAP.c

### File Structure
```
poc/camera/app/
└── html/
    ├── index.html          # Dashboard
    ├── settings.html       # Configuration
    ├── search.html         # Local search
    ├── css/
    │   ├── axis-theme.css  # Brand colors + variables
    │   ├── components.css  # Buttons, cards, forms
    │   └── layout.css      # Page structure
    ├── js/
    │   ├── api.js          # HTTP client wrapper
    │   ├── status.js       # Status polling
    │   ├── detections.js   # Canvas overlay
    │   ├── settings.js     # Form management
    │   └── modules.js      # Module control
    └── assets/
        └── icons/          # SVG icons
```

### Pages

**1. Dashboard (index.html)**
- Live status: FPS, frames, uptime, MQTT state
- Detection canvas: Frame preview + bounding boxes
- Module list: Name, version, priority, enable/disable
- Motion score indicator

**2. Settings (settings.html)**
- Card per config file: core, mqtt, detection, frame_publisher
- Form validation with immediate feedback
- MQTT connection test button
- Save/restore defaults

**3. Search (search.html)**
- Time range filter (1h, 6h, 24h, 72h)
- Object class dropdown
- Confidence threshold slider
- Paginated results table

### New API Endpoints (add to main.c)
```
GET  /local/axis_is_poc/detections      # Latest detections array
GET  /local/axis_is_poc/frame/preview   # JPEG snapshot (320x320)
GET  /local/axis_is_poc/config/{module} # Module config
POST /local/axis_is_poc/config/{module} # Update config
POST /local/axis_is_poc/module/{name}/toggle  # Enable/disable
POST /local/axis_is_poc/mqtt/test       # Test MQTT connection
GET  /local/axis_is_poc/search          # Query local metadata
GET  /local/axis_is_poc/ui/*            # Static file serving
```

### Files to Modify
- `poc/camera/app/ACAP.c` - Add static file serving + content-type detection
- `poc/camera/app/main.c` - Register new HTTP endpoints
- `poc/camera/app/manifest.json` - Add httpConfig entries for new endpoints
- `poc/camera/app/core.h` - Add last_metadata field to CoreContext

### Implementation Order (Edge)
1. **Week 1**: Create html/ structure, CSS theme, static file serving in ACAP.c
2. **Week 2**: Dashboard with status polling + module list
3. **Week 3**: Detection canvas with bounding box overlay
4. **Week 4**: Settings forms with validation
5. **Week 5**: Search interface with pagination
6. **Week 6**: Polish, memory optimization, testing

---

## Part 2: Hub Dashboard (React)

### Technology
- React 18+ with TypeScript
- Vite build tool
- Tailwind CSS (configured with Axis colors)
- Context API for state management
- WebSocket for real-time updates

### File Structure
```
cloud-service/
└── frontend/
    ├── src/
    │   ├── api/              # REST + WebSocket clients
    │   ├── components/
    │   │   ├── layout/       # Header, Sidebar, MainLayout
    │   │   ├── cameras/      # CameraGrid, CameraCard, CameraDetails
    │   │   ├── detections/   # DetectionFeed, BoundingBoxOverlay
    │   │   ├── query/        # ConversationalInput, QueryBuilder
    │   │   ├── zones/        # ZoneCanvas, PolygonEditor
    │   │   ├── heatmaps/     # HeatmapCanvas, DwellTimeMap
    │   │   ├── unusuals/     # UnusualsFeed, FeedbackButtons
    │   │   ├── investigations/ # InvestigationThread, ReportGenerator
    │   │   └── common/       # Button, Card, Modal, Badge
    │   ├── pages/            # Route components
    │   ├── hooks/            # useWebSocket, useCameras, useDetections
    │   ├── context/          # WebSocket, Camera, Theme providers
    │   ├── styles/           # globals.css, variables.css
    │   └── types/            # TypeScript interfaces
    ├── package.json
    ├── vite.config.ts
    └── tailwind.config.js
```

### Routes
| Path | Page | Features |
|------|------|----------|
| `/` | Dashboard | Camera grid, detection feed, system status |
| `/cameras/:id` | Camera Detail | Live view, bounding boxes, zone drawing, point-and-ask |
| `/query` | Query | NL input, query builder, results grid |
| `/analytics` | Analytics | Heatmaps (dwell, traffic, density) |
| `/unusuals` | Unusuals | Anomaly feed with feedback |
| `/investigations` | Investigations | Thread list, evidence gallery |
| `/settings` | Settings | System + per-camera config |

### Key Features

**1. Conversational Query**
- Natural language input field
- Claude parses intent → structured query
- Query builder shows/edits parsed query
- Results with confidence indicators

**2. Point-and-Ask**
- Click bounding box on live view
- Contextual menu: "Track this", "Find similar", "Add to investigation"

**3. Zone Drawing**
- Canvas-based polygon drawing
- Named zones saved per camera
- Zone-based queries: "Count people in Entry zone"

**4. Unusuals Feed**
- Z-score based anomaly detection
- Thumbs up/down feedback trains model
- Sensitivity slider

**5. Heatmaps**
- Dwell time, traffic flow, density
- Canvas gradient rendering
- Time range + camera selectors

### New Backend Endpoints (add to main.py)
```python
# WebSocket
WS  /ws                                    # Real-time stream

# Query
POST /api/v1/query/natural-language        # NL → structured query
POST /api/v1/query/structured              # Execute query

# Zones
GET  /api/v1/cameras/{id}/zones            # List zones
POST /api/v1/cameras/{id}/zones            # Create zone
DELETE /api/v1/cameras/{id}/zones/{zid}    # Delete zone

# Heatmaps
GET  /api/v1/cameras/{id}/heatmap/{type}   # Heatmap data

# Unusuals
GET  /api/v1/unusuals                      # Anomaly feed
POST /api/v1/unusuals/{id}/feedback        # Submit feedback

# Investigations
GET  /api/v1/investigations                # List threads
POST /api/v1/investigations                # Create thread
POST /api/v1/investigations/{id}/evidence  # Add evidence
GET  /api/v1/investigations/{id}/export    # PDF export
```

### Files to Modify
- `cloud-service/main.py` - Add WebSocket endpoint, new REST routes
- `cloud-service/mqtt_handler.py` - Broadcast to WebSocket clients
- `cloud-service/database.py` - New tables: zones, investigations, feedback
- `cloud-service/docker-compose.yml` - Add frontend service

### Implementation Order (Hub)
1. **Weeks 1-2**: Project setup, Tailwind config, layout, camera grid
2. **Weeks 3-4**: WebSocket, detection feed, camera details, point-and-ask
3. **Weeks 5-6**: Query interface, zone drawing
4. **Weeks 7-8**: Heatmaps, analytics
5. **Weeks 9-10**: Unusuals feed, investigations, reports
6. **Weeks 11-12**: Settings, polish, testing

---

## CSS Theme (Both UIs)

```css
:root {
  /* Axis Brand */
  --axis-black: #000000;
  --axis-yellow: #FFD200;
  --axis-red: #B22234;
  --axis-white: #FFFFFF;

  /* Semantic */
  --color-primary: var(--axis-yellow);
  --color-danger: var(--axis-red);
  --color-success: #22C55E;

  /* Status */
  --status-online: #22C55E;
  --status-offline: #EF4444;
  --status-degraded: #F59E0B;
}

/* Button: Yellow background, black text */
.btn-primary {
  background: var(--axis-yellow);
  color: var(--axis-black);
}

/* Header: Black background, white text, yellow accents */
.app-header {
  background: var(--axis-black);
  color: var(--axis-white);
}

/* Cards: White background, subtle border */
.card {
  background: var(--axis-white);
  border: 1px solid #E5E5E5;
}

/* Selected state: Yellow border */
.card.selected {
  border-color: var(--axis-yellow);
}
```

---

## Summary

| Component | Tech | Pages | Timeline |
|-----------|------|-------|----------|
| Edge UI | Vanilla JS | 3 | 6 weeks |
| Hub UI | React | 7 | 12 weeks |

**Total: 12 weeks** (Edge and Hub can be developed in parallel)

### Critical Files
**Edge:**
- `poc/camera/app/ACAP.c`
- `poc/camera/app/main.c`
- `poc/camera/app/manifest.json`

**Hub:**
- `cloud-service/main.py`
- `cloud-service/mqtt_handler.py`
- `cloud-service/database.py`
