# AXION Platform - Planning Phase Complete
**Date:** November 23, 2025  
**Status:** ✅ All Planning Documents Complete

---

## Executive Summary

All detailed planning for the AXION platform has been completed by specialized sub-agents. We now have comprehensive specifications ready for implementation.

### Planning Documents Created

| Document | Status | Lines | Description |
|----------|--------|-------|-------------|
| **AXION_ARCHITECTURE_DIAGRAMS.md** | ✅ Complete | 800+ | System overview, component diagrams, state machines |
| **AXION_DATABASE_SCHEMAS.md** | ✅ Complete | 1,200+ | PostgreSQL + Redis schemas, migrations, queries |
| **AXION_API_SPECIFICATIONS.md** | ✅ Complete | 2,800+ | REST APIs, MQTT schemas, OpenAPI specs |
| **AXION_TEST_PLAN.md** | ✅ Complete | 2,000+ | 157 test cases, 6-week schedule, automation |
| **REPOSITORY_ANALYSIS.md** | ✅ Complete | 800+ | make_acap template analysis |
| **AXIS_REPOS_ANALYSIS.md** | ✅ Complete | 1,500+ | Axis official repos review |

**Total Planning Documentation:** 9,100+ lines of comprehensive specifications

---

## Architecture Overview

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    CAMERA NETWORK (Edge)                     │
├─────────────────────────────────────────────────────────────┤
│  Camera 1-10 (ARTPEC-9)                                     │
│  ├─ VDO Stream (1080p → 416x416)                           │
│  ├─ DLPU Scheduler (Time-division multiplexing)            │
│  ├─ Larod Inference (YOLOv5n INT8)                         │
│  ├─ Metadata Extraction (Motion, Objects, Scene Hash)      │
│  └─ MQTT Publisher (10 FPS metadata stream)                │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ MQTT (QoS 1)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  CLOUD INFRASTRUCTURE                        │
├─────────────────────────────────────────────────────────────┤
│  MQTT Broker (Mosquitto)                                    │
│  ├─ Message Routing                                         │
│  └─ Topic Filtering                                         │
│                                                              │
│  Bandwidth Controller                                       │
│  ├─ Network Quality Monitoring                              │
│  ├─ Dynamic Quality Adjustment                              │
│  └─ Priority-Based Allocation                               │
│                                                              │
│  Claude Agent                                               │
│  ├─ Scene Memory (Last 30 frames/camera)                   │
│  ├─ Frame Request Optimizer                                 │
│  ├─ Pattern Detection                                       │
│  └─ Alert Generation                                        │
│                                                              │
│  System Coordinator                                         │
│  ├─ Multi-Camera Correlation                                │
│  ├─ Time Synchronization                                    │
│  └─ Cross-Camera Pattern Detection                          │
│                                                              │
│  Data Layer                                                 │
│  ├─ PostgreSQL (Events, Analyses, Alerts)                  │
│  └─ Redis (Real-time State, Caches, Queues)                │
└─────────────────────────────────────────────────────────────┘
```

### Technology Stack

**Camera-Side (C/ACAP):**
- ACAP SDK 12.2.0
- VDO API (video capture)
- Larod API (DLPU inference)
- Paho MQTT C client
- YOLOv5n INT8 TFLite model (1.0 MB)
- GLib main loop

**Cloud-Side (Python):**
- Python 3.11
- FastAPI (REST APIs)
- aiomqtt (MQTT client)
- asyncpg (PostgreSQL)
- aioredis (Redis)
- Anthropic Claude API
- Docker Compose

**Infrastructure:**
- Mosquitto MQTT Broker
- PostgreSQL 15 (events, analyses)
- Redis 7 (state, caches)
- Grafana (monitoring)
- Nginx (reverse proxy)

---

## Database Design

### PostgreSQL Schema (6 Tables)

**camera_events** - High-frequency event storage (8.6M rows/day)
- Partitioned by day
- Indexes: (camera_id, timestamp), (event_type, timestamp)
- 30-day retention

**claude_analyses** - AI analysis results
- Links to trigger events
- JSONB payload for full Claude response
- Full-text search on insights

**alerts** - Security notifications
- Severity levels (1-5)
- Multi-channel notification tracking
- User acknowledgment workflow

**frame_metadata** - Frame deduplication
- Multiple hash types
- Quality scoring
- 7-day retention

**system_metrics** - Performance monitoring
- Hourly partitions
- Pre-aggregated rollups
- P95/P99 percentiles

**audit_log** - Immutable audit trail
- Monthly partitions
- 1-year retention

### Redis Data Structures (7 Types)

1. **Camera State** - Hash per camera (current FPS, motion, queue depth)
2. **DLPU Scheduler** - Distributed locks + time slots
3. **Bandwidth Allocations** - Per-camera quality levels
4. **Scene Memory** - Last 30 frames per camera (sorted sets)
5. **Message Queues** - Failed MQTT messages with retry
6. **Rate Limiting** - Claude API call tracking
7. **Prepared Context** - Claude analysis cache (10-min TTL)

---

## API Specifications

### Camera ACAP HTTP API (7 Endpoints)

```
GET  /app                - Application info
GET  /settings           - Configuration
POST /settings           - Update config
GET  /status             - Health metrics
GET  /metadata           - Latest frame metadata
GET  /frame/{timestamp}  - Specific frame retrieval
POST /control            - Control commands
```

### MQTT Topics (9 Message Types)

**Camera → Cloud:**
```
axion/camera/{id}/metadata   - 10 FPS stream (QoS 0)
axion/camera/{id}/event      - Significant events (QoS 1)
axion/camera/{id}/alert      - Critical alerts (QoS 2)
axion/camera/{id}/status     - 60s health updates (QoS 1)
axion/camera/{id}/heartbeat  - 30s keepalive (QoS 0)
```

**Cloud → Camera:**
```
axion/camera/{id}/config         - Config updates (QoS 1)
axion/camera/{id}/dlpu_schedule  - Time slot assignment (QoS 1)
axion/camera/{id}/frame_request  - Frame request (QoS 1)
axion/camera/{id}/command        - Control (QoS 1)
```

### Cloud Services REST API (20+ Endpoints)

**Categories:**
- Camera Management (CRUD operations)
- Analysis Queries (Claude results)
- Alert Management (notifications)
- System Monitoring (metrics, health)
- Configuration (system-wide settings)

**Authentication:**
- JWT tokens for user sessions
- API keys for service-to-service
- mTLS for camera connections

---

## Test Plan

### Test Coverage (157 Test Cases)

**Unit Tests (40 cases)**
- VDO buffer handling
- DLPU scheduler algorithms
- Larod inference
- MQTT publish/retry logic
- Bandwidth controller
- Claude agent components

**Integration Tests (20 cases)**
- Camera ↔ MQTT Broker
- MQTT ↔ Cloud Services
- Cloud ↔ Claude API
- Multi-camera DLPU coordination

**System Tests (25 cases)**
- Happy path end-to-end
- Degraded network scenarios
- Resource constraint handling
- Failure scenario recovery

**Performance Tests (15 cases)**
- Inference FPS benchmarks
- Memory usage over 72 hours
- DLPU wait time measurements
- MQTT throughput testing

**Stress Tests (5 cases)**
- 20 cameras (2x normal)
- 100% DLPU utilization
- Network saturation
- Database connection exhaustion

**Reliability Tests (Critical)**
- 72-hour memory leak detection (Valgrind)
- MQTT delivery verification (99.5% target)
- Time synchronization drift
- DLPU scheduler crash recovery

**Security Tests (10 cases)**
- Authentication bypass attempts
- MQTT topic injection
- API rate limit testing
- Credential exposure checks

**Chaos Engineering (12 cases)**
- Random container kills
- Network packet drops
- Message corruption
- Latency injection

### 6-Week Test Schedule

- **Week 1:** Camera-side unit tests
- **Week 2:** Cloud-side unit tests + integration
- **Week 3:** System tests + failure scenarios
- **Week 4:** Performance + stress testing
- **Week 5:** 72-hour reliability test ⚠️ CRITICAL
- **Week 6:** Security + chaos testing

### Acceptance Criteria

| Metric | Target | Critical |
|--------|--------|----------|
| Inference FPS | ≥5 FPS all cameras | ✅ |
| Memory Usage | <700 MB after 72h | ✅ |
| MQTT Delivery | ≥99.5% | ✅ |
| DLPU Wait Time | <100ms (P95) | ✅ |
| Frame Drop Rate | <1% | ✅ |
| System Uptime | ≥99.9% | ✅ |

---

## Implementation Readiness

### What We Have (Ready to Build)

✅ **Complete Architecture**
- System design finalized
- Component interactions defined
- Data flows documented
- Failure modes identified

✅ **Database Schemas**
- PostgreSQL DDL ready
- Redis structures defined
- Migration scripts prepared
- Query patterns optimized

✅ **API Specifications**
- OpenAPI 3.0 specs
- MQTT message schemas
- Authentication strategy
- Error handling standards

✅ **Test Strategy**
- 157 test cases defined
- Test data requirements
- Automation plan
- Success criteria

✅ **Reusable Code Patterns**
- VDO streaming (from Axis repos)
- Larod inference (from Axis repos)
- MQTT client (from make_acap)
- Docker build (from Axis repos)

### What We Must Build (Custom Code)

❌ **Camera-Side (ACAP)**
- DLPU shared memory scheduler
- Metadata extraction logic
- Scene hash calculation
- Motion scoring algorithm
- High-frequency MQTT streaming

❌ **Cloud-Side (Python)**
- Claude Agent integration
- Adaptive bandwidth controller
- Scene memory system
- Frame request optimizer
- Multi-camera coordinator

❌ **Infrastructure**
- Docker Compose stack
- Grafana dashboards
- Deployment automation
- Monitoring alerts

---

## Risk Assessment

### High-Risk Items (Require POC Validation)

| Risk | Severity | Mitigation | POC Test |
|------|----------|------------|----------|
| DLPU contention | 🔴 Critical | Shared memory scheduler | 3 cameras simultaneously |
| Memory leaks | 🔴 Critical | Rigorous cleanup, Valgrind | 24-hour test |
| MQTT reliability | 🟡 Medium | Retry logic, sequence numbers | Simulate packet loss |
| Network saturation | 🟡 Medium | Bandwidth controller | 10 cameras full quality |
| Time sync drift | 🟡 Medium | NTP + correlation | Clock skew test |

### Success Factors

**Technical:**
- DLPU scheduler prevents contention
- Memory stays under 700 MB
- MQTT delivery >99.5%
- Inference maintains 5 FPS

**Operational:**
- Automated deployment
- Comprehensive monitoring
- 72-hour stability test passes
- Security audit passes

---

## Next Steps: Option 3 (Proof of Concept)

### POC Objectives

Build minimal viable camera app to validate:
1. ✅ VDO streaming + Larod inference works
2. ✅ YOLOv5n achieves 5-10 FPS on ARTPEC-9
3. ✅ Metadata extraction <100ms total
4. ✅ MQTT publishing at 10 FPS sustained
5. ✅ Memory usage acceptable over 24 hours
6. ⚠️ DLPU scheduler prevents contention (3 cameras)

### POC Scope (Minimal)

**Camera-Side:**
- VDO stream setup (416x416)
- Larod inference with YOLOv5n INT8
- Basic metadata extraction (object count, motion score)
- MQTT publish (no retry logic yet)
- Simple DLPU coordination (test with 2-3 cameras)

**Cloud-Side:**
- MQTT subscriber (print messages)
- Basic metrics logging
- No Claude integration yet
- No bandwidth controller yet

**Infrastructure:**
- Mosquitto broker only
- No databases yet
- Manual deployment

### POC Success Criteria

- [ ] Build ACAP successfully
- [ ] Deploy to real camera
- [ ] Achieve 5 FPS inference
- [ ] Memory <300 MB after 24 hours
- [ ] MQTT messages received reliably
- [ ] 3 cameras don't block each other

### POC Timeline

**Week 1:**
- Day 1-2: Set up dev environment, clone repos
- Day 3-4: Create AXION ACAP based on CV SDK example
- Day 5: Build and deploy to first camera

**Week 2:**
- Day 1-2: Test VDO + Larod integration
- Day 3: Add MQTT publishing
- Day 4: Basic DLPU coordination
- Day 5: Deploy to 3 cameras, test contention

**Week 3:**
- Day 1-3: 24-hour stability test
- Day 4-5: Document findings, adjust architecture if needed

### POC Deliverables

1. Working AXION ACAP (.eap file)
2. Deployment instructions
3. Performance measurements
4. Memory profiling results
5. Lessons learned document
6. Updated implementation plan

---

## Planning Phase Complete ✅

All planning documents are ready. We have:

- **9,100+ lines** of detailed specifications
- **6 architecture diagrams** with Mermaid visuals
- **Complete database schemas** ready to deploy
- **2,800+ lines** of API specifications
- **157 test cases** with 6-week schedule
- **Risk assessment** with mitigation strategies

**Status:** Ready to proceed to Option 3 (Proof of Concept)

---

## File Manifest

### Planning Documents (Current Directory)
```
AXION_ARCHITECTURE_DIAGRAMS.md      - System architecture
AXION_DATABASE_SCHEMAS.md           - PostgreSQL + Redis
AXION_API_SPECIFICATIONS.md         - REST + MQTT APIs
AXION_TEST_PLAN.md                  - 157 test cases
REPOSITORY_ANALYSIS.md              - make_acap analysis
AXIS_REPOS_ANALYSIS.md              - Axis repos review
AXION_PLANNING_SUMMARY.md           - This document
```

### Reference Documents (Provided)
```
AXION_CRITICAL_IMPLEMENTATION_GUIDE.md  - Critical gotchas
AXION_Integration_Guide.md              - DLPU + Bandwidth guide
axion_claude_integration.py             - Python reference
axion_complete_deployment.sh            - Deployment script
```

### Next Steps
1. Review this summary
2. Approve POC scope
3. Begin Option 3 implementation

---

**Planning Phase Duration:** 2 hours (parallel sub-agents)  
**Ready for:** Proof of Concept implementation  
**Confidence Level:** High (comprehensive specifications complete)
