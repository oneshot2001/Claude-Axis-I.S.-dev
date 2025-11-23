# AXION POC - Success Criteria Checklist

**Test Date:** _____________
**Tester:** _____________
**Camera Model:** _____________
**Firmware Version:** _____________

---

## Build and Deploy

- [ ] ACAP builds without errors using `./build.sh`
- [ ] `.eap` file created successfully (~1-5 MB)
- [ ] Deploys to camera using `./deploy.sh`
- [ ] Application starts without crashes
- [ ] Application visible in camera web interface
- [ ] HTTP `/status` endpoint responds
- [ ] HTTP `/metadata` endpoint responds

**Notes:**
```

```

---

## VDO Streaming

- [ ] VDO stream initializes (check syslog)
- [ ] Frames captured at 416x416 resolution
- [ ] No VDO errors in logs
- [ ] Frame rate stable (no excessive drops)
- [ ] Zero-copy buffer handling (no memcpy warnings)

**Measured FPS:** _____ (check logs or status endpoint)

**Notes:**
```

```

---

## Larod Inference

- [ ] Model loads successfully (check syslog)
- [ ] Model file exists at correct path
- [ ] Inference executes without errors
- [ ] Output tensors parsed correctly
- [ ] Detections appear in MQTT messages
- [ ] Inference time ≥5 FPS (≤200ms per frame)
- [ ] Inference time target: <100ms per frame

**Measured Inference Time:** _____ ms (average)
**Measured FPS:** _____

**Notes:**
```

```

---

## Metadata Extraction

- [ ] Scene hash generated for each frame
- [ ] Scene change detection works
- [ ] Motion score calculated
- [ ] Object count matches detections
- [ ] Timestamp accurate (±1 second)
- [ ] Sequence numbers increment correctly

**Notes:**
```

```

---

## MQTT Publishing

- [ ] MQTT connection established
- [ ] Messages published successfully
- [ ] Cloud subscriber receives messages
- [ ] Message rate = target FPS (10/sec)
- [ ] No sequence gaps (dropped frames)
- [ ] JSON format valid
- [ ] All required fields present

**Measured Message Rate:** _____ msg/sec

**Sample Message:**
```json


```

**Notes:**
```

```

---

## Performance Tests

### Single Camera

- [ ] **Inference ≥5 FPS:** _____ FPS measured
- [ ] **Latency <200ms:** _____ ms measured
- [ ] **Memory <300MB after 1 hour:** _____ MB measured
- [ ] **Memory <300MB after 24 hours:** _____ MB measured
- [ ] **MQTT rate = target FPS:** _____ msg/sec
- [ ] **No crashes during 24-hour test**
- [ ] **No memory leaks detected**

**Performance Summary:**
```
Start Time: _____
End Time: _____
Total Frames: _____
Errors/Crashes: _____
```

### Multi-Camera Tests (Optional)

- [ ] 2 cameras run simultaneously
- [ ] 3 cameras run simultaneously
- [ ] No DLPU blocking detected
- [ ] All cameras maintain ≥5 FPS
- [ ] DLPU wait time <50ms

**Camera 1 FPS:** _____
**Camera 2 FPS:** _____
**Camera 3 FPS:** _____
**DLPU Avg Wait:** _____ ms

**Notes:**
```

```

---

## Functional Tests

### Object Detection

- [ ] Person detected correctly
- [ ] Car detected correctly
- [ ] Multiple objects detected simultaneously
- [ ] Confidence scores reasonable (>0.25)
- [ ] Bounding boxes within frame (0-1 normalized)

**Test Scenarios:**
- [ ] Empty scene (no objects)
- [ ] Single person
- [ ] Multiple people
- [ ] Person + vehicle
- [ ] Crowded scene (>10 objects)

**Notes:**
```

```

### Motion Detection

- [ ] Static scene = low motion score
- [ ] Moving person = high motion score
- [ ] Scene change = score increase
- [ ] Motion threshold appropriate

**Notes:**
```

```

### Scene Hash

- [ ] Stable scene = same hash
- [ ] Scene change = different hash
- [ ] Hash format consistent (16 hex chars)

**Notes:**
```

```

---

## DLPU Coordination (Multi-Camera)

- [ ] Cameras don't run inference simultaneously
- [ ] Time-slot allocation working
- [ ] No DLPU access errors
- [ ] Wait times reasonable (<50ms)
- [ ] All cameras get fair access

**Notes:**
```

```

---

## Error Handling

- [ ] Graceful shutdown on SIGTERM
- [ ] Resources cleaned up properly
- [ ] Handles network disconnects
- [ ] Recovers from MQTT broker restart
- [ ] Handles invalid model gracefully
- [ ] No zombie processes

**Notes:**
```

```

---

## Code Quality

- [ ] No compiler warnings
- [ ] No memory leaks (valgrind clean)
- [ ] Proper error logging
- [ ] All buffers unreferenced
- [ ] No segfaults during stress test

**Notes:**
```

```

---

## Overall Success

### Critical Requirements (Must Pass)

- [ ] ACAP builds and deploys
- [ ] VDO captures frames
- [ ] Larod runs inference
- [ ] MQTT publishes messages
- [ ] Cloud receives messages
- [ ] Performance ≥5 FPS

### Target Requirements (Should Pass)

- [ ] Performance ≥10 FPS
- [ ] Latency <100ms
- [ ] No sequence gaps
- [ ] 24-hour stability
- [ ] Multi-camera support

### Stretch Goals (Nice to Have)

- [ ] Performance >15 FPS
- [ ] Latency <50ms
- [ ] Zero downtime in 24h test
- [ ] 5+ cameras simultaneously

---

## Final Assessment

**Overall Status:** [ ] PASS  [ ] FAIL  [ ] PARTIAL

**Key Achievements:**
```


```

**Known Issues:**
```


```

**Recommendations:**
```


```

---

## Sign-Off

**Tested By:** _____________________
**Date:** _____________________
**Signature:** _____________________

**Reviewed By:** _____________________
**Date:** _____________________
**Signature:** _____________________

---

**POC Version:** 1.0.0
**Test Completion:** _____ of _____ criteria passed
