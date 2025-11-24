# Axis I.S. Complete Deployment Guide

**Edge-Cloud AI Camera Surveillance Platform**

This guide covers end-to-end deployment of the complete Axis I.S. platform with Claude Vision integration.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    CAMERA EDGE (AXIS OS 12)                  │
├─────────────────────────────────────────────────────────────┤
│  VDO Stream (416x416 @ 10 FPS)                              │
│         ↓                                                    │
│  Detection Module (YOLOv5n)                                 │
│         ↓                                                    │
│  Metadata Extraction                                         │
│         ↓                                                    │
│  MQTT Publisher → metadata (10 FPS)                         │
│         ↓                                                    │
│  Frame Publisher ← frame request (on-demand)                │
│         ↓                                                    │
│  MQTT Publisher → frame (JPEG + Base64)                     │
└─────────────────────────────────────────────────────────────┘
                          ↓ MQTT
┌─────────────────────────────────────────────────────────────┐
│                    CLOUD INFRASTRUCTURE                      │
├─────────────────────────────────────────────────────────────┤
│  Mosquitto MQTT Broker                                      │
│         ↓                                                    │
│  Cloud Service (Python FastAPI)                             │
│    ├─ MQTT Handler (subscribe metadata + frames)           │
│    ├─ Scene Memory (last 30 frames in Redis)               │
│    ├─ Trigger Logic (motion, vehicles, scene change)       │
│    ├─ Frame Request (when interesting)                      │
│    └─ Claude Agent (Vision API analysis)                    │
│         ↓                                                    │
│  PostgreSQL (events, analyses, alerts)                      │
│  Redis (state, scene memory, rate limiting)                 │
└─────────────────────────────────────────────────────────────┘
```

---

## Part 1: Cloud Service Deployment

### Prerequisites
- Ubuntu 20.04+ or similar Linux server
- Docker & Docker Compose installed
- Anthropic API key (Claude)
- Public IP or domain (for camera connectivity)

### Step 1: Clone Repository

```bash
cd /opt
git clone https://github.com/oneshot2001/Claude-Axis-I.S.-dev.git axis-is
cd axis-is/cloud-service
```

### Step 2: Configure Environment

```bash
# Copy example environment
cp .env.example .env

# Edit configuration
vi .env
```

**Required settings:**
```env
ANTHROPIC_API_KEY=sk-ant-api-YOUR_ACTUAL_KEY_HERE
MQTT_BROKER=0.0.0.0  # Listen on all interfaces
DATABASE_URL=postgresql://postgres:YOUR_SECURE_PASSWORD@postgres:5432/axis_is
```

**Optional tuning:**
```env
# Analysis triggers
MOTION_THRESHOLD=0.7
VEHICLE_CONFIDENCE_THRESHOLD=0.5
FRAME_REQUEST_COOLDOWN=60

# Performance
MAX_CONCURRENT_ANALYSES=5
SCENE_MEMORY_FRAMES=30
```

### Step 3: Deploy Services

```bash
# Start all services
docker-compose up -d

# Verify deployment
docker-compose ps

# Check logs
docker-compose logs -f
```

Expected output:
```
axis-is-mosquitto       Up (healthy)
axis-is-postgres        Up (healthy)
axis-is-redis           Up (healthy)
axis-is-cloud-service   Up (healthy)
```

### Step 4: Verify Cloud Service

```bash
# Health check
curl http://localhost:8000/health

# View stats
curl http://localhost:8000/stats | jq

# Check configuration
curl http://localhost:8000/config | jq
```

### Step 5: Configure Firewall

```bash
# Allow MQTT from cameras
sudo ufw allow 1883/tcp comment 'MQTT for Axis cameras'

# Allow API access (optional, for dashboard)
sudo ufw allow 8000/tcp comment 'Axis I.S. API'

# Enable firewall
sudo ufw enable
```

---

## Part 2: Camera-Side Deployment

### Prerequisites
- AXIS camera with ARTPEC-8/9 SoC
- AXIS OS 12.x
- ACAP Native SDK 1.2+ (for building)
- Network connectivity to cloud service
- YOLOv5n INT8 model (1MB)

### Step 1: Build Camera Application

```bash
# On development machine with ACAP SDK
cd poc/camera

# Build for ARM64
export ARCH=aarch64
make clean && make

# Verify binary
ls -lh axis_is_poc
# Expected: ~300-400KB
```

### Step 2: Package as ACAP

```bash
# Create .eap package
acap-build .

# Verify package
ls -lh *.eap
# Expected: axis_is_poc_2.0.0_aarch64.eap
```

### Step 3: Upload to Camera

```bash
# Upload via SCP
scp axis_is_poc_2.0.0_aarch64.eap root@CAMERA_IP:/tmp/

# Or use web interface:
# Navigate to: http://CAMERA_IP/index.html#system/apps
# Click "Upload" and select .eap file
```

### Step 4: Configure Camera Application

SSH to camera and edit configuration:

```bash
ssh root@CAMERA_IP

# Navigate to application directory
cd /usr/local/packages/axis_is_poc

# Configure core settings
vi app/settings/core.json
```

**core.json:**
```json
{
  "camera_id": "axis-camera-001",
  "target_fps": 10,
  "mqtt_broker": "YOUR_CLOUD_SERVER_IP",
  "mqtt_port": 1883
}
```

**detection.json:** (usually no changes needed)
```json
{
  "enabled": true,
  "confidence_threshold": 0.25,
  "model_path": "/usr/local/packages/axis_is_poc/models/yolov5n_int8.tflite"
}
```

**frame_publisher.json:**
```json
{
  "enabled": true,
  "camera_id": "axis-camera-001",
  "jpeg_quality": 85,
  "rate_limit_seconds": 60
}
```

### Step 5: Upload YOLOv5n Model

```bash
# On camera
mkdir -p /usr/local/packages/axis_is_poc/models

# Upload model (from development machine)
scp yolov5n_int8.tflite root@CAMERA_IP:/usr/local/packages/axis_is_poc/models/
```

### Step 6: Start Application

**Via Web Interface:**
1. Navigate to http://CAMERA_IP/index.html#system/apps
2. Find "axis_is_poc"
3. Click "Start"

**Via SSH:**
```bash
/usr/local/packages/axis_is_poc/axis_is_poc &
```

### Step 7: Verify Camera Operation

```bash
# Check logs
tail -f /var/log/syslog | grep axis_is_poc

# Expected output:
# [axis_is_poc] Starting Axis I.S. POC v2.0.0 (Modular)
# [axis_is_poc] Discovered and initialized 2 modules:
# [axis_is_poc]   [0] detection v1.0.0 (priority 10)
# [axis_is_poc]   [1] frame_publisher v1.0.0 (priority 40)
# [axis_is_poc] Starting main loop (target 10 FPS)

# Check HTTP endpoint
curl http://CAMERA_IP:8080/local/axis_is_poc/status | jq

# Check modules loaded
curl http://CAMERA_IP:8080/local/axis_is_poc/modules | jq
```

---

## Part 3: Integration Testing

### Test 1: Metadata Streaming

**On cloud server:**
```bash
# Subscribe to metadata stream
docker exec -it axis-is-mosquitto mosquitto_sub -t "axis-is/camera/+/metadata" -v

# Expected: JSON messages at 10 FPS
# axis-is/camera/axis-camera-001/metadata {"timestamp_us": 1700000000000000, ...}
```

### Test 2: Frame Request Flow

**Trigger conditions:**
1. Wave hand in front of camera (motion > 0.7)
2. Drive car into view (vehicle detection)

**Check cloud service logs:**
```bash
docker-compose logs -f cloud-service

# Expected:
# [mqtt_handler] Triggering frame request for axis-camera-001: high_motion_0.85
# [frame_publisher] Frame requested: id=uuid-1234 reason=high_motion
# [mqtt_handler] Received frame: axis-camera-001 (size: 12345 bytes)
# [claude_agent] Analyzing scene: axis-camera-001 with 5 frames
# [claude_agent] Analysis complete: axis-camera-001 in 2500ms
```

### Test 3: Claude Analysis

**Query recent analyses:**
```bash
curl http://CLOUD_IP:8000/cameras/axis-camera-001/analyses?limit=5 | jq

# Expected:
# {
#   "camera_id": "axis-camera-001",
#   "count": 5,
#   "analyses": [
#     {
#       "id": 1,
#       "summary": "A person is walking through the parking lot...",
#       "frames_analyzed": 5,
#       "created_at": "2025-11-23T10:15:30"
#     }
#   ]
# }
```

### Test 4: Scene Memory

```bash
# Check scene memory
curl http://CLOUD_IP:8000/cameras/axis-camera-001/scene-memory | jq

# Expected:
# {
#   "camera_id": "axis-camera-001",
#   "frames": [ ... 30 frames ... ],
#   "context": {
#     "frames_available": 30,
#     "frames_with_images": 5,
#     "average_motion_score": 0.45
#   }
# }
```

---

## Part 4: Multi-Camera Deployment

### Add Additional Cameras

For each camera, repeat Part 2 but with unique IDs:

**Camera 2:**
```json
{
  "camera_id": "axis-camera-002",
  "target_fps": 10,
  "mqtt_broker": "CLOUD_IP",
  "mqtt_port": 1883
}
```

**Camera 3:**
```json
{
  "camera_id": "axis-camera-003",
  ...
}
```

### Verify All Cameras

```bash
# List active cameras
curl http://CLOUD_IP:8000/cameras | jq

# Expected:
# {
#   "count": 3,
#   "cameras": [
#     {"camera_id": "axis-camera-001", "state": {...}},
#     {"camera_id": "axis-camera-002", "state": {...}},
#     {"camera_id": "axis-camera-003", "state": {...}}
#   ]
# }
```

---

## Part 5: Monitoring & Maintenance

### Real-Time Stats

```bash
# System-wide statistics
watch -n 5 'curl -s http://CLOUD_IP:8000/stats | jq'

# Per-camera stats
curl http://CLOUD_IP:8000/cameras | jq '.cameras[] | {camera_id, fps: .state.actual_fps}'
```

### Database Inspection

```bash
# Connect to PostgreSQL
docker exec -it axis-is-postgres psql -U postgres -d axis_is

# Recent analyses
SELECT camera_id, summary, created_at
FROM claude_analyses
ORDER BY created_at DESC
LIMIT 10;

# Event counts by camera
SELECT camera_id, COUNT(*) as event_count
FROM camera_events
WHERE timestamp_us > EXTRACT(EPOCH FROM NOW() - INTERVAL '1 hour') * 1000000
GROUP BY camera_id;

# Analysis performance
SELECT
  camera_id,
  AVG(analysis_duration_ms) as avg_duration_ms,
  AVG(frames_analyzed) as avg_frames
FROM claude_analyses
GROUP BY camera_id;
```

### Log Monitoring

```bash
# Cloud service logs
docker-compose logs -f cloud-service | grep -E "(ERROR|WARNING|Analysis)"

# Camera logs (on camera)
ssh root@CAMERA_IP 'tail -f /var/log/syslog | grep axis_is_poc'

# MQTT broker logs
docker-compose logs -f mosquitto
```

### Health Checks

```bash
# Automated health check script
cat > check_health.sh << 'EOF'
#!/bin/bash
echo "=== Axis I.S. Health Check ==="

# Cloud service
if curl -sf http://localhost:8000/health > /dev/null; then
  echo "✓ Cloud service healthy"
else
  echo "✗ Cloud service DOWN"
fi

# Database
if docker exec axis-is-postgres pg_isready -U postgres > /dev/null 2>&1; then
  echo "✓ PostgreSQL healthy"
else
  echo "✗ PostgreSQL DOWN"
fi

# Redis
if docker exec axis-is-redis redis-cli ping > /dev/null 2>&1; then
  echo "✓ Redis healthy"
else
  echo "✗ Redis DOWN"
fi

# MQTT
if docker exec axis-is-mosquitto mosquitto_sub -t '$SYS/broker/uptime' -C 1 -W 2 > /dev/null 2>&1; then
  echo "✓ Mosquitto healthy"
else
  echo "✗ Mosquitto DOWN"
fi
EOF

chmod +x check_health.sh
./check_health.sh
```

---

## Troubleshooting

### Camera Not Connecting

**Symptom:** No cameras shown in `GET /cameras`

**Checks:**
```bash
# 1. Verify MQTT connectivity from camera
ssh root@CAMERA_IP
ping CLOUD_IP

# 2. Test MQTT publish from camera
mosquitto_pub -h CLOUD_IP -p 1883 -t test -m "hello"

# 3. Check firewall on cloud
sudo ufw status | grep 1883

# 4. Check MQTT broker logs
docker-compose logs mosquitto | grep CONNECT
```

### No Frame Requests

**Symptom:** Metadata streaming but no Claude analyses

**Checks:**
```bash
# 1. Check trigger thresholds
curl http://CLOUD_IP:8000/config | jq '.motion_threshold'

# 2. Lower threshold for testing
# In .env: MOTION_THRESHOLD=0.3
docker-compose restart cloud-service

# 3. Manual frame request
curl -X POST http://CLOUD_IP:8000/cameras/axis-camera-001/request-frame?reason=test

# 4. Check cooldown
redis-cli -h localhost GET "camera:axis-camera-001:last_request"
```

### High Latency

**Symptom:** Analysis takes > 10 seconds

**Checks:**
```bash
# 1. Check Claude API latency
docker-compose logs cloud-service | grep "Analysis complete"

# 2. Check network latency
ping anthropic.com

# 3. Reduce frames analyzed
# In .env: SCENE_MEMORY_FRAMES=10
```

### Memory Leaks

**Symptom:** Memory usage growing over time

**Checks:**
```bash
# 1. Camera memory
ssh root@CAMERA_IP
top -p $(pgrep axis_is_poc)

# 2. Cloud service memory
docker stats axis-is-cloud-service

# 3. Redis memory
docker exec -it axis-is-redis redis-cli INFO memory

# 4. PostgreSQL connections
docker exec -it axis-is-postgres psql -U postgres -c "SELECT count(*) FROM pg_stat_activity;"
```

---

## Production Checklist

### Security
- [ ] Change default PostgreSQL password
- [ ] Enable MQTT authentication (username/password)
- [ ] Use TLS for MQTT (port 8883)
- [ ] Add API authentication (JWT tokens)
- [ ] Configure firewall rules
- [ ] Rotate Anthropic API keys regularly
- [ ] Enable audit logging

### Reliability
- [ ] Set up PostgreSQL backups (daily)
- [ ] Configure Redis persistence (AOF)
- [ ] Enable Docker restart policies
- [ ] Set up monitoring alerts (email/Slack)
- [ ] Test failover scenarios
- [ ] Document disaster recovery procedure

### Performance
- [ ] Tune PostgreSQL (shared_buffers, work_mem)
- [ ] Configure Redis maxmemory policy
- [ ] Enable MQTT persistent sessions
- [ ] Optimize analysis triggers
- [ ] Monitor Claude API costs
- [ ] Set up CDN for static assets (future UI)

### Monitoring
- [ ] Add Prometheus metrics
- [ ] Create Grafana dashboards
- [ ] Set up log aggregation (ELK/Loki)
- [ ] Configure uptime monitoring
- [ ] Track Claude API usage/costs
- [ ] Alert on analysis failures

---

## Cost Estimation

### Claude API Costs (Example)

**Assumptions:**
- 3 cameras
- 1 analysis per camera per hour (conservative)
- 5 frames per analysis
- 500 tokens per analysis (average)

**Monthly cost:**
```
3 cameras × 24 hours × 30 days = 2,160 analyses/month
2,160 × 500 tokens = 1,080,000 tokens/month
1,080,000 × $0.003/1K tokens = $3.24/month
```

**With higher activity:**
- 5 analyses/hour/camera: $16.20/month
- 10 analyses/hour/camera: $32.40/month

### Infrastructure Costs

- **Cloud server:** $10-40/month (2-4 GB RAM, 2 vCPU)
- **Storage:** Minimal (<10 GB)
- **Bandwidth:** 100-500 MB/day (metadata only)

**Total:** $15-75/month depending on scale

---

## Next Steps

1. ✅ Deploy cloud service
2. ✅ Deploy camera applications
3. ✅ Verify end-to-end integration
4. 🔲 Build UI dashboard (future)
5. 🔲 Add alert notifications
6. 🔲 Implement LPR module (future)
7. 🔲 Implement OCR module (future)

---

**Deployment complete!** Your edge-cloud AI surveillance platform is now running with Claude Vision integration.

For questions or issues, consult:
- Cloud Service README: `cloud-service/README.md`
- Camera Module README: `README.md`
- Architecture Design: `AXIS_IS_MODULAR_ARCHITECTURE_DESIGN.md`
