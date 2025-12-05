# Axis I.S. Platform - Comprehensive Test Plan
**Version:** 1.0
**Date:** November 23, 2025
**Author:** QA Architect - Distributed Systems & Edge Computing
**Target Platform:** Axis ARTPEC-9 Cameras (aarch64)

---

## Table of Contents
1. [Executive Summary](#executive-summary)
2. [Test Strategy & Objectives](#test-strategy--objectives)
3. [Unit Testing Strategy](#1-unit-testing-strategy)
4. [Integration Testing](#2-integration-testing)
5. [System Testing](#3-system-testing)
6. [Performance Testing](#4-performance-testing)
7. [Stress Testing](#5-stress-testing)
8. [Reliability Testing](#6-reliability-testing-critical)
9. [Security Testing](#7-security-testing)
10. [Chaos Engineering](#8-chaos-engineering)
11. [Test Environments](#9-test-environments)
12. [Test Automation](#10-test-automation)
13. [Acceptance Criteria](#11-acceptance-criteria)
14. [Test Schedule](#12-test-schedule)
15. [Risk-Based Testing Matrix](#risk-based-testing-matrix)

---

## Executive Summary

The Axis I.S. platform is a distributed edge computing system combining real-time video analytics on Axis cameras with cloud-based AI processing via Claude. This test plan addresses the unique challenges of:

- **Edge Resource Constraints** - Limited DLPU access, 700MB memory ceiling, shared compute
- **Network Reliability** - MQTT message delivery, retry logic, bandwidth adaptation
- **Time Synchronization** - Multi-camera correlation with clock drift tolerance
- **Memory Safety** - VDO buffer management, reference counting, 72-hour leak testing
- **Concurrent Access** - DLPU scheduler race conditions, deadlock prevention

**Test Coverage Goals:**
- Unit Tests: 85% code coverage on camera-side C code, 90% on cloud-side Python
- Integration Tests: 100% of inter-component communication paths
- System Tests: 100% of critical user scenarios
- Performance Tests: All acceptance criteria benchmarks verified
- Reliability: 72-hour continuous operation with <1% failure rate

---

## Test Strategy & Objectives

### Primary Objectives
1. **Functional Correctness** - All components work as specified
2. **Resource Safety** - No memory leaks, no resource exhaustion after 72 hours
3. **Fault Tolerance** - System recovers from network failures, camera reboots, service crashes
4. **Performance** - Meets all SLA targets (5-10 FPS inference, <100ms DLPU wait, 99.5% delivery)
5. **Security** - No credential leaks, proper authentication, encrypted communication

### Testing Approach
- **Risk-Based Prioritization** - Test critical gotchas first (DLPU contention, memory leaks, MQTT delivery)
- **Shift-Left Testing** - Extensive unit tests before integration
- **Continuous Validation** - Automated regression suite in CI/CD pipeline
- **Real Hardware Testing** - Final validation on actual Axis ARTPEC-9 cameras
- **Chaos Engineering** - Inject failures to validate resilience

---

## 1. Unit Testing Strategy

### 1.1 Camera-Side (C/ACAP) Unit Tests

#### 1.1.1 VDO Stream Handling

**Test Case UT-VDO-001: VDO Buffer Allocation and Deallocation**
- **Objective:** Verify no memory leaks in VDO buffer lifecycle
- **Test Data:** 1000 consecutive frame captures
- **Procedure:**
  ```c
  for (int i = 0; i < 1000; i++) {
      VdoBuffer* buffer;
      vdo_stream_get_buffer(stream, &buffer, 100);
      VdoFrame* frame = vdo_buffer_get_frame(buffer);
      void* data = vdo_frame_get_data(frame);
      // Process data
      vdo_frame_unref(frame);
      vdo_buffer_unref(buffer);
  }
  // Verify memory usage returns to baseline
  ```
- **Expected Result:** Memory usage stable, no leaks detected by Valgrind
- **Pass/Fail:** PASS if `valgrind --leak-check=full` shows 0 bytes lost
- **Tools:** Valgrind, GDB
- **Effort:** 4 hours

**Test Case UT-VDO-002: Frame Timestamp Accuracy**
- **Objective:** Verify VDO frame timestamps are monotonically increasing and accurate
- **Test Data:** 100 frames captured at 10 FPS
- **Procedure:**
  ```c
  uint64_t prev_ts = 0;
  for (int i = 0; i < 100; i++) {
      VdoBuffer* buffer;
      vdo_stream_get_buffer(stream, &buffer, 100);
      VdoFrame* frame = vdo_buffer_get_frame(buffer);
      uint64_t ts = vdo_frame_get_timestamp(frame);
      assert(ts > prev_ts);  // Monotonic
      assert((ts - prev_ts) < 150000);  // ~100ms @ 10fps (allow jitter)
      prev_ts = ts;
      vdo_frame_unref(frame);
      vdo_buffer_unref(buffer);
  }
  ```
- **Expected Result:** All timestamps monotonically increasing, avg delta ~100ms
- **Pass/Fail:** PASS if 0% timestamp reversals, <10% jitter
- **Tools:** Custom test harness, clock_gettime()
- **Effort:** 3 hours

**Test Case UT-VDO-003: YUV to RGB Format Conversion**
- **Objective:** Verify color format conversion correctness
- **Test Data:** Known test pattern image (color bars)
- **Procedure:**
  1. Load test pattern in YUV420 format
  2. Convert to RGB using VDO/OpenCV conversion
  3. Compare RGB values to known reference
- **Expected Result:** RGB values match reference within 2% tolerance
- **Pass/Fail:** PASS if PSNR > 40dB between converted and reference
- **Tools:** OpenCV, custom test harness
- **Effort:** 6 hours

**Test Case UT-VDO-004: Stream Disconnection Error Handling**
- **Objective:** Verify graceful handling of VDO stream errors
- **Test Data:** Simulated stream failures
- **Procedure:**
  ```c
  // Simulate VDO stream timeout
  VdoBuffer* buffer;
  int result = vdo_stream_get_buffer(stream, &buffer, 100);
  if (result < 0) {
      // Verify error logged
      // Verify cleanup performed
      // Verify retry attempted
  }
  ```
- **Expected Result:** Error logged, resources cleaned, retry attempted
- **Pass/Fail:** PASS if no resource leaks on error path
- **Tools:** Fault injection framework, Valgrind
- **Effort:** 5 hours

---

#### 1.1.2 DLPU Scheduler

**Test Case UT-DLPU-001: Time Slot Allocation Algorithm**
- **Objective:** Verify correct time slot allocation for N cameras
- **Test Data:** 5 cameras, 5 FPS target each
- **Procedure:**
  ```c
  // Calculate expected slot offsets
  // Camera 0: 0ms, 200ms, 400ms, 600ms, 800ms
  // Camera 1: 40ms, 240ms, 440ms, 640ms, 840ms
  // Camera 2: 80ms, 280ms, 480ms, 680ms, 880ms
  // etc.

  for (int cam = 0; cam < 5; cam++) {
      int offset = dlpu_scheduler_get_slot_offset(cam, 5);
      assert(offset == (cam * 40));
  }
  ```
- **Expected Result:** Slots evenly distributed, no overlaps
- **Pass/Fail:** PASS if all slots unique and evenly spaced
- **Tools:** Unity test framework, custom DLPU mock
- **Effort:** 8 hours

**Test Case UT-DLPU-002: Priority Handling**
- **Objective:** Verify high-priority cameras get preferential DLPU access
- **Test Data:** 3 cameras with priorities 1, 2, 3
- **Procedure:**
  1. Submit inference requests from all 3 cameras simultaneously
  2. Verify processing order matches priority
  3. Measure wait times per priority level
- **Expected Result:** Priority 3 processed first, priority 1 last
- **Pass/Fail:** PASS if 100% of requests processed in priority order
- **Tools:** Custom scheduler test harness
- **Effort:** 6 hours

**Test Case UT-DLPU-003: Concurrent Access Race Conditions**
- **Objective:** Verify scheduler handles concurrent lock acquisition safely
- **Test Data:** 10 threads attempting simultaneous lock acquisition
- **Procedure:**
  ```c
  // Thread-safe test
  pthread_t threads[10];
  for (int i = 0; i < 10; i++) {
      pthread_create(&threads[i], NULL, acquire_dlpu_lock, &i);
  }
  // Verify only ONE thread acquires lock at a time
  // Verify all threads eventually succeed
  ```
- **Expected Result:** No race conditions, all threads eventually acquire lock
- **Pass/Fail:** PASS if ThreadSanitizer reports 0 data races
- **Tools:** ThreadSanitizer, Helgrind, stress testing
- **Effort:** 10 hours

**Test Case UT-DLPU-004: Crash Recovery**
- **Objective:** Verify scheduler recovers from process crash mid-inference
- **Test Data:** Simulated SIGKILL during DLPU lock hold
- **Procedure:**
  1. Process A acquires DLPU lock
  2. Send SIGKILL to process A
  3. Verify scheduler detects stale lock (timeout)
  4. Verify lock released and available to other processes
- **Expected Result:** Lock released within timeout period (5 seconds)
- **Pass/Fail:** PASS if lock available within 5s of process death
- **Tools:** Fault injection, shared memory inspection
- **Effort:** 8 hours

**Test Case UT-DLPU-005: Deadlock Prevention**
- **Objective:** Verify no deadlocks in multi-resource acquisition
- **Test Data:** Multiple cameras requesting DLPU + network resources
- **Procedure:**
  1. Implement timeout-based lock acquisition
  2. Simulate resource contention scenarios
  3. Verify all processes make forward progress
- **Expected Result:** No deadlocks, all processes complete within timeout
- **Pass/Fail:** PASS if no process waits >10 seconds
- **Tools:** Deadlock detector, timeout monitoring
- **Effort:** 8 hours

---

#### 1.1.3 Larod Inference

**Test Case UT-LAROD-001: Model Loading**
- **Objective:** Verify TFLite model loads correctly into Larod
- **Test Data:** YOLOv5n INT8 model (1.0 MB)
- **Procedure:**
  ```c
  larodConnection* conn = larodConnect(NULL, &error);
  larodModel* model = larodLoadModel(
      conn,
      "/usr/local/packages/axis-is/models/yolov5n_int8.tflite",
      LAROD_ACCESS_PRIVATE,
      "tflite",
      LAROD_CHIP_DLPU,
      &error
  );
  assert(model != NULL);
  assert(error == NULL);
  ```
- **Expected Result:** Model loads successfully, no errors
- **Pass/Fail:** PASS if model != NULL and error == NULL
- **Tools:** Larod API, custom test harness
- **Effort:** 3 hours

**Test Case UT-LAROD-002: Tensor Allocation**
- **Objective:** Verify input/output tensor allocation and deallocation
- **Test Data:** YOLOv5n model (input: 416x416x3, output: 1x25200x85)
- **Procedure:**
  ```c
  larodTensor** inputs = larodCreateModelInputs(model, &num_inputs, &error);
  larodTensor** outputs = larodCreateModelOutputs(model, &num_outputs, &error);

  assert(num_inputs == 1);
  assert(num_outputs == 1);

  // Cleanup
  larodDestroyTensors(inputs, num_inputs);
  larodDestroyTensors(outputs, num_outputs);
  ```
- **Expected Result:** Tensors allocated with correct dimensions
- **Pass/Fail:** PASS if tensor dimensions match model spec
- **Tools:** Larod API, Valgrind
- **Effort:** 4 hours

**Test Case UT-LAROD-003: Inference Correctness**
- **Objective:** Verify inference produces expected outputs for known inputs
- **Test Data:** Test image with known objects (e.g., 3 persons, 1 car)
- **Procedure:**
  1. Load test image into input tensor
  2. Run inference
  3. Parse output tensor
  4. Verify detected objects match expected
- **Expected Result:** Detects 3 persons and 1 car with >60% confidence
- **Pass/Fail:** PASS if all expected objects detected
- **Tools:** YOLOv5 post-processing, test image dataset
- **Effort:** 8 hours

**Test Case UT-LAROD-004: Model File Missing Error Handling**
- **Objective:** Verify graceful handling of missing model file
- **Test Data:** Invalid model path
- **Procedure:**
  ```c
  larodModel* model = larodLoadModel(
      conn,
      "/invalid/path/model.tflite",
      LAROD_ACCESS_PRIVATE,
      "tflite",
      LAROD_CHIP_DLPU,
      &error
  );
  assert(model == NULL);
  assert(error != NULL);
  // Verify error logged
  larodClearError(&error);
  ```
- **Expected Result:** Error returned, logged, and cleared
- **Pass/Fail:** PASS if error != NULL and logged
- **Tools:** Syslog inspection
- **Effort:** 2 hours

**Test Case UT-LAROD-005: DLPU Busy Error Handling**
- **Objective:** Verify retry logic when DLPU is busy
- **Test Data:** DLPU locked by another process
- **Procedure:**
  1. Process A acquires DLPU lock
  2. Process B attempts inference
  3. Verify Process B waits/retries
  4. Process A releases lock
  5. Verify Process B succeeds
- **Expected Result:** Process B waits then succeeds
- **Pass/Fail:** PASS if Process B succeeds within timeout
- **Tools:** Multi-process test framework
- **Effort:** 6 hours

---

#### 1.1.4 MQTT Publisher

**Test Case UT-MQTT-001: Message Serialization**
- **Objective:** Verify metadata correctly serialized to JSON
- **Test Data:** Sample metadata object
- **Procedure:**
  ```c
  cJSON* metadata = cJSON_CreateObject();
  cJSON_AddNumberToObject(metadata, "sequence", 12345);
  cJSON_AddStringToObject(metadata, "camera_id", "CAM-001");
  cJSON_AddNumberToObject(metadata, "timestamp", 1700000000.123);

  char* json = cJSON_PrintUnformatted(metadata);
  // Verify JSON structure
  assert(strstr(json, "\"sequence\":12345") != NULL);
  ```
- **Expected Result:** JSON structure matches expected format
- **Pass/Fail:** PASS if JSON parseable and contains all fields
- **Tools:** cJSON library, JSON validator
- **Effort:** 3 hours

**Test Case UT-MQTT-002: Retry Logic**
- **Objective:** Verify exponential backoff retry on publish failure
- **Test Data:** Simulated MQTT broker unavailable
- **Procedure:**
  ```c
  // Mock MQTT_Publish to fail
  int attempts = 0;
  for (int i = 0; i < 3; i++) {
      if (mqtt_publish_with_retry(topic, payload)) break;
      attempts++;
      // Verify backoff: 1s, 2s, 4s
  }
  assert(attempts == 3);
  ```
- **Expected Result:** 3 retry attempts with exponential backoff
- **Pass/Fail:** PASS if retry delays match 1s, 2s, 4s pattern
- **Tools:** Mock MQTT client, timer instrumentation
- **Effort:** 5 hours

**Test Case UT-MQTT-003: Queue Management**
- **Objective:** Verify message queue handles overflow correctly
- **Test Data:** 1100 messages (queue size = 1000)
- **Procedure:**
  1. Disconnect MQTT broker
  2. Publish 1100 messages
  3. Verify oldest 100 messages dropped (FIFO)
  4. Verify 1000 messages in queue
  5. Reconnect broker
  6. Verify 1000 messages delivered in order
- **Expected Result:** Oldest messages dropped, queue maintained at 1000
- **Pass/Fail:** PASS if exactly 1000 messages delivered in correct order
- **Tools:** Custom queue implementation, MQTT mock
- **Effort:** 6 hours

**Test Case UT-MQTT-004: Sequence Numbering**
- **Objective:** Verify sequence numbers monotonically increase
- **Test Data:** 1000 consecutive messages
- **Procedure:**
  ```c
  int seq = 0;
  for (int i = 0; i < 1000; i++) {
      cJSON* msg = create_metadata_message();
      int msg_seq = cJSON_GetObjectItem(msg, "sequence")->valueint;
      assert(msg_seq == seq + 1);
      seq = msg_seq;
  }
  ```
- **Expected Result:** Sequence numbers 1-1000, no gaps or reversals
- **Pass/Fail:** PASS if 100% sequential, no duplicates
- **Tools:** Custom test harness
- **Effort:** 3 hours

---

### 1.2 Cloud-Side (Python) Unit Tests

#### 1.2.1 Bandwidth Controller

**Test Case UT-BW-001: Network Quality Calculation**
- **Objective:** Verify network quality score calculation from metrics
- **Test Data:** Various network conditions (latency, loss, jitter)
- **Procedure:**
  ```python
  # Excellent: latency=20ms, loss=0%, jitter=5ms
  score = calculate_network_quality(latency=20, loss=0, jitter=5)
  assert score >= 90

  # Poor: latency=500ms, loss=10%, jitter=100ms
  score = calculate_network_quality(latency=500, loss=10, jitter=100)
  assert score <= 30
  ```
- **Expected Result:** Score ranges 0-100, correctly reflects network state
- **Pass/Fail:** PASS if score correlation with quality >95%
- **Tools:** pytest, mock network metrics
- **Effort:** 4 hours

**Test Case UT-BW-002: Quality Adjustment Algorithm**
- **Objective:** Verify quality level adjusted based on network score
- **Test Data:** Network score transitions 90→50→20
- **Procedure:**
  ```python
  controller = BandwidthController()

  # High quality for good network
  controller.update_network_score(90)
  assert controller.get_quality_level() == QualityLevel.HIGH

  # Medium for degraded network
  controller.update_network_score(50)
  assert controller.get_quality_level() == QualityLevel.MEDIUM

  # Minimal for poor network
  controller.update_network_score(20)
  assert controller.get_quality_level() == QualityLevel.MINIMAL
  ```
- **Expected Result:** Quality level matches network score thresholds
- **Pass/Fail:** PASS if transitions occur at correct thresholds
- **Tools:** pytest, unittest.mock
- **Effort:** 5 hours

**Test Case UT-BW-003: Priority-Based Bandwidth Allocation**
- **Objective:** Verify high-priority cameras get more bandwidth
- **Test Data:** 5 cameras with varying priorities
- **Procedure:**
  ```python
  cameras = [
      {"id": "CAM-1", "priority": 3},
      {"id": "CAM-2", "priority": 1},
      {"id": "CAM-3", "priority": 2},
  ]

  allocations = controller.allocate_bandwidth(cameras, total_bw=1000)

  # Verify CAM-1 gets most bandwidth
  assert allocations["CAM-1"] > allocations["CAM-2"]
  assert allocations["CAM-1"] > allocations["CAM-3"]
  ```
- **Expected Result:** Bandwidth allocated proportional to priority
- **Pass/Fail:** PASS if high-priority cameras get ≥2x more bandwidth
- **Tools:** pytest
- **Effort:** 4 hours

---

#### 1.2.2 Claude Agent

**Test Case UT-CLAUDE-001: Scene Memory Management**
- **Objective:** Verify scene memory stores and retrieves context correctly
- **Test Data:** 100 frames of metadata
- **Procedure:**
  ```python
  agent = ClaudeAgent()

  for i in range(100):
      metadata = create_test_metadata(frame_id=i)
      agent.update_scene_memory(metadata)

  # Verify memory size limited to 50 frames
  assert len(agent.scene_memory) == 50

  # Verify oldest frames evicted (FIFO)
  assert agent.scene_memory[0]["frame_id"] == 50
  ```
- **Expected Result:** Memory size capped, FIFO eviction
- **Pass/Fail:** PASS if memory size ≤ 50 and FIFO order maintained
- **Tools:** pytest
- **Effort:** 4 hours

**Test Case UT-CLAUDE-002: Frame Request Logic**
- **Objective:** Verify agent requests frames only when needed
- **Test Data:** Metadata with varying motion/object scores
- **Procedure:**
  ```python
  # Low motion, no objects → no request
  metadata1 = {"motion_score": 0.1, "object_count": 0}
  assert agent.should_request_frame(metadata1) == False

  # High motion → request
  metadata2 = {"motion_score": 0.8, "object_count": 0}
  assert agent.should_request_frame(metadata2) == True

  # Many objects → request
  metadata3 = {"motion_score": 0.1, "object_count": 5}
  assert agent.should_request_frame(metadata3) == True
  ```
- **Expected Result:** Frames requested only when thresholds exceeded
- **Pass/Fail:** PASS if request rate <20% for typical scenes
- **Tools:** pytest, hypothesis (property-based testing)
- **Effort:** 6 hours

**Test Case UT-CLAUDE-003: Pattern Detection**
- **Objective:** Verify agent detects recurring patterns in scene
- **Test Data:** Sequence of metadata showing repeating events
- **Procedure:**
  ```python
  # Simulate person appearing every 60 seconds
  for i in range(10):
      metadata = create_metadata(
          timestamp=i*60,
          objects=[{"class": "person", "confidence": 0.9}]
      )
      agent.update_scene_memory(metadata)

  patterns = agent.detect_patterns()
  assert "periodic_person" in patterns
  assert patterns["periodic_person"]["interval"] == 60
  ```
- **Expected Result:** Periodic patterns detected with correct interval
- **Pass/Fail:** PASS if pattern interval within 10% of actual
- **Tools:** pytest, numpy for statistical analysis
- **Effort:** 8 hours

---

#### 1.2.3 System Coordinator

**Test Case UT-COORD-001: Multi-Camera Correlation**
- **Objective:** Verify coordinator correlates events across cameras
- **Test Data:** Person detected in CAM-1, then CAM-2 (5 seconds later)
- **Procedure:**
  ```python
  coord = SystemCoordinator()

  event1 = {"camera": "CAM-1", "timestamp": 1000.0, "object": "person"}
  coord.process_event(event1)

  event2 = {"camera": "CAM-2", "timestamp": 1005.0, "object": "person"}
  coord.process_event(event2)

  correlated = coord.get_correlated_events()
  assert len(correlated) == 1
  assert correlated[0]["cameras"] == ["CAM-1", "CAM-2"]
  ```
- **Expected Result:** Events correlated within time window
- **Pass/Fail:** PASS if correlation accuracy >90%
- **Tools:** pytest
- **Effort:** 6 hours

**Test Case UT-COORD-002: Time Synchronization**
- **Objective:** Verify coordinator handles clock drift
- **Test Data:** Two cameras with 2-second clock skew
- **Procedure:**
  ```python
  # CAM-1 clock: 1000.0
  # CAM-2 clock: 1002.0 (2s ahead)

  coord.calibrate_clock_skew("CAM-1", offset=0)
  coord.calibrate_clock_skew("CAM-2", offset=2.0)

  event1 = {"camera": "CAM-1", "timestamp": 1000.0}
  event2 = {"camera": "CAM-2", "timestamp": 1002.0}

  # Should be treated as simultaneous after correction
  assert coord.events_simultaneous(event1, event2) == True
  ```
- **Expected Result:** Events synchronized despite clock skew
- **Pass/Fail:** PASS if synchronization accuracy <500ms
- **Tools:** pytest, mock NTP
- **Effort:** 5 hours

---

## 2. Integration Testing

### 2.1 Camera + MQTT Broker Integration

**Test Case IT-MQTT-001: Connection Establishment**
- **Objective:** Verify camera connects to MQTT broker on startup
- **Test Data:** Mosquitto broker running on localhost:1883
- **Procedure:**
  1. Start MQTT broker
  2. Start ACAP application
  3. Verify connection established within 5 seconds
  4. Verify TLS handshake (if enabled)
- **Expected Result:** Connection established, subscribed to topics
- **Pass/Fail:** PASS if connection state = CONNECTED within 5s
- **Tools:** Mosquitto, tcpdump, MQTT explorer
- **Effort:** 4 hours

**Test Case IT-MQTT-002: Message Delivery**
- **Objective:** Verify messages published by camera are received by broker
- **Test Data:** 1000 metadata messages
- **Procedure:**
  1. Subscribe to topic `axis-is/camera/CAM-001/metadata`
  2. Camera publishes 1000 messages
  3. Count received messages
- **Expected Result:** ≥995 messages received (≥99.5% delivery)
- **Pass/Fail:** PASS if delivery rate ≥99.5%
- **Tools:** MQTT subscriber, message counter
- **Effort:** 3 hours

**Test Case IT-MQTT-003: Reconnection Logic**
- **Objective:** Verify camera reconnects after broker restart
- **Test Data:** Broker restart scenario
- **Procedure:**
  1. Camera connected to broker
  2. Stop broker
  3. Verify camera detects disconnection
  4. Start broker
  5. Verify camera reconnects within 10 seconds
  6. Verify queued messages delivered
- **Expected Result:** Reconnection successful, no message loss
- **Pass/Fail:** PASS if reconnect <10s and delivery rate ≥99%
- **Tools:** Mosquitto, systemd, MQTT client
- **Effort:** 5 hours

---

### 2.2 MQTT Broker + Cloud Services Integration

**Test Case IT-CLOUD-001: Message Routing**
- **Objective:** Verify broker routes messages to cloud subscribers
- **Test Data:** 10 cameras publishing metadata
- **Procedure:**
  1. 10 cameras publish to topics `axis-is/camera/{CAM-ID}/metadata`
  2. Cloud service subscribes to `axis-is/camera/+/metadata`
  3. Verify all messages routed to cloud service
- **Expected Result:** 100% of messages routed correctly
- **Pass/Fail:** PASS if routing accuracy = 100%
- **Tools:** Mosquitto, Python MQTT client
- **Effort:** 4 hours

**Test Case IT-CLOUD-002: Topic Subscriptions**
- **Objective:** Verify wildcard subscriptions work correctly
- **Test Data:** Multiple topic patterns
- **Procedure:**
  ```python
  # Subscribe to all camera metadata
  client.subscribe("axis-is/camera/+/metadata")

  # Subscribe to all events from CAM-001
  client.subscribe("axis-is/camera/CAM-001/#")

  # Verify messages received as expected
  ```
- **Expected Result:** Messages match subscription patterns
- **Pass/Fail:** PASS if pattern matching = 100%
- **Tools:** MQTT client library
- **Effort:** 3 hours

**Test Case IT-CLOUD-003: Load Handling**
- **Objective:** Verify broker handles 100 msg/sec from 10 cameras
- **Test Data:** 10 cameras at 10 msg/sec each
- **Procedure:**
  1. Configure 10 cameras to publish at 10 FPS
  2. Monitor broker message rate
  3. Verify no message drops
  4. Monitor broker CPU/memory
- **Expected Result:** Broker handles load with <50% CPU
- **Pass/Fail:** PASS if message drop rate <0.1% and CPU <50%
- **Tools:** Mosquitto, htop, MQTT metrics
- **Effort:** 5 hours

---

### 2.3 Cloud Services + Claude API Integration

**Test Case IT-CLAUDE-001: API Rate Limiting**
- **Objective:** Verify cloud service respects Claude API rate limits
- **Test Data:** 1000 API requests in 1 minute
- **Procedure:**
  1. Submit 1000 analysis requests
  2. Verify requests throttled to stay under limit
  3. Verify retry logic on 429 errors
- **Expected Result:** No 429 errors, requests queued
- **Pass/Fail:** PASS if 0 rate limit errors
- **Tools:** Claude API, rate limiter implementation
- **Effort:** 6 hours

**Test Case IT-CLAUDE-002: Batch Processing**
- **Objective:** Verify multiple frames batched into single API call
- **Test Data:** 5 frames from same camera
- **Procedure:**
  1. Queue 5 frame analysis requests
  2. Verify batched into single Claude API call
  3. Verify all 5 frames analyzed
- **Expected Result:** Single API call for 5 frames
- **Pass/Fail:** PASS if API calls = 1 for 5 frames
- **Tools:** API call logging, Claude SDK
- **Effort:** 5 hours

**Test Case IT-CLAUDE-003: Error Recovery**
- **Objective:** Verify retry logic on Claude API errors
- **Test Data:** Simulated API failures
- **Procedure:**
  1. Mock Claude API to return 500 errors
  2. Submit analysis request
  3. Verify exponential backoff retry
  4. Verify eventual success on recovery
- **Expected Result:** Retries with backoff, eventual success
- **Pass/Fail:** PASS if request succeeds within 3 retries
- **Tools:** Mock API server, retry logic
- **Effort:** 4 hours

---

### 2.4 Multi-Camera DLPU Coordination

**Test Case IT-DLPU-001: Scheduler Prevents Collisions**
- **Objective:** Verify no two cameras use DLPU simultaneously
- **Test Data:** 5 cameras all requesting inference
- **Procedure:**
  1. Configure 5 cameras with synchronized start
  2. Monitor DLPU lock acquisition
  3. Verify only ONE camera holds lock at any time
- **Expected Result:** Zero DLPU collisions
- **Pass/Fail:** PASS if collision count = 0
- **Tools:** Shared memory monitor, lock tracer
- **Effort:** 8 hours

**Test Case IT-DLPU-002: Target FPS Achievement**
- **Objective:** Verify all cameras achieve 5 FPS inference rate
- **Test Data:** 5 cameras, 5 FPS target each
- **Procedure:**
  1. Run all 5 cameras for 60 seconds
  2. Measure actual inference FPS per camera
  3. Verify all cameras ≥5 FPS
- **Expected Result:** All cameras ≥5 FPS sustained
- **Pass/Fail:** PASS if min FPS across all cameras ≥5
- **Tools:** FPS counter, performance profiler
- **Effort:** 6 hours

**Test Case IT-DLPU-003: Fair Resource Allocation**
- **Objective:** Verify DLPU time distributed fairly across cameras
- **Test Data:** 5 cameras, equal priority
- **Procedure:**
  1. Run for 60 seconds
  2. Measure DLPU time per camera
  3. Verify variance <10% from mean
- **Expected Result:** Fair distribution (variance <10%)
- **Pass/Fail:** PASS if coefficient of variation <0.1
- **Tools:** DLPU time tracker, statistical analysis
- **Effort:** 5 hours

---

## 3. System Testing

### 3.1 End-to-End Happy Path Scenarios

**Test Case ST-E2E-001: Basic Detection and Analysis Flow**
- **Objective:** Verify complete flow from frame capture to Claude analysis
- **Test Data:** Live camera feed with person walking
- **Procedure:**
  1. Camera captures frame
  2. YOLOv5 inference detects person
  3. Metadata published to MQTT
  4. Cloud receives metadata
  5. Bandwidth controller approves frame request
  6. Camera sends full frame
  7. Claude analyzes frame
  8. Alert generated (if person in restricted area)
- **Expected Result:** Alert generated within 5 seconds of detection
- **Pass/Fail:** PASS if end-to-end latency <5s
- **Tools:** Full system deployment, timer instrumentation
- **Effort:** 8 hours

**Test Case ST-E2E-002: Multi-Camera Event Correlation**
- **Objective:** Verify person tracked across multiple cameras
- **Test Data:** Person walking past CAM-1, CAM-2, CAM-3
- **Procedure:**
  1. Person enters FOV of CAM-1
  2. Detection sent to coordinator
  3. Person leaves CAM-1, enters CAM-2
  4. Coordinator correlates as same person
  5. Alert generated for cross-camera tracking
- **Expected Result:** Single person ID across 3 cameras
- **Pass/Fail:** PASS if correlation accuracy ≥90%
- **Tools:** Full system, test subject walking path
- **Effort:** 10 hours

---

### 3.2 Degraded Network Scenarios

**Test Case ST-NET-001: 5% Packet Loss**
- **Objective:** Verify system operates under moderate packet loss
- **Test Data:** Network emulation with 5% random loss
- **Procedure:**
  1. Configure network emulator: `tc qdisc add dev eth0 root netem loss 5%`
  2. Run system for 30 minutes
  3. Measure MQTT delivery rate
  4. Measure frame request success rate
- **Expected Result:** System adapts, delivery rate ≥95%
- **Pass/Fail:** PASS if effective delivery ≥95%
- **Tools:** tc (traffic control), network emulator
- **Effort:** 4 hours

**Test Case ST-NET-002: High Latency (500ms)**
- **Objective:** Verify system tolerates high network latency
- **Test Data:** Network emulation with 500ms delay
- **Procedure:**
  1. Configure: `tc qdisc add dev eth0 root netem delay 500ms`
  2. Run system for 30 minutes
  3. Measure end-to-end latency
  4. Verify bandwidth controller reduces quality
- **Expected Result:** Quality reduced to MEDIUM/MINIMAL
- **Pass/Fail:** PASS if quality adapts correctly
- **Tools:** tc, latency monitor
- **Effort:** 4 hours

**Test Case ST-NET-003: Bandwidth Constraints (1 Mbps)**
- **Objective:** Verify adaptive bandwidth control works
- **Test Data:** Network limited to 1 Mbps
- **Procedure:**
  1. Configure: `tc qdisc add dev eth0 root tbf rate 1mbit burst 32kbit latency 400ms`
  2. Run 10 cameras
  3. Verify bandwidth controller reduces quality
  4. Verify metadata still delivered at 10 FPS
- **Expected Result:** Metadata delivery maintained, frame quality reduced
- **Pass/Fail:** PASS if metadata FPS ≥10 and frames compressed
- **Tools:** tc, bandwidth monitor
- **Effort:** 6 hours

**Test Case ST-NET-004: MQTT Broker Unavailable**
- **Objective:** Verify camera queues messages when broker down
- **Test Data:** Broker offline for 5 minutes
- **Procedure:**
  1. Stop MQTT broker
  2. Camera continues capturing/inference
  3. Messages queued locally
  4. Restart broker after 5 minutes
  5. Verify queued messages delivered
- **Expected Result:** All messages delivered after reconnection
- **Pass/Fail:** PASS if delivery rate ≥99% after reconnect
- **Tools:** Mosquitto, message queue inspector
- **Effort:** 5 hours

---

### 3.3 Resource Constraint Scenarios

**Test Case ST-RES-001: DLPU Fully Utilized**
- **Objective:** Verify graceful degradation when DLPU saturated
- **Test Data:** 10 cameras on single DLPU (2x oversubscription)
- **Procedure:**
  1. Configure 10 cameras, 5 FPS target each
  2. Monitor DLPU utilization (should be 100%)
  3. Measure actual FPS per camera
  4. Verify fair scheduling
- **Expected Result:** All cameras get ≥2.5 FPS (50% of target)
- **Pass/Fail:** PASS if min FPS ≥2.5 and fair distribution
- **Tools:** DLPU profiler, FPS monitor
- **Effort:** 6 hours

**Test Case ST-RES-002: Memory Pressure (OOM Scenarios)**
- **Objective:** Verify system handles low memory gracefully
- **Test Data:** Memory limited to 600 MB (vs 700 MB target)
- **Procedure:**
  1. Configure cgroup memory limit: 600 MB
  2. Run camera application
  3. Monitor memory usage
  4. Verify no OOM kills
  5. Verify graceful degradation (reduce inference rate)
- **Expected Result:** No crashes, reduced FPS acceptable
- **Pass/Fail:** PASS if no OOM kills in 1 hour
- **Tools:** cgroup, memory profiler, OOM killer logs
- **Effort:** 6 hours

**Test Case ST-RES-003: CPU Saturation**
- **Objective:** Verify system handles CPU overload
- **Test Data:** 100% CPU load from competing processes
- **Procedure:**
  1. Start CPU stress test: `stress-ng --cpu 4`
  2. Run Axis I.S. camera application
  3. Measure inference FPS
  4. Verify no deadlocks or hangs
- **Expected Result:** Reduced FPS but continued operation
- **Pass/Fail:** PASS if FPS ≥1 and no hangs
- **Tools:** stress-ng, htop, profiler
- **Effort:** 4 hours

**Test Case ST-RES-004: Network Saturation**
- **Objective:** Verify system handles network congestion
- **Test Data:** Network saturated with iperf traffic
- **Procedure:**
  1. Start iperf server/client to saturate network
  2. Run Axis I.S. system
  3. Verify bandwidth controller reduces quality
  4. Verify metadata still delivered
- **Expected Result:** Quality reduced, metadata delivered
- **Pass/Fail:** PASS if metadata delivery ≥90%
- **Tools:** iperf3, bandwidth monitor
- **Effort:** 5 hours

---

### 3.4 Failure Scenarios

**Test Case ST-FAIL-001: Camera Reboot During Operation**
- **Objective:** Verify system recovers from camera reboot
- **Test Data:** Camera reboot via VAPIX API
- **Procedure:**
  1. Camera running normally
  2. Issue reboot command
  3. Camera reboots (~60 seconds)
  4. Verify ACAP auto-starts
  5. Verify reconnection to MQTT broker
  6. Verify inference resumes
- **Expected Result:** Full recovery within 90 seconds
- **Pass/Fail:** PASS if operational within 90s of boot
- **Tools:** VAPIX API, systemd logs
- **Effort:** 4 hours

**Test Case ST-FAIL-002: MQTT Broker Restart**
- **Objective:** Verify all cameras reconnect after broker restart
- **Test Data:** 10 cameras connected, broker restart
- **Procedure:**
  1. Stop Mosquitto broker
  2. Wait 30 seconds
  3. Start broker
  4. Verify all 10 cameras reconnect within 30 seconds
  5. Verify message delivery resumes
- **Expected Result:** All cameras reconnected within 30s
- **Pass/Fail:** PASS if 100% reconnection rate
- **Tools:** Mosquitto, MQTT client monitor
- **Effort:** 3 hours

**Test Case ST-FAIL-003: Database Connection Loss**
- **Objective:** Verify cloud service handles DB disconnect
- **Test Data:** PostgreSQL stopped during operation
- **Procedure:**
  1. Stop PostgreSQL
  2. Cloud service attempts to log event
  3. Verify retry logic engaged
  4. Verify fallback to file logging
  5. Start PostgreSQL
  6. Verify reconnection and backlog flush
- **Expected Result:** No data loss, eventual consistency
- **Pass/Fail:** PASS if all events logged after recovery
- **Tools:** PostgreSQL, connection pool monitor
- **Effort:** 5 hours

**Test Case ST-FAIL-004: Claude API Outage**
- **Objective:** Verify graceful degradation during API outage
- **Test Data:** Claude API returns 503 errors
- **Procedure:**
  1. Mock Claude API to return 503
  2. Submit frame analysis requests
  3. Verify requests queued (not dropped)
  4. Verify alerts still generated from metadata
  5. API recovers
  6. Verify queued frames analyzed
- **Expected Result:** Metadata-based alerts continue, queue processed after recovery
- **Pass/Fail:** PASS if no dropped requests and eventual analysis
- **Tools:** Mock API, queue inspector
- **Effort:** 6 hours

**Test Case ST-FAIL-005: Time Synchronization Drift**
- **Objective:** Verify system handles clock drift between cameras
- **Test Data:** 2 cameras with 5-second clock skew
- **Procedure:**
  1. Manually set CAM-2 clock 5 seconds ahead
  2. Run multi-camera correlation test
  3. Verify coordinator detects and corrects skew
  4. Verify events still correlated correctly
- **Expected Result:** Correlation accuracy ≥85%
- **Pass/Fail:** PASS if correlation accuracy ≥85%
- **Tools:** NTP offset tool, coordinator logs
- **Effort:** 5 hours

---

## 4. Performance Testing

### 4.1 Camera-Side Performance Benchmarks

**Test Case PT-CAM-001: Inference FPS Sustained**
- **Objective:** Verify 5-10 FPS sustained over 1 hour
- **Test Data:** Single camera, continuous operation
- **Procedure:**
  1. Run camera for 60 minutes
  2. Measure inference FPS every 10 seconds
  3. Calculate average, min, max, P95, P99
- **Expected Result:** Avg ≥5 FPS, P95 ≥5 FPS
- **Pass/Fail:** PASS if P95 ≥5 FPS
- **Tools:** FPS counter, statistical analysis
- **Effort:** 3 hours

**Test Case PT-CAM-002: Memory Usage Over 72 Hours**
- **Objective:** Verify memory stays <700 MB for 72 hours (critical!)
- **Test Data:** Camera running 72 hours continuously
- **Procedure:**
  1. Start camera, measure baseline memory
  2. Run for 72 hours
  3. Sample memory every 5 minutes
  4. Plot memory usage over time
  5. Verify no upward trend (leak detection)
- **Expected Result:** Memory <700 MB, no leaks
- **Pass/Fail:** PASS if max memory <700 MB and trend slope <0.1 MB/hour
- **Tools:** Valgrind, memory profiler, smem, gnuplot
- **Effort:** 80 hours (mostly waiting, 8 hours active work)

**Test Case PT-CAM-003: DLPU Wait Times**
- **Objective:** Verify DLPU wait time <100ms (P95)
- **Test Data:** 5 cameras sharing DLPU
- **Procedure:**
  1. Run 5 cameras for 30 minutes
  2. Measure time from inference request to DLPU acquisition
  3. Calculate P50, P95, P99 wait times
- **Expected Result:** P95 wait time <100ms
- **Pass/Fail:** PASS if P95 <100ms
- **Tools:** Timestamp logging, percentile calculator
- **Effort:** 4 hours

**Test Case PT-CAM-004: MQTT Publish Latency**
- **Objective:** Verify metadata publish latency <50ms
- **Test Data:** 1000 messages published
- **Procedure:**
  1. Timestamp at message creation
  2. Timestamp at MQTT publish completion
  3. Calculate latency distribution
- **Expected Result:** P95 latency <50ms
- **Pass/Fail:** PASS if P95 <50ms
- **Tools:** High-resolution timer, histogram
- **Effort:** 3 hours

---

### 4.2 Cloud-Side Performance Benchmarks

**Test Case PT-CLOUD-001: Metadata Ingestion Rate**
- **Objective:** Verify cloud ingests 100 msg/sec from 10 cameras
- **Test Data:** 10 cameras at 10 msg/sec each
- **Procedure:**
  1. Configure 10 cameras for 10 FPS metadata
  2. Run for 10 minutes
  3. Measure cloud ingestion rate
  4. Verify no dropped messages
- **Expected Result:** 100 msg/sec sustained, 0% drops
- **Pass/Fail:** PASS if ingestion rate ≥100 msg/sec and drops <1%
- **Tools:** MQTT broker metrics, cloud service logs
- **Effort:** 4 hours

**Test Case PT-CLOUD-002: Claude Analysis Latency**
- **Objective:** Verify frame analysis completes <3 seconds (P95)
- **Test Data:** 100 frames submitted for analysis
- **Procedure:**
  1. Submit 100 frames to Claude Agent
  2. Measure time from submission to result
  3. Calculate P50, P95, P99 latency
- **Expected Result:** P95 latency <3 seconds
- **Pass/Fail:** PASS if P95 <3s
- **Tools:** Timer, Claude API metrics
- **Effort:** 4 hours

**Test Case PT-CLOUD-003: Database Query Response Times**
- **Objective:** Verify DB queries complete <100ms (P95)
- **Test Data:** 1000 typical queries (event lookup, camera status)
- **Procedure:**
  1. Execute 1000 queries
  2. Measure query execution time
  3. Calculate latency distribution
- **Expected Result:** P95 latency <100ms
- **Pass/Fail:** PASS if P95 <100ms
- **Tools:** PostgreSQL EXPLAIN ANALYZE, pg_stat_statements
- **Effort:** 5 hours

**Test Case PT-CLOUD-004: Alert Generation Latency**
- **Objective:** Verify alerts generated <1 second after detection
- **Test Data:** 100 alert-triggering events
- **Procedure:**
  1. Generate 100 events requiring alerts
  2. Measure time from event to alert
  3. Calculate latency distribution
- **Expected Result:** P95 latency <1 second
- **Pass/Fail:** PASS if P95 <1s
- **Tools:** Event tracer, alert system logs
- **Effort:** 4 hours

---

### 4.3 Multi-Camera Scaling Tests

**Test Case PT-SCALE-001: 2 Cameras**
- **Objective:** Baseline performance with 2 cameras
- **Test Data:** 2 cameras, 5 FPS target each
- **Procedure:**
  1. Measure per-camera CPU, memory, network usage
  2. Measure DLPU utilization
  3. Measure inference FPS
- **Expected Result:** Both cameras achieve ≥5 FPS
- **Pass/Fail:** PASS if both ≥5 FPS
- **Tools:** htop, iftop, DLPU profiler
- **Effort:** 3 hours

**Test Case PT-SCALE-002: 5 Cameras**
- **Objective:** Target deployment scale performance
- **Test Data:** 5 cameras, 5 FPS target each
- **Procedure:**
  1. Measure aggregate metrics
  2. Identify bottlenecks (DLPU, CPU, network)
  3. Verify fair resource distribution
- **Expected Result:** All 5 cameras achieve ≥5 FPS
- **Pass/Fail:** PASS if all ≥5 FPS
- **Tools:** Full monitoring stack
- **Effort:** 4 hours

**Test Case PT-SCALE-003: 10 Cameras**
- **Objective:** Stress test with 2x target scale
- **Test Data:** 10 cameras, 5 FPS target each
- **Procedure:**
  1. Measure per-camera FPS (expect degradation)
  2. Identify primary bottleneck
  3. Verify no crashes or deadlocks
- **Expected Result:** All cameras ≥2.5 FPS (graceful degradation)
- **Pass/Fail:** PASS if all ≥2.5 FPS and stable
- **Tools:** Full monitoring stack
- **Effort:** 5 hours

---

## 5. Stress Testing

**Test Case STRESS-001: 20 Cameras on Single Scheduler**
- **Objective:** Test 4x oversubscription (breaking point)
- **Test Data:** 20 cameras, 5 FPS target each
- **Procedure:**
  1. Deploy 20 camera instances
  2. Monitor DLPU scheduler behavior
  3. Measure FPS distribution
  4. Monitor for crashes, deadlocks, starvation
- **Expected Result:** System remains stable, FPS reduced but fair
- **Pass/Fail:** PASS if no crashes and min FPS ≥1
- **Tools:** Full monitoring, crash detector
- **Effort:** 8 hours

**Test Case STRESS-002: Sustained 100% DLPU Utilization**
- **Objective:** Verify scheduler handles continuous saturation
- **Test Data:** DLPU at 100% for 24 hours
- **Procedure:**
  1. Configure cameras to saturate DLPU
  2. Run for 24 hours
  3. Monitor for scheduler issues
  4. Verify fair allocation maintained
- **Expected Result:** No crashes, fair allocation
- **Pass/Fail:** PASS if stable for 24 hours
- **Tools:** DLPU monitor, scheduler tracer
- **Effort:** 28 hours (24h test + 4h analysis)

**Test Case STRESS-003: Network Bandwidth Saturation**
- **Objective:** Test behavior under network saturation
- **Test Data:** 10 cameras publishing at max bandwidth
- **Procedure:**
  1. Disable bandwidth controller
  2. All cameras publish full frames at max rate
  3. Saturate network link
  4. Verify no deadlocks or crashes
  5. Enable bandwidth controller
  6. Verify quality reduction
- **Expected Result:** Graceful degradation with controller
- **Pass/Fail:** PASS if system stable and adapts
- **Tools:** iperf3, tc, bandwidth monitor
- **Effort:** 6 hours

**Test Case STRESS-004: Database Connection Pool Exhaustion**
- **Objective:** Test cloud service under DB connection pressure
- **Test Data:** 1000 concurrent DB queries
- **Procedure:**
  1. Submit 1000 queries simultaneously
  2. Verify connection pool handles load
  3. Measure query queueing
  4. Verify no connection leaks
- **Expected Result:** Queries queued, no errors
- **Pass/Fail:** PASS if all queries complete and no leaks
- **Tools:** PostgreSQL connection monitor, pgBouncer
- **Effort:** 5 hours

**Test Case STRESS-005: Claude API Rate Limit Triggering**
- **Objective:** Verify graceful handling of rate limits
- **Test Data:** Submit requests above API rate limit
- **Procedure:**
  1. Submit 1000 requests in 1 minute
  2. Verify 429 errors handled
  3. Verify exponential backoff
  4. Verify eventual success
- **Expected Result:** All requests eventually succeed
- **Pass/Fail:** PASS if 100% success after retries
- **Tools:** API mock, rate limiter
- **Effort:** 4 hours

---

## 6. Reliability Testing (Critical!)

### 6.1 Memory Leak Detection

**Test Case REL-MEM-001: 72-Hour Valgrind Run**
- **Objective:** Detect any memory leaks over extended operation (CRITICAL!)
- **Test Data:** Camera running 72 hours under Valgrind
- **Procedure:**
  ```bash
  valgrind --leak-check=full \
           --show-leak-kinds=all \
           --track-origins=yes \
           --log-file=valgrind-72h.log \
           ./axion_acap

  # Run for 72 hours
  # Analyze valgrind-72h.log
  ```
- **Expected Result:** 0 bytes definitely lost, 0 bytes indirectly lost
- **Pass/Fail:** PASS if `definitely lost: 0 bytes in 0 blocks`
- **Tools:** Valgrind, memory profiler
- **Effort:** 80 hours (mostly waiting)

**Test Case REL-MEM-002: Memory Trend Analysis**
- **Objective:** Verify no memory growth trend over time
- **Test Data:** Memory samples every 5 minutes for 72 hours
- **Procedure:**
  ```bash
  # Sample memory every 5 minutes
  while true; do
      smem -c "pid name pss" | grep axion >> mem_log.txt
      sleep 300
  done

  # Plot trend line
  # Verify slope near zero
  ```
- **Expected Result:** Trend slope <0.1 MB/hour
- **Pass/Fail:** PASS if linear regression slope <0.1 MB/hour
- **Tools:** smem, gnuplot, statistical analysis
- **Effort:** 76 hours

**Test Case REL-MEM-003: VDO Buffer Reference Counting**
- **Objective:** Verify all VDO buffers properly unreferenced (CRITICAL!)
- **Test Data:** 10000 frame captures
- **Procedure:**
  ```c
  int unref_count = 0;
  int get_count = 0;

  for (int i = 0; i < 10000; i++) {
      VdoBuffer* buffer;
      vdo_stream_get_buffer(stream, &buffer, 100);
      get_count++;

      // Use buffer

      vdo_buffer_unref(buffer);
      unref_count++;
  }

  assert(get_count == unref_count);  // Must match!
  ```
- **Expected Result:** get_count == unref_count
- **Pass/Fail:** PASS if perfect 1:1 match
- **Tools:** Custom instrumentation, assertion checks
- **Effort:** 4 hours

---

### 6.2 MQTT Message Delivery Reliability

**Test Case REL-MQTT-001: 1% Message Loss Simulation**
- **Objective:** Verify retry logic handles packet loss
- **Test Data:** Network with 1% packet loss
- **Procedure:**
  1. Configure: `tc qdisc add dev eth0 root netem loss 1%`
  2. Publish 10000 messages
  3. Monitor MQTT broker receive count
  4. Verify retry logic fills gaps
- **Expected Result:** ≥99.5% delivery after retries
- **Pass/Fail:** PASS if delivery ≥99.5%
- **Tools:** tc, MQTT message counter
- **Effort:** 4 hours

**Test Case REL-MQTT-002: Sequence Gap Detection**
- **Objective:** Verify cloud detects missing sequence numbers
- **Test Data:** Artificially drop messages 100, 200, 300
- **Procedure:**
  1. Publish messages 1-1000, skip 100, 200, 300
  2. Cloud service detects gaps
  3. Verify gap alerts generated
- **Expected Result:** All 3 gaps detected
- **Pass/Fail:** PASS if 100% gap detection
- **Tools:** Custom sequence checker
- **Effort:** 3 hours

**Test Case REL-MQTT-003: Retry Logic Under Sustained Loss**
- **Objective:** Verify retry logic handles sustained network issues
- **Test Data:** Network offline for 60 seconds, then recovers
- **Procedure:**
  1. Disconnect network
  2. Camera continues publishing (queued locally)
  3. After 60 seconds, reconnect
  4. Verify queued messages delivered
  5. Verify sequence integrity
- **Expected Result:** 100% of messages delivered after reconnection
- **Pass/Fail:** PASS if delivery = 100%
- **Tools:** Network control, message queue inspector
- **Effort:** 4 hours

---

### 6.3 Time Synchronization Reliability

**Test Case REL-TIME-001: Clock Drift Simulation (1 second)**
- **Objective:** Verify correlation works with 1s clock skew
- **Test Data:** 2 cameras with 1s drift
- **Procedure:**
  1. Set CAM-2 clock 1s ahead
  2. Person walks past both cameras
  3. Verify coordinator correlates events
- **Expected Result:** Events correlated correctly
- **Pass/Fail:** PASS if correlation accuracy ≥95%
- **Tools:** NTP offset, correlation analyzer
- **Effort:** 3 hours

**Test Case REL-TIME-002: Clock Drift Simulation (2 seconds)**
- **Objective:** Test correlation at tolerance limit
- **Test Data:** 2 cameras with 2s drift
- **Procedure:**
  1. Set CAM-2 clock 2s ahead
  2. Run correlation tests
  3. Measure accuracy degradation
- **Expected Result:** Correlation accuracy ≥85%
- **Pass/Fail:** PASS if accuracy ≥85%
- **Tools:** NTP offset, correlation analyzer
- **Effort:** 3 hours

**Test Case REL-TIME-003: NTP Sync Recovery**
- **Objective:** Verify system recovers from clock correction
- **Test Data:** NTP sync corrects 5s drift
- **Procedure:**
  1. Manually set clock 5s ahead
  2. Trigger NTP sync
  3. Verify system adapts to sudden time change
  4. Verify no timestamp reversals in logs
- **Expected Result:** System adapts gracefully
- **Pass/Fail:** PASS if no crashes and logs consistent
- **Tools:** NTP, system logs
- **Effort:** 3 hours

---

### 6.4 DLPU Scheduler Robustness

**Test Case REL-DLPU-001: Camera Process Crash Mid-Inference**
- **Objective:** Verify scheduler recovers from process crash (CRITICAL!)
- **Test Data:** Kill camera process holding DLPU lock
- **Procedure:**
  ```bash
  # Camera A acquires DLPU lock
  # Send SIGKILL to Camera A
  kill -9 $CAMERA_A_PID

  # Verify scheduler detects stale lock (timeout 5s)
  # Verify lock released
  # Verify Camera B can acquire lock
  ```
- **Expected Result:** Lock released within 5 seconds
- **Pass/Fail:** PASS if lock available within 5s
- **Tools:** Kill, shared memory monitor
- **Effort:** 6 hours

**Test Case REL-DLPU-002: Scheduler Recovery After Corruption**
- **Objective:** Verify scheduler recovers from corrupted state
- **Test Data:** Manually corrupt scheduler shared memory
- **Procedure:**
  1. Corrupt scheduler state in shared memory
  2. Next camera detects corruption
  3. Scheduler reinitializes
  4. Verify normal operation resumes
- **Expected Result:** Scheduler reinits and recovers
- **Pass/Fail:** PASS if recovery successful
- **Tools:** Shared memory editor, scheduler monitor
- **Effort:** 8 hours

**Test Case REL-DLPU-003: Stale Lock Detection**
- **Objective:** Verify scheduler detects and clears stale locks
- **Test Data:** Lock held >5 seconds (timeout threshold)
- **Procedure:**
  1. Camera A acquires lock
  2. Camera A hangs (simulated with sleep(10))
  3. Camera B attempts lock acquisition
  4. After 5s timeout, Camera B clears stale lock
  5. Camera B proceeds with inference
- **Expected Result:** Stale lock cleared, Camera B succeeds
- **Pass/Fail:** PASS if Camera B succeeds after timeout
- **Tools:** Lock timeout monitor
- **Effort:** 5 hours

---

## 7. Security Testing

**Test Case SEC-001: Authentication Bypass Attempts**
- **Objective:** Verify MQTT broker rejects unauthenticated connections
- **Test Data:** Connection attempts without credentials
- **Procedure:**
  1. Attempt MQTT connection without username/password
  2. Attempt with invalid credentials
  3. Verify all rejected
- **Expected Result:** 100% rejection of invalid auth
- **Pass/Fail:** PASS if 0 successful unauthorized connections
- **Tools:** MQTT client, Mosquitto logs
- **Effort:** 3 hours

**Test Case SEC-002: MQTT Topic Injection**
- **Objective:** Verify topic access control prevents injection
- **Test Data:** Malicious topic names
- **Procedure:**
  ```python
  # Attempt to publish to unauthorized topics
  client.publish("axis-is/camera/../../admin/commands", "malicious")

  # Verify publish rejected
  ```
- **Expected Result:** Unauthorized publishes rejected
- **Pass/Fail:** PASS if ACL blocks unauthorized topics
- **Tools:** Mosquitto ACL, MQTT client
- **Effort:** 4 hours

**Test Case SEC-003: SQL Injection in Queries**
- **Objective:** Verify parameterized queries prevent SQL injection
- **Test Data:** Malicious input strings
- **Procedure:**
  ```python
  # Attempt SQL injection
  camera_id = "CAM-001'; DROP TABLE events; --"
  result = db.query("SELECT * FROM events WHERE camera_id = %s", camera_id)

  # Verify query safely escaped
  # Verify tables intact
  ```
- **Expected Result:** Injection blocked, data safe
- **Pass/Fail:** PASS if 0 successful injections
- **Tools:** SQL injection scanner, PostgreSQL logs
- **Effort:** 5 hours

**Test Case SEC-004: API Rate Limit Bypass Attempts**
- **Objective:** Verify rate limiter cannot be bypassed
- **Test Data:** Rapid API requests from multiple IPs
- **Procedure:**
  1. Submit requests above rate limit
  2. Attempt IP spoofing
  3. Attempt request smuggling
  4. Verify all blocked
- **Expected Result:** Rate limit enforced consistently
- **Pass/Fail:** PASS if 100% of excess requests blocked
- **Tools:** Rate limit tester, load generator
- **Effort:** 4 hours

**Test Case SEC-005: Credential Exposure**
- **Objective:** Verify no credentials in logs, error messages, or responses
- **Test Data:** All log files and API responses
- **Procedure:**
  ```bash
  # Search logs for credentials
  grep -r "password" /var/log/axis-is/
  grep -r "api_key" /var/log/axis-is/
  grep -r "secret" /var/log/axis-is/

  # Verify no matches
  ```
- **Expected Result:** 0 credential exposures
- **Pass/Fail:** PASS if 0 credentials found
- **Tools:** grep, log analyzer
- **Effort:** 3 hours

---

## 8. Chaos Engineering

**Test Case CHAOS-001: Random Container Kills**
- **Objective:** Verify system resilience to random service failures
- **Test Data:** Kill random containers every 5 minutes
- **Procedure:**
  ```bash
  # Chaos monkey script
  while true; do
      # Kill random container
      CONTAINER=$(docker ps -q | shuf -n 1)
      docker kill $CONTAINER
      sleep 300
  done
  ```
- **Expected Result:** System recovers from each kill
- **Pass/Fail:** PASS if uptime ≥95% over 24 hours
- **Tools:** Docker, chaos monkey script
- **Effort:** 28 hours

**Test Case CHAOS-002: Random Network Packet Drops**
- **Objective:** Verify resilience to intermittent packet loss
- **Test Data:** Random 1-10% packet loss every 10 minutes
- **Procedure:**
  ```bash
  while true; do
      LOSS=$((1 + RANDOM % 10))
      tc qdisc change dev eth0 root netem loss ${LOSS}%
      sleep 600
  done
  ```
- **Expected Result:** System adapts to changing conditions
- **Pass/Fail:** PASS if delivery rate ≥90%
- **Tools:** tc, network monitor
- **Effort:** 6 hours

**Test Case CHAOS-003: Random Message Corruption**
- **Objective:** Verify message validation detects corruption
- **Test Data:** Corrupt 1% of MQTT messages
- **Procedure:**
  1. MQTT proxy corrupts random bytes
  2. Cloud service receives corrupted messages
  3. Verify validation detects corruption
  4. Verify corrupted messages discarded
- **Expected Result:** 100% corruption detection
- **Pass/Fail:** PASS if 0 corrupted messages processed
- **Tools:** MQTT proxy, corruption injector
- **Effort:** 8 hours

**Test Case CHAOS-004: Random Latency Spikes**
- **Objective:** Verify timeout handling with variable latency
- **Test Data:** Random 0-1000ms latency spikes
- **Procedure:**
  ```bash
  while true; do
      DELAY=$((RANDOM % 1000))
      tc qdisc change dev eth0 root netem delay ${DELAY}ms
      sleep 30
  done
  ```
- **Expected Result:** Timeouts handled gracefully
- **Pass/Fail:** PASS if no crashes or deadlocks
- **Tools:** tc, latency monitor
- **Effort:** 4 hours

**Test Case CHAOS-005: Clock Skew Injection**
- **Objective:** Verify time correlation handles random clock changes
- **Test Data:** Random ±5 second clock adjustments
- **Procedure:**
  ```bash
  while true; do
      SKEW=$((RANDOM % 10 - 5))
      date -s "+${SKEW} seconds"
      sleep 600
  done
  ```
- **Expected Result:** Correlation adapts to clock changes
- **Pass/Fail:** PASS if correlation accuracy ≥80%
- **Tools:** date, NTP, correlation analyzer
- **Effort:** 6 hours

---

## 9. Test Environments

### 9.1 Local Development Environment

**Purpose:** Developer testing, rapid iteration

**Components:**
- Docker Compose stack:
  - Mosquitto MQTT broker
  - Redis
  - PostgreSQL
  - Mock camera simulator (Python)
  - Cloud services (Python)
- Mock VDO/Larod libraries
- Simulated network conditions (tc)

**Setup:**
```yaml
# docker-compose.yml
version: '3.8'
services:
  mosquitto:
    image: eclipse-mosquitto:2.0
    ports:
      - "1883:1883"

  redis:
    image: redis:7-alpine

  postgres:
    image: postgres:15
    environment:
      POSTGRES_DB: axion

  mock_camera:
    build: ./tests/mock_camera
    depends_on:
      - mosquitto
```

**Characteristics:**
- Fast startup (<30 seconds)
- Reproducible (Docker Compose)
- Isolated (no real cameras needed)
- Suitable for unit and integration tests

---

### 9.2 Staging Environment

**Purpose:** Integration testing with real hardware

**Components:**
- 2-3 real Axis ARTPEC-9 cameras
- Physical network switch (isolated VLAN)
- Dedicated server for cloud services
- Full monitoring stack (Grafana, Prometheus)

**Setup:**
```
Camera 1 (192.168.100.101) ──┐
Camera 2 (192.168.100.102) ──┼── Switch ── Server (192.168.100.10)
Camera 3 (192.168.100.103) ──┘                    ├── Mosquitto
                                                  ├── Redis
                                                  ├── PostgreSQL
                                                  ├── Cloud Services
                                                  └── Grafana
```

**Characteristics:**
- Real camera hardware (ARTPEC-9)
- Isolated network (no production impact)
- Full observability stack
- Suitable for system and performance tests

---

### 9.3 Production-Like Environment

**Purpose:** Final validation before production deployment

**Components:**
- 10 Axis ARTPEC-9 cameras
- Production network topology
- High-availability cloud services (k8s)
- Full security configuration (TLS, auth)
- Complete monitoring and alerting

**Setup:**
- Kubernetes cluster for cloud services
- Load balancer for MQTT broker
- Replicated PostgreSQL
- Redis Sentinel for HA
- Full TLS encryption
- LDAP/OAuth authentication

**Characteristics:**
- Matches production configuration
- Full scale (10 cameras)
- Production-grade security
- Suitable for stress, reliability, and chaos tests

---

## 10. Test Automation

### 10.1 CI/CD Integration

**Pipeline Stages:**

```yaml
# .github/workflows/test.yml
name: Axis I.S. Test Suite

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Run camera-side unit tests
        run: |
          docker build -t axion-test -f Dockerfile.test .
          docker run axion-test make test

      - name: Run cloud-side unit tests
        run: |
          cd cloud
          pip install -r requirements-test.txt
          pytest --cov=axion tests/

  integration-tests:
    needs: unit-tests
    runs-on: ubuntu-latest
    steps:
      - name: Start test environment
        run: docker-compose -f docker-compose.test.yml up -d

      - name: Run integration tests
        run: pytest tests/integration/

      - name: Collect logs
        if: failure()
        run: docker-compose logs > integration-logs.txt

  performance-tests:
    needs: integration-tests
    runs-on: ubuntu-latest
    steps:
      - name: Run performance benchmarks
        run: pytest tests/performance/ --benchmark-only

      - name: Check performance regression
        run: |
          python scripts/check_performance_regression.py \
            --baseline results/baseline.json \
            --current results/current.json
```

---

### 10.2 Automated Regression Suite

**Test Categories:**
1. **Smoke Tests** (run on every commit)
   - Basic functionality: camera connects, publishes metadata
   - Duration: 5 minutes
   - Trigger: Every git push

2. **Regression Tests** (run on every PR)
   - Full unit test suite
   - Critical integration tests
   - Duration: 30 minutes
   - Trigger: Pull request creation

3. **Nightly Tests** (run overnight)
   - Full integration test suite
   - Performance benchmarks
   - Memory leak detection (12-hour run)
   - Duration: 14 hours
   - Trigger: Daily at 8 PM

4. **Weekly Tests** (run on weekends)
   - 72-hour reliability test
   - Full chaos engineering suite
   - Stress tests
   - Duration: 72 hours
   - Trigger: Every Saturday

---

### 10.3 Performance Regression Detection

**Baseline Establishment:**
```python
# scripts/establish_baseline.py
def establish_baseline():
    """Run performance tests and save baseline metrics"""
    results = {
        "inference_fps": run_fps_test(),
        "dlpu_wait_time_p95": run_dlpu_test(),
        "mqtt_latency_p95": run_mqtt_test(),
        "memory_usage_72h": run_memory_test(),
    }
    save_json("baseline.json", results)
```

**Regression Detection:**
```python
# scripts/check_performance_regression.py
def check_regression(baseline, current):
    """Compare current results to baseline, fail if >10% worse"""
    regressions = []

    for metric, baseline_value in baseline.items():
        current_value = current[metric]
        regression_pct = (current_value - baseline_value) / baseline_value * 100

        if regression_pct > 10:  # 10% threshold
            regressions.append(f"{metric}: {regression_pct:.1f}% regression")

    if regressions:
        print("PERFORMANCE REGRESSION DETECTED:")
        for r in regressions:
            print(f"  - {r}")
        sys.exit(1)
```

---

### 10.4 Test Data Generation

**Mock Camera Simulator:**
```python
# tests/mock_camera/simulator.py
class MockCamera:
    """Simulates Axis camera ACAP behavior"""

    def __init__(self, camera_id, fps=5):
        self.camera_id = camera_id
        self.fps = fps
        self.sequence = 0

    def generate_metadata(self):
        """Generate realistic metadata"""
        return {
            "camera_id": self.camera_id,
            "sequence": self.sequence,
            "timestamp": time.time(),
            "motion_score": random.uniform(0, 1),
            "objects": [
                {
                    "class": random.choice(["person", "car", "bicycle"]),
                    "confidence": random.uniform(0.6, 0.95),
                    "bbox": [random.randint(0, 416) for _ in range(4)]
                }
                for _ in range(random.randint(0, 5))
            ],
            "scene_hash": hashlib.md5(str(self.sequence).encode()).hexdigest()
        }
        self.sequence += 1
```

**Test Data Sets:**
- **empty_scene.json** - No objects, minimal motion
- **busy_scene.json** - Many objects, high motion
- **edge_cases.json** - Invalid data, corrupted messages
- **timing_tests.json** - Precise timestamps for correlation tests

---

### 10.5 Test Reporting and Dashboards

**Grafana Dashboard:**

```json
{
  "dashboard": {
    "title": "Axis I.S. Test Metrics",
    "panels": [
      {
        "title": "Test Pass Rate",
        "targets": [{
          "expr": "sum(test_passed) / sum(test_total) * 100"
        }]
      },
      {
        "title": "Performance Regression",
        "targets": [{
          "expr": "performance_metric / performance_baseline"
        }]
      },
      {
        "title": "Code Coverage",
        "targets": [{
          "expr": "code_coverage_percent"
        }]
      }
    ]
  }
}
```

**HTML Test Report:**
```bash
# Generate HTML report after test run
pytest --html=report.html --self-contained-html
```

**Test Result Summary Email:**
```python
# Send email after nightly tests
def send_test_report(results):
    summary = f"""
    Axis I.S. Nightly Test Results - {date.today()}

    Total Tests: {results['total']}
    Passed: {results['passed']} ({results['pass_rate']:.1f}%)
    Failed: {results['failed']}

    Performance Regressions: {results['regressions']}
    Memory Leaks Detected: {results['memory_leaks']}

    Full report: https://jenkins.example.com/job/axion-tests/
    """
    send_email(to="team@example.com", subject="Axis I.S. Test Report", body=summary)
```

---

## 11. Acceptance Criteria

### 11.1 Functional Requirements

| Requirement | Acceptance Criteria | Test Case(s) |
|-------------|---------------------|--------------|
| Camera captures frames | All cameras capture at ≥5 FPS | PT-CAM-001 |
| Inference runs on DLPU | YOLOv5 inference completes <100ms | UT-LAROD-003 |
| Metadata published to MQTT | ≥99.5% delivery rate | IT-MQTT-002, REL-MQTT-001 |
| Claude analyzes frames | Analysis completes <3s (P95) | PT-CLOUD-002 |
| Alerts generated | Alerts within 1s of detection | PT-CLOUD-004 |
| Multi-camera correlation | Correlation accuracy ≥90% | ST-E2E-002, UT-COORD-001 |

---

### 11.2 Performance Requirements

| Metric | Target | Acceptance Criteria | Test Case(s) |
|--------|--------|---------------------|--------------|
| Inference FPS | ≥5 FPS sustained | P95 ≥5 FPS | PT-CAM-001 |
| Memory usage (72h) | <700 MB | Max <700 MB, no leaks | PT-CAM-002, REL-MEM-001 |
| MQTT delivery rate | ≥99.5% | Delivery ≥99.5% | IT-MQTT-002, REL-MQTT-001 |
| DLPU wait time | <100ms (P95) | P95 <100ms | PT-CAM-003 |
| Frame drop rate | <1% | Drops <1% | PT-CAM-001 |
| End-to-end latency | <5s | P95 <5s | ST-E2E-001 |
| Cloud ingestion rate | ≥100 msg/sec | Sustained ≥100 msg/sec | PT-CLOUD-001 |

---

### 11.3 Reliability Requirements

| Requirement | Acceptance Criteria | Test Case(s) |
|-------------|---------------------|--------------|
| System uptime | ≥99.9% | Uptime ≥99.9% over 30 days | STRESS-002, CHAOS-001 |
| No memory leaks | 0 bytes lost after 72h | Valgrind clean | REL-MEM-001, REL-MEM-002 |
| DLPU scheduler recovery | Lock released <5s after crash | Recovery <5s | REL-DLPU-001 |
| MQTT reconnection | Reconnect <10s | Reconnection <10s | IT-MQTT-003 |
| Time sync tolerance | Handle ≤2s clock drift | Correlation ≥85% with 2s skew | REL-TIME-002 |

---

### 11.4 Scalability Requirements

| Requirement | Acceptance Criteria | Test Case(s) |
|-------------|---------------------|--------------|
| 5 cameras @ 5 FPS | All cameras ≥5 FPS | PT-SCALE-002 |
| 10 cameras (degraded) | All cameras ≥2.5 FPS | PT-SCALE-003 |
| Cloud ingestion | 100 msg/sec for 10 cameras | PT-CLOUD-001 |
| Database queries | P95 <100ms | PT-CLOUD-003 |

---

### 11.5 Security Requirements

| Requirement | Acceptance Criteria | Test Case(s) |
|-------------|---------------------|--------------|
| MQTT authentication | 0 unauthorized connections | SEC-001 |
| SQL injection prevention | 0 successful injections | SEC-003 |
| Credential protection | 0 credentials in logs | SEC-005 |
| TLS encryption | All MQTT traffic encrypted | Manual inspection |

---

## 12. Test Schedule

### Week 1: Unit Tests for Core Components

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon | VDO Stream Handling | UT-VDO-001 to UT-VDO-004 | 18 hours |
| Tue | DLPU Scheduler | UT-DLPU-001 to UT-DLPU-003 | 22 hours |
| Wed | DLPU Scheduler | UT-DLPU-004 to UT-DLPU-005 | 16 hours |
| Thu | Larod Inference | UT-LAROD-001 to UT-LAROD-005 | 23 hours |
| Fri | MQTT Publisher | UT-MQTT-001 to UT-MQTT-004 | 17 hours |

**Deliverables:** Unit test suite with ≥85% code coverage for camera-side C code

---

### Week 2: Cloud-Side Unit Tests & Integration Tests

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon | Bandwidth Controller | UT-BW-001 to UT-BW-003 | 13 hours |
| Tue | Claude Agent | UT-CLAUDE-001 to UT-CLAUDE-003 | 18 hours |
| Wed | System Coordinator | UT-COORD-001 to UT-COORD-002 | 11 hours |
| Thu | Camera + MQTT Integration | IT-MQTT-001 to IT-MQTT-003 | 12 hours |
| Fri | Cloud Integration | IT-CLOUD-001 to IT-CLAUDE-003 | 20 hours |

**Deliverables:**
- Unit test suite with ≥90% coverage for cloud-side Python
- Integration test suite for MQTT and Cloud services

---

### Week 3: System Tests & DLPU Integration

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon | DLPU Multi-Camera Tests | IT-DLPU-001 to IT-DLPU-003 | 19 hours |
| Tue | E2E Happy Path | ST-E2E-001 to ST-E2E-002 | 18 hours |
| Wed | Degraded Network Tests | ST-NET-001 to ST-NET-004 | 19 hours |
| Thu | Resource Constraints | ST-RES-001 to ST-RES-004 | 21 hours |
| Fri | Failure Scenarios | ST-FAIL-001 to ST-FAIL-005 | 22 hours |

**Deliverables:** System test suite covering all critical scenarios

---

### Week 4: Performance & Stress Tests

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon | Camera-Side Performance | PT-CAM-001, PT-CAM-003, PT-CAM-004 | 10 hours |
| Tue | Cloud-Side Performance | PT-CLOUD-001 to PT-CLOUD-004 | 17 hours |
| Wed | Multi-Camera Scaling | PT-SCALE-001 to PT-SCALE-003 | 12 hours |
| Thu | Stress Tests | STRESS-001, STRESS-003, STRESS-004 | 19 hours |
| Fri | Stress Tests | STRESS-002 (start 24h run), STRESS-005 | 8 hours |

**Deliverables:** Performance benchmarks and stress test results

---

### Week 5: 72-Hour Reliability Test

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon AM | Setup & Start | REL-MEM-001, REL-MEM-002 (start 72h) | 4 hours |
| Mon-Wed | Monitoring | Monitor 72h test, check metrics | 4 hours/day |
| Thu | Analysis | Analyze 72h test results | 8 hours |
| Fri | DLPU & Time Sync | REL-DLPU-001 to REL-TIME-003 | 24 hours |

**Deliverables:** 72-hour reliability test report, memory leak analysis

---

### Week 6: Security & Chaos Testing

| Day | Focus Area | Test Cases | Effort |
|-----|------------|------------|--------|
| Mon | Security Tests | SEC-001 to SEC-005 | 19 hours |
| Tue | Chaos Engineering | CHAOS-001 (start 24h), CHAOS-002 | 10 hours |
| Wed | Chaos Engineering | CHAOS-003 to CHAOS-005 | 18 hours |
| Thu | Final Verification | Re-run failed tests, regression checks | 8 hours |
| Fri | Test Report | Generate final test report | 8 hours |

**Deliverables:**
- Security test report
- Chaos engineering results
- Final comprehensive test report

---

### Week 7 (Optional): Production Readiness

| Day | Focus Area | Activities |
|-----|------------|------------|
| Mon | Production-Like Testing | Deploy to production-like environment |
| Tue | Load Testing | Simulate production load (10 cameras) |
| Wed | Soak Testing | 7-day reliability test (start) |
| Thu | Performance Tuning | Optimize based on test results |
| Fri | Documentation | Finalize test documentation |

---

## Risk-Based Testing Matrix

### Critical Risks (MUST TEST)

| Risk ID | Risk Description | Severity | Test Cases | Priority |
|---------|------------------|----------|------------|----------|
| RISK-001 | Memory leaks in VDO buffer handling | 🔴 Critical | UT-VDO-001, REL-MEM-001, REL-MEM-003 | P0 |
| RISK-002 | DLPU scheduler deadlocks | 🔴 Critical | UT-DLPU-003, UT-DLPU-005, REL-DLPU-001 | P0 |
| RISK-003 | MQTT message loss >0.5% | 🔴 Critical | IT-MQTT-002, REL-MQTT-001, REL-MQTT-003 | P0 |
| RISK-004 | Memory usage >700MB | 🔴 Critical | PT-CAM-002, REL-MEM-002 | P0 |
| RISK-005 | DLPU scheduler process crash | 🔴 Critical | REL-DLPU-001, REL-DLPU-002 | P0 |

---

### High Risks (SHOULD TEST)

| Risk ID | Risk Description | Severity | Test Cases | Priority |
|---------|------------------|----------|------------|----------|
| RISK-006 | Time drift >2s breaks correlation | 🟡 High | REL-TIME-002, ST-FAIL-005 | P1 |
| RISK-007 | Network saturation drops metadata | 🟡 High | ST-NET-003, ST-RES-004 | P1 |
| RISK-008 | Claude API rate limits | 🟡 High | IT-CLAUDE-001, STRESS-005 | P1 |
| RISK-009 | Camera reboot loses state | 🟡 High | ST-FAIL-001 | P1 |
| RISK-010 | DLPU contention with 10+ cameras | 🟡 High | PT-SCALE-003, STRESS-001 | P1 |

---

### Medium Risks (COULD TEST)

| Risk ID | Risk Description | Severity | Test Cases | Priority |
|---------|------------------|----------|------------|----------|
| RISK-011 | Database connection pool exhausted | 🟢 Medium | STRESS-004 | P2 |
| RISK-012 | Clock skew between cameras | 🟢 Medium | REL-TIME-001 | P2 |
| RISK-013 | MQTT topic injection | 🟢 Medium | SEC-002 | P2 |
| RISK-014 | SQL injection in queries | 🟢 Medium | SEC-003 | P2 |

---

## Appendix A: Critical Gotchas Test Coverage

Based on the Axis I.S. implementation guide, these critical gotchas MUST be tested:

### Gotcha 1: VDO Buffer Reference Counting
- **Test Coverage:** UT-VDO-001, REL-MEM-003
- **Why Critical:** Unreferenced buffers cause memory leaks
- **Verification:** Valgrind must show 0 leaks after 72 hours

### Gotcha 2: DLPU Shared Memory Scheduler
- **Test Coverage:** UT-DLPU-003, IT-DLPU-001, REL-DLPU-001
- **Why Critical:** Race conditions can deadlock entire system
- **Verification:** ThreadSanitizer clean, no deadlocks under stress

### Gotcha 3: MQTT Message Delivery
- **Test Coverage:** IT-MQTT-002, REL-MQTT-001, REL-MQTT-003
- **Why Critical:** Message loss breaks event detection
- **Verification:** ≥99.5% delivery under all network conditions

### Gotcha 4: Time Synchronization
- **Test Coverage:** REL-TIME-001, REL-TIME-002, ST-FAIL-005
- **Why Critical:** Clock drift breaks multi-camera correlation
- **Verification:** Correlation ≥85% with 2s skew

### Gotcha 5: Memory Usage Over Time
- **Test Coverage:** PT-CAM-002, REL-MEM-001, REL-MEM-002
- **Why Critical:** Memory leaks cause OOM after hours/days
- **Verification:** Memory <700MB and flat trend after 72 hours

---

## Appendix B: Test Tools and Frameworks

### Camera-Side (C) Testing
- **Unity** - C unit testing framework
- **Valgrind** - Memory leak detection
- **GDB** - Debugger for crash analysis
- **ThreadSanitizer** - Race condition detection
- **Helgrind** - Deadlock detection
- **smem** - Memory profiling
- **stress-ng** - CPU/memory stress testing

### Cloud-Side (Python) Testing
- **pytest** - Python testing framework
- **pytest-cov** - Code coverage
- **hypothesis** - Property-based testing
- **unittest.mock** - Mocking framework
- **locust** - Load testing
- **pytest-benchmark** - Performance benchmarking

### Network Testing
- **tc (traffic control)** - Network emulation
- **iperf3** - Bandwidth testing
- **tcpdump** - Packet capture
- **wireshark** - Protocol analysis

### System Testing
- **Docker Compose** - Test environment orchestration
- **Mosquitto** - MQTT broker
- **MQTT Explorer** - MQTT debugging
- **PostgreSQL** - Database
- **Redis** - State storage

### Monitoring
- **Grafana** - Visualization
- **Prometheus** - Metrics collection
- **Loki** - Log aggregation
- **htop** - Process monitoring
- **iftop** - Network monitoring

---

## Appendix C: Test Data Repository Structure

```
tests/
├── unit/
│   ├── camera/
│   │   ├── test_vdo_handling.c
│   │   ├── test_dlpu_scheduler.c
│   │   ├── test_larod_inference.c
│   │   └── test_mqtt_publisher.c
│   └── cloud/
│       ├── test_bandwidth_controller.py
│       ├── test_claude_agent.py
│       └── test_coordinator.py
├── integration/
│   ├── test_mqtt_integration.py
│   ├── test_cloud_integration.py
│   └── test_dlpu_coordination.py
├── system/
│   ├── test_e2e_scenarios.py
│   ├── test_degraded_network.py
│   ├── test_resource_constraints.py
│   └── test_failure_scenarios.py
├── performance/
│   ├── test_camera_performance.py
│   ├── test_cloud_performance.py
│   └── test_scaling.py
├── stress/
│   ├── test_oversubscription.py
│   ├── test_saturation.py
│   └── test_rate_limits.py
├── reliability/
│   ├── test_memory_leaks.py
│   ├── test_mqtt_reliability.py
│   ├── test_time_sync.py
│   └── test_dlpu_robustness.py
├── security/
│   ├── test_authentication.py
│   ├── test_injection.py
│   └── test_credentials.py
├── chaos/
│   ├── test_random_failures.py
│   ├── test_network_chaos.py
│   └── test_corruption.py
├── fixtures/
│   ├── test_images/
│   ├── test_metadata/
│   └── test_configs/
└── mocks/
    ├── mock_camera.py
    ├── mock_mqtt.py
    └── mock_claude_api.py
```

---

## Appendix D: Test Execution Checklist

### Pre-Test Checklist
- [ ] All test environments provisioned
- [ ] Test data sets prepared
- [ ] Baseline performance metrics established
- [ ] Monitoring dashboards configured
- [ ] Test automation pipeline configured
- [ ] Team trained on test procedures

### During Test Execution
- [ ] Daily standup to review test progress
- [ ] Log all test failures with reproduction steps
- [ ] Monitor test environment health
- [ ] Collect performance metrics continuously
- [ ] Update test documentation with findings

### Post-Test Checklist
- [ ] Analyze test results
- [ ] Generate test report
- [ ] Log all defects in issue tracker
- [ ] Update acceptance criteria based on findings
- [ ] Archive test artifacts (logs, metrics, reports)
- [ ] Conduct test retrospective

---

## Appendix E: Defect Severity Classification

### Severity 1 (Critical) - Production Blocker
- System crash or data loss
- Memory leak causing OOM
- DLPU deadlock
- Security vulnerability
- **Example:** VDO buffer not unreferenced, causing memory leak

### Severity 2 (High) - Major Functionality Broken
- Feature not working as specified
- Performance degradation >20%
- Intermittent failures >5%
- **Example:** MQTT delivery rate <99%

### Severity 3 (Medium) - Minor Functionality Issue
- Feature works but with limitations
- Performance degradation 10-20%
- Rare edge case failures
- **Example:** Correlation accuracy 80% (target 90%)

### Severity 4 (Low) - Cosmetic/Enhancement
- UI issues
- Log message formatting
- Documentation errors
- **Example:** Typo in log message

---

**End of Test Plan**

**Document Revision History:**
- v1.0 - November 23, 2025 - Initial comprehensive test plan

**Approval:**
- QA Architect: _____________________ Date: _______
- Engineering Lead: _________________ Date: _______
- Product Owner: ____________________ Date: _______
