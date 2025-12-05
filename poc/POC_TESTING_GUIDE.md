# Axis I.S. POC - Testing Guide

**Version:** 1.0.0
**Last Updated:** November 23, 2025

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Initial Setup](#initial-setup)
3. [Build and Deploy](#build-and-deploy)
4. [Functional Testing](#functional-testing)
5. [Performance Testing](#performance-testing)
6. [Multi-Camera Testing](#multi-camera-testing)
7. [24-Hour Stability Test](#24-hour-stability-test)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Hardware

- Axis camera with ARTPEC-9 SoC (examples: P3245-LVE, Q1656-LE, M3067-P)
- Camera firmware ≥10.12.x
- Computer with Docker for building
- MQTT broker (or use included Mosquitto in docker-compose)
- Network connectivity between all components

### Software

- Docker Desktop (for building ACAP)
- Python 3.8+ (for cloud subscriber)
- SSH client (for camera access)
- curl or similar HTTP client
- Optional: jq for JSON formatting

### Network Setup

```
[Camera] ---> [MQTT Broker] ---> [Cloud Subscriber]
   |
   +---> [Your Computer] (for HTTP/SSH access)
```

All devices must be on same network or have routing between them.

---

## Initial Setup

### Step 1: Verify Camera Access

```bash
# Test network connectivity
ping 192.168.1.101

# Test SSH access
ssh root@192.168.1.101
# Default password is often "pass" or found on camera label

# Check camera info
ssh root@192.168.1.101 "cat /etc/axis-release"
```

Expected output:
```
PRODUCT=AXIS P3245-LVE
VERSION=10.12.123
```

### Step 2: Configure MQTT Broker

If using the included Mosquitto:

```bash
cd poc/cloud
docker-compose up -d mosquitto

# Verify it's running
docker-compose ps
docker-compose logs mosquitto
```

Test broker:
```bash
# Subscribe to test topic
mosquitto_sub -h localhost -t test &

# Publish test message
mosquitto_pub -h localhost -t test -m "Hello"
```

### Step 3: Update Configuration

Edit `poc/camera/app/settings/mqtt.json`:
```json
{
  "broker": "192.168.1.50",  # <-- Update to your broker IP
  "port": 1883,
  "clientId": "axis-is-camera-001"
}
```

Edit `poc/camera/app/settings/settings.json`:
```json
{
  "axis-is": {
    "camera_id": "axis-camera-001",  # <-- Unique per camera
    "target_fps": 10,
    "confidence_threshold": 0.25
  }
}
```

---

## Build and Deploy

### Step 1: Copy Template Files

```bash
cd poc/camera/app

# Copy MQTT framework files
cp ../../../make_acap/mqtt/app/ACAP.c ./
cp ../../../make_acap/mqtt/app/ACAP.h ./
cp ../../../make_acap/mqtt/app/MQTT.c ./
cp ../../../make_acap/mqtt/app/MQTT.h ./
cp ../../../make_acap/mqtt/app/CERTS.c ./
cp ../../../make_acap/mqtt/app/CERTS.h ./
cp ../../../make_acap/mqtt/app/cJSON.c ./
cp ../../../make_acap/mqtt/app/cJSON.h ./

cd ../..
```

### Step 2: Download Model

```bash
cd camera/models

# Option A: Download from Axis Model Zoo
wget https://github.com/AxisCommunications/axis-model-zoo/releases/download/v1.0/yolov5n_416_416_int8.tflite

# Option B: Use placeholder for testing (will fail inference)
# touch yolov5n_int8.tflite

cd ../..
```

### Step 3: Build ACAP

```bash
cd camera
./build.sh

# Expected output:
# Building Docker image...
# Extracting .eap file...
# Build Complete
# axis_is_poc_1.0.0_aarch64.eap
```

Verify:
```bash
ls -lh *.eap
# Should be 1-5 MB
```

### Step 4: Deploy to Camera

```bash
cd ..
./deploy.sh 192.168.1.101 root mypassword
```

Expected output:
```
====== Axis I.S. POC Deployment ======
Testing connectivity...
✓ Upload successful
✓ Application started
✓ Application is running
====== Deployment Complete ======
```

---

## Functional Testing

### Test 1: Verify Application Started

```bash
# Check via camera web interface
# Navigate to: http://192.168.1.101
# Go to: Apps → Installed apps
# Look for: "Axis I.S. POC" (should be "Started")

# Or via API
curl -u root:pass http://192.168.1.101/axis-cgi/applications/list.cgi
```

**Expected:** Application listed with status "Running"

### Test 2: Check HTTP Endpoints

```bash
# Status endpoint
curl -s -u root:pass http://192.168.1.101/local/axis_is_poc/status | jq

# Expected response:
{
  "app": "axis_is_poc",
  "version": "1.0.0",
  "camera_id": "axis-camera-001",
  "uptime_seconds": 123,
  "frames_processed": 456,
  "target_fps": 10,
  "actual_fps": 9.8,
  "vdo_active": true,
  "larod_active": true,
  "dlpu_active": true
}
```

```bash
# Metadata endpoint
curl -s -u root:pass http://192.168.1.101/local/axis_is_poc/metadata | jq

# Expected response:
{
  "version": "1.0",
  "msg_type": "metadata",
  "seq": 789,
  "timestamp": 1700000000000,
  "scene": {
    "hash": "a3f5c8d91e7b2f4a",
    "changed": false
  },
  "detections": [
    {
      "class_id": 0,
      "confidence": 0.85,
      "x": 0.5,
      "y": 0.5,
      "width": 0.2,
      "height": 0.4
    }
  ],
  "motion": {
    "score": 0.15,
    "detected": true
  }
}
```

### Test 3: Check Camera Logs

```bash
ssh root@192.168.1.101 "tail -n 50 /var/log/messages | grep axis_is_poc"
```

Look for:
- ✓ "Starting Axis I.S. POC v1.0.0"
- ✓ "VDO stream initialized: 416x416 @ 10 FPS"
- ✓ "Larod initialized"
- ✓ "MQTT: Connected successfully"
- ✓ "Frame 100: Inference=48ms Total=125ms..."

Look for errors:
- ❌ "Failed to initialize..."
- ❌ "Inference failed..."
- ❌ "MQTT: Disconnected..."

### Test 4: Verify MQTT Messages

Start cloud subscriber:
```bash
cd poc/cloud
python mqtt_subscriber.py --broker localhost
```

Expected output (within 10 seconds):
```
Axis I.S. POC Cloud Subscriber v1.0
Connecting to MQTT broker at localhost:1883
✓ Subscribed to Axis I.S. topics
Waiting for messages...

📡 [axis-camera-001] ONLINE at 12:34:56 (v1.0.0)
[axis-camera-001] Msg=  10 FPS= 9.8 Inf= 48ms Avg= 47.2ms Obj= 2 Mot=0.35 Gaps= 0
[axis-camera-001] Msg=  20 FPS= 9.9 Inf= 45ms Avg= 46.5ms Obj= 1 Mot=0.22 Gaps= 0
```

**Success Criteria:**
- Messages arriving
- FPS close to target (9-11 FPS)
- Inference time reasonable (<100ms)
- No sequence gaps

---

## Performance Testing

### Test 5: Measure Inference FPS

Monitor for 1 minute and record:

```bash
# In one terminal: watch camera logs
ssh root@192.168.1.101 "tail -f /var/log/messages | grep 'Frame.*FPS'"

# In another terminal: watch subscriber
cd poc/cloud
python mqtt_subscriber.py --broker localhost
```

After 1 minute:
- Record average FPS from subscriber
- Record average inference time
- Check for any errors or warnings

**Target:** FPS ≥ 5, preferably ≥ 10

### Test 6: Measure Total Latency

Latency = time from frame capture to MQTT publish

```bash
# Enable debug logging (if implemented)
# Or calculate from logs:
ssh root@192.168.1.101 "tail -f /var/log/messages | grep 'Total='"
```

Look for "Total=XYZms" in logs.

**Target:** <200ms total latency

### Test 7: Measure Memory Usage

```bash
# Initial memory
ssh root@192.168.1.101 "top -b -n 1 | grep axis_is_poc"

# Wait 1 hour, check again
sleep 3600
ssh root@192.168.1.101 "top -b -n 1 | grep axis_is_poc"

# Compare VSZ and RSS columns
```

**Target:** <300 MB, no increase over time

### Test 8: Object Detection Accuracy

Create test scenarios:

**Scenario A: Empty Scene**
- Point camera at empty room
- Verify: Object count = 0

**Scenario B: Single Person**
- Have person stand in view
- Verify: Object count = 1, class_id = 0 (person), confidence > 0.25

**Scenario C: Multiple Objects**
- Person + car (or person + chair)
- Verify: Object count = 2, both detected correctly

**Scenario D: Crowded Scene**
- Multiple people
- Verify: All detected (within reason)

Record results in POC_SUCCESS_CRITERIA.md

### Test 9: Motion Detection

**Test Static Scene:**
```bash
# Point camera at static scene
# Monitor motion scores (should be low, <0.1)
curl -s -u root:pass http://192.168.1.101/local/axis_is_poc/metadata | jq '.motion.score'
```

**Test Movement:**
```bash
# Have person walk through frame
# Monitor motion scores (should increase, >0.3)
```

---

## Multi-Camera Testing

### Setup 2-3 Cameras

For each additional camera:

1. Update `camera_index` in dlpu_basic.c:
   ```c
   // Camera 1: camera_index = 0
   // Camera 2: camera_index = 1
   // Camera 3: camera_index = 2
   ```

2. Update camera_id in settings.json:
   ```json
   {
     "axis-is": {
       "camera_id": "axis-camera-002"  // Unique!
     }
   }
   ```

3. Rebuild and deploy to each camera

### Test 10: Multi-Camera Coordination

Start subscriber monitoring all cameras:
```bash
python mqtt_subscriber.py --broker localhost
```

Expected output:
```
[axis-camera-001] Msg=  10 FPS= 9.8 ...
[axis-camera-002] Msg=  10 FPS= 9.7 ...
[axis-camera-003] Msg=  10 FPS= 9.9 ...
```

**Success Criteria:**
- All cameras maintain ≥5 FPS
- No DLPU errors in logs
- Inference times don't spike
- DLPU wait times <50ms

Check logs on each camera for DLPU coordination:
```bash
ssh root@192.168.1.101 "grep 'DLPU' /var/log/messages | tail -20"
```

---

## 24-Hour Stability Test

### Procedure

1. Deploy POC to camera
2. Start cloud subscriber
3. Let run for 24 hours uninterrupted
4. Monitor periodically

### Monitoring Script

```bash
#!/bin/bash
# monitor.sh - Run on your computer

CAMERA_IP="192.168.1.101"
LOG_FILE="stability_test.log"

while true; do
  TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

  # Get memory usage
  MEM=$(ssh root@$CAMERA_IP "top -b -n 1 | grep axis_is_poc | awk '{print \$6}'")

  # Get uptime
  UPTIME=$(curl -s -u root:pass http://$CAMERA_IP/local/axis_is_poc/status | jq -r '.uptime_seconds')

  # Get FPS
  FPS=$(curl -s -u root:pass http://$CAMERA_IP/local/axis_is_poc/status | jq -r '.actual_fps')

  echo "$TIMESTAMP | Uptime: ${UPTIME}s | FPS: $FPS | Mem: $MEM" | tee -a $LOG_FILE

  sleep 300  # Check every 5 minutes
done
```

Run:
```bash
chmod +x monitor.sh
./monitor.sh
```

### Success Criteria

After 24 hours:
- [ ] Application still running (uptime = 86400s)
- [ ] No crashes or restarts
- [ ] Memory usage stable (<300 MB)
- [ ] FPS maintained (≥5 FPS)
- [ ] MQTT messages continuous (no gaps)
- [ ] No error accumulation in logs

---

## Troubleshooting

### Issue: Build Fails

**Symptom:** Docker build errors

**Solutions:**
1. Check Docker is running: `docker ps`
2. Verify Makefile includes all source files
3. Check for syntax errors in C code
4. Try clean build: `rm -rf build *.eap && ./build.sh`

### Issue: Deploy Fails

**Symptom:** Upload error or HTTP 401/403

**Solutions:**
1. Verify camera IP: `ping 192.168.1.101`
2. Check username/password
3. Verify camera has space: `ssh root@192.168.1.101 "df -h"`
4. Try manual upload via camera web interface

### Issue: App Won't Start

**Symptom:** App shows "Stopped" in web interface

**Solutions:**
1. Check logs: `ssh root@192.168.1.101 "tail -100 /var/log/messages | grep axis-is"`
2. Check manifest.json is valid JSON
3. Verify dependencies (larod, vdo) available on camera
4. Try manual start: `ssh root@192.168.1.101 "/usr/local/packages/axis_is_poc/axis_is_poc"`

### Issue: No MQTT Messages

**Symptom:** Subscriber receives nothing

**Solutions:**
1. Check broker running: `docker-compose ps`
2. Test broker: `mosquitto_pub -h localhost -t test -m hello`
3. Verify broker IP in mqtt.json
4. Check network: `ssh root@192.168.1.101 "ping <broker-ip>"`
5. Check MQTT logs on camera: `grep MQTT /var/log/messages`

### Issue: Low FPS

**Symptom:** FPS < 5

**Solutions:**
1. Verify YOLOv5n INT8 model (not FP32)
2. Check model loads: `grep "Larod initialized" /var/log/messages`
3. Review inference times (should be <100ms)
4. Check CPU usage: `top`
5. Verify single DLPU user (no other ACAPs)

### Issue: Sequence Gaps

**Symptom:** Subscriber shows "SEQUENCE GAP"

**Solutions:**
1. Check network quality (ping, packet loss)
2. Increase MQTT QoS (in MQTT.c)
3. Review camera CPU/memory usage
4. Check for frame drops in VDO logs
5. Reduce frame rate if needed

### Issue: Memory Leak

**Symptom:** Memory increases over time

**Solutions:**
1. Check all vdo_buffer_unref() calls
2. Verify Larod tensor cleanup
3. Check JSON cleanup (cJSON_Delete)
4. Review malloc/free pairs
5. Use valgrind (if available)

---

## Getting Help

If issues persist:

1. **Collect Diagnostics:**
   ```bash
   # Save logs
   ssh root@192.168.1.101 "cat /var/log/messages | grep axis_is_poc" > camera.log

   # Save status
   curl -s -u root:pass http://192.168.1.101/local/axis_is_poc/status > status.json

   # Save MQTT samples
   python mqtt_subscriber.py --broker localhost > mqtt_samples.txt
   # (Ctrl+C after 10 seconds)
   ```

2. **Review Documentation:**
   - AXIS_REPOS_ANALYSIS.md (code patterns)
   - Axis I.S._PLANNING_SUMMARY.md (architecture)
   - POC_SUCCESS_CRITERIA.md (expected behavior)

3. **Check Common Issues:**
   - Model file exists and is readable
   - MQTT broker accessible from camera
   - Camera has correct firmware version
   - No conflicting ACAPs running

---

**Testing Guide Version:** 1.0.0
**Last Updated:** November 23, 2025
