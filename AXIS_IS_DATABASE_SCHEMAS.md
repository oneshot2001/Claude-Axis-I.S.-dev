# Axis I.S. Database Schemas

**Version:** 1.0.0
**Date:** 2025-11-23
**Target Load:** 10 cameras x 10 FPS = 100 writes/second
**Database Stack:** PostgreSQL 15+ (events) + Redis 7+ (state)

---

## Table of Contents

1. [PostgreSQL Schema](#1-postgresql-schema)
2. [Redis Schema](#2-redis-schema)
3. [Time-Series Optimizations](#3-time-series-optimizations)
4. [Migration Scripts](#4-migration-scripts)
5. [Performance Tuning](#5-performance-tuning)
6. [Query Patterns](#6-query-patterns)
7. [Backup & Restore](#7-backup--restore)

---

## 1. PostgreSQL Schema

### Overview

PostgreSQL serves as the persistent event store for Axis I.S., handling:
- Long-term event storage (motion, detections, alerts)
- Claude analysis results and insights
- System audit logs and metrics
- Frame metadata for historical queries

### 1.1 camera_events

**Purpose:** Primary event storage for all camera-generated events (motion, object detection, scene changes)

```sql
-- ============================================================================
-- TABLE: camera_events
-- Purpose: High-frequency event storage with time-series partitioning
-- Write Rate: ~100 events/second (10 cameras x 10 FPS)
-- Retention: 30 days hot, 90 days warm, 1 year cold
-- ============================================================================

CREATE TABLE camera_events (
    -- Primary Key
    event_id BIGSERIAL NOT NULL,

    -- Event Identification
    camera_id VARCHAR(64) NOT NULL,
    event_type VARCHAR(32) NOT NULL, -- 'motion', 'object_detected', 'scene_change'
    event_timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- Frame Information
    frame_number BIGINT NOT NULL,
    frame_hash VARCHAR(64), -- MD5/SHA256 hash for deduplication
    fps_actual DECIMAL(5,2),

    -- Motion Detection Data
    motion_score DECIMAL(5,4), -- 0.0000 to 1.0000
    motion_regions JSONB, -- Array of motion region coordinates

    -- Object Detection Data (from YOLO)
    objects_detected JSONB, -- Array of {class, confidence, bbox, track_id}
    object_count INTEGER DEFAULT 0,
    max_object_confidence DECIMAL(5,4),

    -- Scene Analysis
    scene_hash VARCHAR(64), -- Perceptual hash for scene similarity
    scene_change_score DECIMAL(5,4), -- 0.0000 to 1.0000

    -- Quality Metrics
    image_quality_score DECIMAL(5,4), -- Motion blur, exposure, etc.
    bandwidth_usage_bytes INTEGER,

    -- Processing Metadata
    dlpu_inference_ms INTEGER, -- Time spent on DLPU inference
    total_processing_ms INTEGER, -- End-to-end processing time

    -- Status Flags
    sent_to_claude BOOLEAN DEFAULT FALSE,
    claude_analysis_id BIGINT, -- FK to claude_analyses
    alert_generated BOOLEAN DEFAULT FALSE,

    -- Timestamps
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ,

    -- Constraints
    CONSTRAINT pk_camera_events PRIMARY KEY (event_id, event_timestamp),
    CONSTRAINT chk_event_type CHECK (event_type IN ('motion', 'object_detected', 'scene_change', 'manual_trigger')),
    CONSTRAINT chk_motion_score CHECK (motion_score IS NULL OR (motion_score >= 0 AND motion_score <= 1)),
    CONSTRAINT chk_scene_change_score CHECK (scene_change_score IS NULL OR (scene_change_score >= 0 AND scene_change_score <= 1))
) PARTITION BY RANGE (event_timestamp);

-- Create indexes on parent table
CREATE INDEX idx_camera_events_camera_time ON camera_events(camera_id, event_timestamp DESC);
CREATE INDEX idx_camera_events_type_time ON camera_events(event_type, event_timestamp DESC);
CREATE INDEX idx_camera_events_frame_hash ON camera_events(frame_hash) WHERE frame_hash IS NOT NULL;
CREATE INDEX idx_camera_events_scene_hash ON camera_events(scene_hash) WHERE scene_hash IS NOT NULL;
CREATE INDEX idx_camera_events_claude_pending ON camera_events(event_timestamp DESC) WHERE sent_to_claude = FALSE;
CREATE INDEX idx_camera_events_objects_gin ON camera_events USING GIN (objects_detected jsonb_path_ops);

-- ============================================================================
-- PARTITIONING STRATEGY: Daily partitions for 30 days
-- ============================================================================

-- Create function to automatically create partitions
CREATE OR REPLACE FUNCTION create_camera_events_partition(partition_date DATE)
RETURNS void AS $$
DECLARE
    partition_name TEXT;
    start_date TEXT;
    end_date TEXT;
BEGIN
    partition_name := 'camera_events_' || TO_CHAR(partition_date, 'YYYY_MM_DD');
    start_date := partition_date::TEXT;
    end_date := (partition_date + INTERVAL '1 day')::TEXT;

    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS %I PARTITION OF camera_events
         FOR VALUES FROM (%L) TO (%L)',
        partition_name, start_date, end_date
    );

    -- Create partition-specific indexes
    EXECUTE format('CREATE INDEX IF NOT EXISTS %I ON %I(camera_id, event_timestamp DESC)',
        partition_name || '_camera_time_idx', partition_name);
    EXECUTE format('CREATE INDEX IF NOT EXISTS %I ON %I(event_type)',
        partition_name || '_type_idx', partition_name);
END;
$$ LANGUAGE plpgsql;

-- Create partitions for next 7 days (run daily via cron)
DO $$
DECLARE
    partition_date DATE;
BEGIN
    FOR i IN 0..6 LOOP
        partition_date := CURRENT_DATE + (i || ' days')::INTERVAL;
        PERFORM create_camera_events_partition(partition_date);
    END LOOP;
END $$;

-- ============================================================================
-- RETENTION POLICY: Automated partition management
-- ============================================================================

CREATE OR REPLACE FUNCTION cleanup_old_partitions()
RETURNS void AS $$
DECLARE
    partition_record RECORD;
    partition_date DATE;
    cutoff_date DATE := CURRENT_DATE - INTERVAL '30 days';
BEGIN
    FOR partition_record IN
        SELECT tablename FROM pg_tables
        WHERE schemaname = 'public'
        AND tablename LIKE 'camera_events_20%'
    LOOP
        -- Extract date from partition name (camera_events_YYYY_MM_DD)
        partition_date := TO_DATE(
            SUBSTRING(partition_record.tablename FROM 15),
            'YYYY_MM_DD'
        );

        IF partition_date < cutoff_date THEN
            -- Option 1: Drop partition (data loss)
            -- EXECUTE format('DROP TABLE IF EXISTS %I', partition_record.tablename);

            -- Option 2: Detach and archive (recommended)
            EXECUTE format('ALTER TABLE camera_events DETACH PARTITION %I', partition_record.tablename);
            EXECUTE format('ALTER TABLE %I SET SCHEMA archive', partition_record.tablename);

            RAISE NOTICE 'Archived partition: %', partition_record.tablename;
        END IF;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- Create archive schema
CREATE SCHEMA IF NOT EXISTS archive;

-- Schedule cleanup (add to pg_cron or external scheduler)
-- SELECT cron.schedule('cleanup-partitions', '0 2 * * *', 'SELECT cleanup_old_partitions()');
```

---

### 1.2 claude_analyses

**Purpose:** Store Claude's interpretations, insights, and reasoning about camera events

```sql
-- ============================================================================
-- TABLE: claude_analyses
-- Purpose: Claude AI analysis results and contextual insights
-- Write Rate: ~1-5 analyses/second (selective frame analysis)
-- Retention: 90 days
-- ============================================================================

CREATE TABLE claude_analyses (
    -- Primary Key
    analysis_id BIGSERIAL PRIMARY KEY,

    -- Associated Event(s)
    trigger_event_id BIGINT NOT NULL,
    camera_id VARCHAR(64) NOT NULL,
    event_timestamp TIMESTAMPTZ NOT NULL,

    -- Request Information
    request_type VARCHAR(32) NOT NULL, -- 'scene_description', 'anomaly_detection', 'pattern_analysis'
    prompt_template VARCHAR(64), -- Which prompt was used
    context_window_events INTEGER, -- Number of previous events included

    -- Claude Response
    claude_response JSONB NOT NULL, -- Full API response
    primary_insight TEXT, -- Main takeaway (extracted from response)
    confidence_score DECIMAL(5,4), -- Claude's confidence (if provided)

    -- Extracted Analysis
    objects_identified TEXT[], -- Array of objects Claude identified
    activities_detected TEXT[], -- Array of activities/behaviors
    anomalies_flagged TEXT[], -- Array of anomalies/concerns
    scene_description TEXT, -- Natural language description
    risk_assessment VARCHAR(16), -- 'none', 'low', 'medium', 'high', 'critical'

    -- Pattern Detection
    pattern_detected BOOLEAN DEFAULT FALSE,
    pattern_type VARCHAR(64), -- 'loitering', 'unusual_traffic', 'crowd_formation'
    pattern_confidence DECIMAL(5,4),

    -- API Metrics
    api_call_duration_ms INTEGER,
    tokens_used INTEGER,
    model_version VARCHAR(32), -- 'claude-sonnet-4-5-20250929'

    -- Alert Generation
    should_alert BOOLEAN DEFAULT FALSE,
    alert_reason TEXT,
    alert_severity INTEGER, -- 1-5 scale

    -- Timestamps
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- Constraints
    CONSTRAINT chk_request_type CHECK (request_type IN ('scene_description', 'anomaly_detection', 'pattern_analysis', 'real_time_query')),
    CONSTRAINT chk_risk_level CHECK (risk_assessment IN ('none', 'low', 'medium', 'high', 'critical'))
);

-- Indexes
CREATE INDEX idx_claude_analyses_event ON claude_analyses(trigger_event_id);
CREATE INDEX idx_claude_analyses_camera_time ON claude_analyses(camera_id, event_timestamp DESC);
CREATE INDEX idx_claude_analyses_risk ON claude_analyses(risk_assessment) WHERE risk_assessment != 'none';
CREATE INDEX idx_claude_analyses_patterns ON claude_analyses(pattern_type) WHERE pattern_detected = TRUE;
CREATE INDEX idx_claude_analyses_alerts ON claude_analyses(created_at DESC) WHERE should_alert = TRUE;
CREATE INDEX idx_claude_analyses_response_gin ON claude_analyses USING GIN (claude_response jsonb_path_ops);

-- Full-text search on insights
CREATE INDEX idx_claude_analyses_insight_fts ON claude_analyses USING GIN (to_tsvector('english', primary_insight));
CREATE INDEX idx_claude_analyses_description_fts ON claude_analyses USING GIN (to_tsvector('english', scene_description));

-- Foreign key to camera_events (cross-partition, optional)
-- ALTER TABLE claude_analyses ADD CONSTRAINT fk_claude_analyses_event
--     FOREIGN KEY (trigger_event_id) REFERENCES camera_events(event_id) ON DELETE CASCADE;
```

---

### 1.3 alerts

**Purpose:** Security alerts and notifications generated by the system

```sql
-- ============================================================================
-- TABLE: alerts
-- Purpose: Consolidated alert management and notification tracking
-- Write Rate: ~1-10 alerts/minute (varies by activity)
-- Retention: 1 year
-- ============================================================================

CREATE TABLE alerts (
    -- Primary Key
    alert_id BIGSERIAL PRIMARY KEY,

    -- Alert Classification
    alert_type VARCHAR(32) NOT NULL, -- 'motion', 'object', 'pattern', 'anomaly', 'system'
    severity INTEGER NOT NULL, -- 1=info, 2=low, 3=medium, 4=high, 5=critical
    priority INTEGER NOT NULL DEFAULT 3, -- 1-5, affects notification routing

    -- Source Information
    camera_id VARCHAR(64),
    trigger_event_id BIGINT,
    claude_analysis_id BIGINT,

    -- Alert Content
    title VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    alert_data JSONB, -- Structured data (bboxes, scores, metadata)

    -- Detection Details
    detected_objects TEXT[], -- Objects that triggered alert
    confidence_score DECIMAL(5,4),
    frame_references BIGINT[], -- Array of relevant frame IDs

    -- Notification Status
    status VARCHAR(16) NOT NULL DEFAULT 'new', -- 'new', 'acknowledged', 'investigating', 'resolved', 'false_positive'
    notification_sent BOOLEAN DEFAULT FALSE,
    notification_channels TEXT[], -- ['email', 'sms', 'webhook', 'mobile_push']
    notification_sent_at TIMESTAMPTZ,

    -- User Actions
    acknowledged_by VARCHAR(64),
    acknowledged_at TIMESTAMPTZ,
    resolved_by VARCHAR(64),
    resolved_at TIMESTAMPTZ,
    resolution_notes TEXT,

    -- Escalation
    escalated BOOLEAN DEFAULT FALSE,
    escalated_at TIMESTAMPTZ,
    escalation_level INTEGER,

    -- Timestamps
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ,

    -- Constraints
    CONSTRAINT chk_severity CHECK (severity BETWEEN 1 AND 5),
    CONSTRAINT chk_priority CHECK (priority BETWEEN 1 AND 5),
    CONSTRAINT chk_status CHECK (status IN ('new', 'acknowledged', 'investigating', 'resolved', 'false_positive', 'dismissed'))
);

-- Indexes
CREATE INDEX idx_alerts_status_severity ON alerts(status, severity DESC, created_at DESC);
CREATE INDEX idx_alerts_camera_time ON alerts(camera_id, created_at DESC);
CREATE INDEX idx_alerts_unresolved ON alerts(created_at DESC) WHERE status NOT IN ('resolved', 'false_positive', 'dismissed');
CREATE INDEX idx_alerts_high_priority ON alerts(created_at DESC) WHERE severity >= 4;
CREATE INDEX idx_alerts_event ON alerts(trigger_event_id) WHERE trigger_event_id IS NOT NULL;
CREATE INDEX idx_alerts_claude ON alerts(claude_analysis_id) WHERE claude_analysis_id IS NOT NULL;
CREATE INDEX idx_alerts_data_gin ON alerts USING GIN (alert_data jsonb_path_ops);

-- Trigger to update updated_at
CREATE OR REPLACE FUNCTION update_alerts_timestamp()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_alerts_updated_at
    BEFORE UPDATE ON alerts
    FOR EACH ROW
    EXECUTE FUNCTION update_alerts_timestamp();
```

---

### 1.4 frame_metadata

**Purpose:** Detailed metadata for each processed frame (optimized for quick lookups)

```sql
-- ============================================================================
-- TABLE: frame_metadata
-- Purpose: Compressed metadata storage for frame retrieval and analysis
-- Write Rate: ~100 entries/second (10 cameras x 10 FPS)
-- Retention: 7 days hot, 30 days compressed
-- ============================================================================

CREATE TABLE frame_metadata (
    -- Primary Key
    metadata_id BIGSERIAL NOT NULL,

    -- Frame Identification
    camera_id VARCHAR(64) NOT NULL,
    frame_number BIGINT NOT NULL,
    frame_timestamp TIMESTAMPTZ NOT NULL,

    -- Frame Hashes (for deduplication and similarity)
    frame_hash VARCHAR(64) NOT NULL, -- Exact match hash (MD5/SHA256)
    perceptual_hash VARCHAR(64), -- pHash for visual similarity
    scene_hash VARCHAR(64), -- Scene-level hash (ignores minor changes)

    -- Image Properties
    resolution_width INTEGER NOT NULL,
    resolution_height INTEGER NOT NULL,
    format VARCHAR(16) NOT NULL, -- 'jpeg', 'png', 'yuv420'
    file_size_bytes INTEGER,
    compression_quality INTEGER, -- 1-100

    -- Quality Metrics
    sharpness_score DECIMAL(5,4), -- 0-1, motion blur detection
    brightness_level DECIMAL(5,4), -- 0-1, exposure level
    contrast_level DECIMAL(5,4), -- 0-1
    noise_level DECIMAL(5,4), -- 0-1
    overall_quality_score DECIMAL(5,4), -- Composite quality

    -- Storage Information
    storage_location VARCHAR(16) NOT NULL DEFAULT 'none', -- 'none', 'local', 's3', 'archive'
    storage_path TEXT, -- Full path or S3 key
    storage_size_bytes INTEGER,

    -- Processing Metadata
    sent_to_claude BOOLEAN DEFAULT FALSE,
    claude_analysis_count INTEGER DEFAULT 0,
    last_accessed_at TIMESTAMPTZ,

    -- Retention
    expires_at TIMESTAMPTZ, -- Auto-delete after this time
    archived BOOLEAN DEFAULT FALSE,

    -- Timestamps
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- Constraints
    CONSTRAINT pk_frame_metadata PRIMARY KEY (metadata_id, frame_timestamp),
    CONSTRAINT uq_frame_metadata_camera_frame UNIQUE (camera_id, frame_number, frame_timestamp),
    CONSTRAINT chk_storage_location CHECK (storage_location IN ('none', 'local', 's3', 'archive'))
) PARTITION BY RANGE (frame_timestamp);

-- Indexes
CREATE INDEX idx_frame_metadata_camera_time ON frame_metadata(camera_id, frame_timestamp DESC);
CREATE INDEX idx_frame_metadata_hash ON frame_metadata(frame_hash);
CREATE INDEX idx_frame_metadata_perceptual ON frame_metadata(perceptual_hash) WHERE perceptual_hash IS NOT NULL;
CREATE INDEX idx_frame_metadata_scene ON frame_metadata(scene_hash) WHERE scene_hash IS NOT NULL;
CREATE INDEX idx_frame_metadata_quality ON frame_metadata(overall_quality_score DESC) WHERE overall_quality_score >= 0.7;
CREATE INDEX idx_frame_metadata_storage ON frame_metadata(storage_location, created_at DESC) WHERE storage_location != 'none';

-- Create daily partitions (similar to camera_events)
CREATE OR REPLACE FUNCTION create_frame_metadata_partition(partition_date DATE)
RETURNS void AS $$
DECLARE
    partition_name TEXT;
    start_date TEXT;
    end_date TEXT;
BEGIN
    partition_name := 'frame_metadata_' || TO_CHAR(partition_date, 'YYYY_MM_DD');
    start_date := partition_date::TEXT;
    end_date := (partition_date + INTERVAL '1 day')::TEXT;

    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS %I PARTITION OF frame_metadata
         FOR VALUES FROM (%L) TO (%L)',
        partition_name, start_date, end_date
    );

    EXECUTE format('CREATE INDEX IF NOT EXISTS %I ON %I(camera_id, frame_timestamp DESC)',
        partition_name || '_camera_time_idx', partition_name);
END;
$$ LANGUAGE plpgsql;

-- Initialize partitions
DO $$
DECLARE
    partition_date DATE;
BEGIN
    FOR i IN 0..6 LOOP
        partition_date := CURRENT_DATE + (i || ' days')::INTERVAL;
        PERFORM create_frame_metadata_partition(partition_date);
    END LOOP;
END $$;

-- Auto-expire old frames
CREATE OR REPLACE FUNCTION cleanup_expired_frames()
RETURNS void AS $$
BEGIN
    DELETE FROM frame_metadata
    WHERE expires_at IS NOT NULL
    AND expires_at < NOW();
END;
$$ LANGUAGE plpgsql;

-- Schedule expiration cleanup (run every hour)
-- SELECT cron.schedule('cleanup-frames', '0 * * * *', 'SELECT cleanup_expired_frames()');
```

---

### 1.5 system_metrics

**Purpose:** Performance monitoring and system health tracking

```sql
-- ============================================================================
-- TABLE: system_metrics
-- Purpose: Time-series performance metrics for monitoring and alerting
-- Write Rate: ~10-100 metrics/second (varies by granularity)
-- Retention: 7 days raw, 90 days aggregated
-- ============================================================================

CREATE TABLE system_metrics (
    -- Primary Key
    metric_id BIGSERIAL NOT NULL,

    -- Metric Identification
    metric_name VARCHAR(64) NOT NULL,
    metric_type VARCHAR(16) NOT NULL, -- 'counter', 'gauge', 'histogram', 'rate'
    component VARCHAR(32) NOT NULL, -- 'camera', 'dlpu', 'mqtt', 'claude', 'redis', 'postgres'
    source_id VARCHAR(64), -- camera_id, service_name, etc.

    -- Metric Value
    value DECIMAL(15,6) NOT NULL,
    unit VARCHAR(16), -- 'fps', 'ms', 'bytes', 'percent', 'count'

    -- Metric Context
    tags JSONB, -- Additional dimensional data

    -- Timestamp
    metric_timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- Constraints
    CONSTRAINT pk_system_metrics PRIMARY KEY (metric_id, metric_timestamp),
    CONSTRAINT chk_metric_type CHECK (metric_type IN ('counter', 'gauge', 'histogram', 'rate'))
) PARTITION BY RANGE (metric_timestamp);

-- Indexes
CREATE INDEX idx_system_metrics_name_time ON system_metrics(metric_name, metric_timestamp DESC);
CREATE INDEX idx_system_metrics_component_time ON system_metrics(component, metric_timestamp DESC);
CREATE INDEX idx_system_metrics_source_time ON system_metrics(source_id, metric_timestamp DESC) WHERE source_id IS NOT NULL;
CREATE INDEX idx_system_metrics_tags_gin ON system_metrics USING GIN (tags jsonb_path_ops);

-- Create hourly partitions for metrics (higher frequency)
CREATE OR REPLACE FUNCTION create_system_metrics_partition(partition_timestamp TIMESTAMPTZ)
RETURNS void AS $$
DECLARE
    partition_name TEXT;
    start_time TEXT;
    end_time TEXT;
BEGIN
    partition_name := 'system_metrics_' || TO_CHAR(partition_timestamp, 'YYYY_MM_DD_HH24');
    start_time := partition_timestamp::TEXT;
    end_time := (partition_timestamp + INTERVAL '1 hour')::TEXT;

    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS %I PARTITION OF system_metrics
         FOR VALUES FROM (%L) TO (%L)',
        partition_name, start_time, end_time
    );

    EXECUTE format('CREATE INDEX IF NOT EXISTS %I ON %I(metric_name, metric_timestamp DESC)',
        partition_name || '_name_time_idx', partition_name);
END;
$$ LANGUAGE plpgsql;

-- Initialize next 24 hours of partitions
DO $$
DECLARE
    partition_time TIMESTAMPTZ;
BEGIN
    FOR i IN 0..23 LOOP
        partition_time := DATE_TRUNC('hour', NOW()) + (i || ' hours')::INTERVAL;
        PERFORM create_system_metrics_partition(partition_time);
    END LOOP;
END $$;

-- Common metrics view (for easy querying)
CREATE VIEW v_system_metrics_latest AS
SELECT DISTINCT ON (metric_name, component, source_id)
    metric_name,
    component,
    source_id,
    value,
    unit,
    metric_timestamp
FROM system_metrics
ORDER BY metric_name, component, source_id, metric_timestamp DESC;

-- Aggregated metrics (1-minute rollups)
CREATE TABLE system_metrics_rollup_1m (
    metric_name VARCHAR(64) NOT NULL,
    component VARCHAR(32) NOT NULL,
    source_id VARCHAR(64),
    rollup_timestamp TIMESTAMPTZ NOT NULL,

    -- Aggregations
    value_min DECIMAL(15,6),
    value_max DECIMAL(15,6),
    value_avg DECIMAL(15,6),
    value_sum DECIMAL(15,6),
    value_count BIGINT,
    value_p50 DECIMAL(15,6),
    value_p95 DECIMAL(15,6),
    value_p99 DECIMAL(15,6),

    PRIMARY KEY (metric_name, component, COALESCE(source_id, ''), rollup_timestamp)
);

CREATE INDEX idx_system_metrics_rollup_time ON system_metrics_rollup_1m(rollup_timestamp DESC);
CREATE INDEX idx_system_metrics_rollup_component ON system_metrics_rollup_1m(component, rollup_timestamp DESC);

-- Function to create rollups
CREATE OR REPLACE FUNCTION rollup_system_metrics()
RETURNS void AS $$
BEGIN
    INSERT INTO system_metrics_rollup_1m
    SELECT
        metric_name,
        component,
        source_id,
        DATE_TRUNC('minute', metric_timestamp) as rollup_timestamp,
        MIN(value) as value_min,
        MAX(value) as value_max,
        AVG(value) as value_avg,
        SUM(value) as value_sum,
        COUNT(*) as value_count,
        PERCENTILE_CONT(0.50) WITHIN GROUP (ORDER BY value) as value_p50,
        PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY value) as value_p95,
        PERCENTILE_CONT(0.99) WITHIN GROUP (ORDER BY value) as value_p99
    FROM system_metrics
    WHERE metric_timestamp >= NOW() - INTERVAL '2 minutes'
    AND metric_timestamp < DATE_TRUNC('minute', NOW())
    GROUP BY metric_name, component, source_id, DATE_TRUNC('minute', metric_timestamp)
    ON CONFLICT (metric_name, component, COALESCE(source_id, ''), rollup_timestamp) DO NOTHING;
END;
$$ LANGUAGE plpgsql;

-- Schedule rollup (run every minute)
-- SELECT cron.schedule('rollup-metrics', '* * * * *', 'SELECT rollup_system_metrics()');
```

---

### 1.6 audit_log

**Purpose:** System operations audit trail for security and debugging

```sql
-- ============================================================================
-- TABLE: audit_log
-- Purpose: Immutable audit trail of system operations and changes
-- Write Rate: ~1-50 entries/second (varies by user activity)
-- Retention: 1 year minimum (compliance requirement)
-- ============================================================================

CREATE TABLE audit_log (
    -- Primary Key
    audit_id BIGSERIAL NOT NULL,

    -- Event Classification
    event_type VARCHAR(32) NOT NULL, -- 'create', 'update', 'delete', 'access', 'config_change', 'auth', 'api_call'
    entity_type VARCHAR(32) NOT NULL, -- 'camera', 'alert', 'user', 'config', 'system'
    entity_id VARCHAR(64),

    -- Action Details
    action VARCHAR(64) NOT NULL, -- 'camera.created', 'alert.acknowledged', 'config.bandwidth_updated'
    action_status VARCHAR(16) NOT NULL, -- 'success', 'failure', 'denied'

    -- Actor Information
    user_id VARCHAR(64),
    user_email VARCHAR(255),
    service_name VARCHAR(64), -- For automated system actions
    ip_address INET,
    user_agent TEXT,

    -- Change Details
    changes_made JSONB, -- {field: {old: X, new: Y}}
    request_data JSONB, -- Original request payload
    response_data JSONB, -- Response or error details

    -- Context
    session_id VARCHAR(64),
    correlation_id VARCHAR(64), -- For tracing across services
    severity VARCHAR(16) DEFAULT 'info', -- 'debug', 'info', 'warning', 'error', 'critical'

    -- Compliance
    compliance_relevant BOOLEAN DEFAULT FALSE, -- Flag for regulatory review
    data_access_type VARCHAR(16), -- 'read', 'write', 'export', 'delete'

    -- Timestamp
    event_timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- Constraints
    CONSTRAINT pk_audit_log PRIMARY KEY (audit_id, event_timestamp),
    CONSTRAINT chk_action_status CHECK (action_status IN ('success', 'failure', 'denied', 'pending')),
    CONSTRAINT chk_severity CHECK (severity IN ('debug', 'info', 'warning', 'error', 'critical'))
) PARTITION BY RANGE (event_timestamp);

-- Indexes
CREATE INDEX idx_audit_log_entity ON audit_log(entity_type, entity_id, event_timestamp DESC);
CREATE INDEX idx_audit_log_user ON audit_log(user_id, event_timestamp DESC) WHERE user_id IS NOT NULL;
CREATE INDEX idx_audit_log_action ON audit_log(action, event_timestamp DESC);
CREATE INDEX idx_audit_log_failures ON audit_log(event_timestamp DESC) WHERE action_status = 'failure';
CREATE INDEX idx_audit_log_compliance ON audit_log(event_timestamp DESC) WHERE compliance_relevant = TRUE;
CREATE INDEX idx_audit_log_correlation ON audit_log(correlation_id) WHERE correlation_id IS NOT NULL;
CREATE INDEX idx_audit_log_changes_gin ON audit_log USING GIN (changes_made jsonb_path_ops);

-- Create monthly partitions for audit log (compliance retention)
CREATE OR REPLACE FUNCTION create_audit_log_partition(partition_month DATE)
RETURNS void AS $$
DECLARE
    partition_name TEXT;
    start_date TEXT;
    end_date TEXT;
BEGIN
    partition_name := 'audit_log_' || TO_CHAR(partition_month, 'YYYY_MM');
    start_date := partition_month::TEXT;
    end_date := (partition_month + INTERVAL '1 month')::TEXT;

    EXECUTE format(
        'CREATE TABLE IF NOT EXISTS %I PARTITION OF audit_log
         FOR VALUES FROM (%L) TO (%L)',
        partition_name, start_date, end_date
    );

    EXECUTE format('CREATE INDEX IF NOT EXISTS %I ON %I(action, event_timestamp DESC)',
        partition_name || '_action_idx', partition_name);
END;
$$ LANGUAGE plpgsql;

-- Initialize partitions for current and next 3 months
DO $$
DECLARE
    partition_month DATE;
BEGIN
    FOR i IN 0..3 LOOP
        partition_month := DATE_TRUNC('month', NOW()) + (i || ' months')::INTERVAL;
        PERFORM create_audit_log_partition(partition_month);
    END LOOP;
END $$;

-- Prevent modifications (audit logs should be immutable)
CREATE OR REPLACE FUNCTION prevent_audit_modification()
RETURNS TRIGGER AS $$
BEGIN
    IF TG_OP = 'UPDATE' THEN
        RAISE EXCEPTION 'Audit logs cannot be modified';
    ELSIF TG_OP = 'DELETE' THEN
        RAISE EXCEPTION 'Audit logs cannot be deleted manually';
    END IF;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_audit_immutable
    BEFORE UPDATE OR DELETE ON audit_log
    FOR EACH ROW
    EXECUTE FUNCTION prevent_audit_modification();
```

---

### 1.7 Supporting Tables

#### cameras (Configuration and Registry)

```sql
-- ============================================================================
-- TABLE: cameras
-- Purpose: Camera registry and configuration management
-- ============================================================================

CREATE TABLE cameras (
    camera_id VARCHAR(64) PRIMARY KEY,
    camera_name VARCHAR(255) NOT NULL,

    -- Network Configuration
    ip_address INET NOT NULL,
    mac_address MACADDR,
    rtsp_url TEXT,

    -- Hardware Information
    model VARCHAR(64),
    firmware_version VARCHAR(32),
    hardware_id VARCHAR(64),

    -- Capabilities
    max_resolution_width INTEGER,
    max_resolution_height INTEGER,
    max_fps INTEGER,
    has_ptz BOOLEAN DEFAULT FALSE,
    has_audio BOOLEAN DEFAULT FALSE,
    has_ir BOOLEAN DEFAULT FALSE,

    -- Current Configuration
    active_resolution_width INTEGER,
    active_resolution_height INTEGER,
    active_fps INTEGER,
    active_quality INTEGER, -- 1-100

    -- Axis I.S. Configuration
    metadata_extraction_enabled BOOLEAN DEFAULT TRUE,
    send_to_claude BOOLEAN DEFAULT TRUE,
    dlpu_priority INTEGER DEFAULT 5, -- 1-10

    -- Status
    status VARCHAR(16) NOT NULL DEFAULT 'offline', -- 'online', 'offline', 'error', 'maintenance'
    last_seen_at TIMESTAMPTZ,
    last_event_at TIMESTAMPTZ,

    -- Location
    location_name VARCHAR(255),
    location_coordinates POINT, -- Geographic coordinates
    zone VARCHAR(64), -- Security zone

    -- Metadata
    tags JSONB,
    notes TEXT,

    -- Timestamps
    registered_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ,

    CONSTRAINT chk_camera_status CHECK (status IN ('online', 'offline', 'error', 'maintenance', 'decommissioned'))
);

CREATE INDEX idx_cameras_status ON cameras(status) WHERE status = 'online';
CREATE INDEX idx_cameras_zone ON cameras(zone);
CREATE INDEX idx_cameras_last_seen ON cameras(last_seen_at DESC);

CREATE TRIGGER trigger_cameras_updated_at
    BEFORE UPDATE ON cameras
    FOR EACH ROW
    EXECUTE FUNCTION update_alerts_timestamp(); -- Reuse timestamp function
```

---

## 2. Redis Schema

### Overview

Redis provides real-time state management for Axis I.S. with sub-millisecond access times:
- Camera state and current metrics
- DLPU scheduler coordination
- Bandwidth allocation tracking
- Scene memory for Claude context
- Message queues and retry logic
- Rate limiting

### 2.1 Key Naming Conventions

```
Format: {namespace}:{entity_type}:{id}:{attribute}

Examples:
  axion:camera:CAM001:state
  axion:dlpu:scheduler:lock
  axion:bandwidth:CAM001:allocation
  axion:scene:CAM001:memory
  axion:queue:mqtt:failed
  axion:ratelimit:claude:counter
```

---

### 2.2 Camera State

**Purpose:** Real-time camera status and metrics

```redis
# ============================================================================
# CAMERA STATE - Hash structure per camera
# Key: axion:camera:{camera_id}:state
# TTL: None (persistent until camera disconnects)
# ============================================================================

HSET axion:camera:CAM001:state
  status "online"
  fps_current "9.8"
  fps_target "10.0"
  frames_processed "12450"
  frames_dropped "23"
  last_event_timestamp "1732377623"
  last_metadata_sent_at "1732377622"
  quality_level "medium"
  bandwidth_mbps "2.4"
  motion_detected "true"
  objects_count "3"
  dlpu_queue_depth "2"
  last_error ""
  uptime_seconds "86423"

# Retrieve current FPS
HGET axion:camera:CAM001:state fps_current
# Returns: "9.8"

# Get all camera state
HGETALL axion:camera:CAM001:state

# Update specific field (atomic)
HSET axion:camera:CAM001:state fps_current "10.1"

# Increment frame counter (atomic)
HINCRBY axion:camera:CAM001:state frames_processed 1

# ============================================================================
# CAMERA METRICS - Time-series data (last 60 seconds)
# Key: axion:camera:{camera_id}:metrics
# Type: Sorted Set (score = timestamp)
# TTL: 300 seconds
# ============================================================================

# Add metric sample
ZADD axion:camera:CAM001:metrics 1732377623 '{"fps":9.8,"motion":0.45,"objects":3}'

# Get last 60 seconds of metrics
ZRANGEBYSCORE axion:camera:CAM001:metrics (NOW-60) +inf

# Remove metrics older than 5 minutes
ZREMRANGEBYSCORE axion:camera:CAM001:metrics -inf (NOW-300)

# Set TTL on metrics key
EXPIRE axion:camera:CAM001:metrics 300

# ============================================================================
# CAMERA LIST - Set of all active cameras
# Key: axion:cameras:active
# Type: Set
# ============================================================================

SADD axion:cameras:active "CAM001" "CAM002" "CAM003"

# Get all active cameras
SMEMBERS axion:cameras:active

# Check if camera is active
SISMEMBER axion:cameras:active "CAM001"

# Remove offline camera
SREM axion:cameras:active "CAM005"
```

---

### 2.3 DLPU Scheduler State

**Purpose:** Coordinate DLPU access across multiple cameras (critical for preventing conflicts)

```redis
# ============================================================================
# DLPU SCHEDULER LOCK - Distributed lock for DLPU access
# Key: axion:dlpu:scheduler:lock
# Type: String
# TTL: 100ms (auto-release if holder crashes)
# ============================================================================

# Acquire lock (SET with NX and PX)
SET axion:dlpu:scheduler:lock "CAM001:frame_12450" NX PX 100
# Returns: OK (if acquired), nil (if already locked)

# Release lock (DEL with check)
DEL axion:dlpu:scheduler:lock

# Check lock holder
GET axion:dlpu:scheduler:lock
# Returns: "CAM001:frame_12450"

# ============================================================================
# DLPU TIME SLOTS - Hash map of camera -> next available slot
# Key: axion:dlpu:timeslots
# Type: Hash
# ============================================================================

HSET axion:dlpu:timeslots
  CAM001 "1732377623.000"
  CAM002 "1732377623.050"
  CAM003 "1732377623.100"
  CAM004 "1732377623.150"
  CAM005 "1732377623.200"

# Get next slot for camera
HGET axion:dlpu:timeslots CAM001
# Returns: "1732377623.000"

# Update slot after inference (add 50ms)
HINCRBYFLOAT axion:dlpu:timeslots CAM001 0.050

# ============================================================================
# DLPU QUEUE - Sorted set of pending inference requests
# Key: axion:dlpu:queue
# Type: Sorted Set (score = priority timestamp)
# ============================================================================

# Add camera to queue (score = target_time * 1000000 + priority)
ZADD axion:dlpu:queue 1732377623000005 "CAM001:frame_12450"

# Get next camera to process
ZPOPMIN axion:dlpu:queue 1
# Returns: ["CAM001:frame_12450", 1732377623000005]

# Check queue depth
ZCARD axion:dlpu:queue

# Remove stale requests (older than 1 second)
ZREMRANGEBYSCORE axion:dlpu:queue -inf (NOW*1000000-1000000)

# ============================================================================
# DLPU STATS - Hash of inference statistics
# Key: axion:dlpu:stats
# Type: Hash
# ============================================================================

HSET axion:dlpu:stats
  total_inferences "125430"
  avg_inference_ms "48.3"
  last_inference_ms "45"
  queue_wait_avg_ms "12.5"
  contentions_count "234"
  last_contention_at "1732377500"

HINCRBY axion:dlpu:stats total_inferences 1
HSET axion:dlpu:stats last_inference_ms "47"
```

---

### 2.4 Bandwidth Allocations

**Purpose:** Dynamic quality level management per camera

```redis
# ============================================================================
# BANDWIDTH ALLOCATION - Hash per camera
# Key: axion:bandwidth:{camera_id}:allocation
# Type: Hash
# TTL: None (updated by bandwidth controller)
# ============================================================================

HSET axion:bandwidth:CAM001:allocation
  quality_level "medium"
  target_fps "10"
  max_bitrate_kbps "2500"
  current_bitrate_kbps "2340"
  compression_quality "75"
  resolution "1920x1080"
  last_adjustment_at "1732377600"
  adjustment_reason "high_motion_detected"

# Get current quality level
HGET axion:bandwidth:CAM001:allocation quality_level

# Update allocation (atomic)
HMSET axion:bandwidth:CAM001:allocation \
  quality_level "high" \
  target_fps "15" \
  max_bitrate_kbps "4000" \
  last_adjustment_at "1732377623"

# ============================================================================
# GLOBAL BANDWIDTH BUDGET - String (total available bandwidth)
# Key: axion:bandwidth:global:budget
# Type: String (float)
# ============================================================================

SET axion:bandwidth:global:budget "25.0"
# 25 Mbps total

# Get remaining bandwidth
GET axion:bandwidth:global:budget

# Atomic decrement (reserve bandwidth)
INCRBYFLOAT axion:bandwidth:global:budget -2.5
# Reserves 2.5 Mbps

# ============================================================================
# BANDWIDTH USAGE TRACKING - Sorted set of cameras by usage
# Key: axion:bandwidth:usage:ranking
# Type: Sorted Set (score = current bitrate)
# ============================================================================

ZADD axion:bandwidth:usage:ranking \
  2340 "CAM001" \
  1850 "CAM002" \
  3100 "CAM003"

# Get top bandwidth consumers
ZREVRANGE axion:bandwidth:usage:ranking 0 4 WITHSCORES

# Get camera rank
ZREVRANK axion:bandwidth:usage:ranking "CAM001"
```

---

### 2.5 Scene Memory

**Purpose:** Store recent frame metadata for Claude contextual analysis

```redis
# ============================================================================
# SCENE MEMORY - List of recent frames per camera
# Key: axion:scene:{camera_id}:memory
# Type: List (LPUSH/RPUSH for queue behavior)
# TTL: 3600 seconds (1 hour)
# ============================================================================

# Add new frame to scene memory (keep last 30 frames)
LPUSH axion:scene:CAM001:memory '{"frame":12450,"ts":1732377623,"hash":"a3f5c8","objects":["person","car"],"motion":0.45}'
LTRIM axion:scene:CAM001:memory 0 29

# Get last N frames for context
LRANGE axion:scene:CAM001:memory 0 9
# Returns last 10 frames

# Get oldest frame in memory
LINDEX axion:scene:CAM001:memory -1

# Set TTL (auto-expire if no activity)
EXPIRE axion:scene:CAM001:memory 3600

# ============================================================================
# SCENE HASH INDEX - Set of unique scene hashes (deduplication)
# Key: axion:scene:{camera_id}:hashes
# Type: Sorted Set (score = timestamp)
# TTL: 3600 seconds
# ============================================================================

# Add scene hash
ZADD axion:scene:CAM001:hashes 1732377623 "a3f5c8"

# Check if scene already seen (last 5 minutes)
ZRANGEBYSCORE axion:scene:CAM001:hashes (NOW-300) +inf
# If contains "a3f5c8", scene is duplicate

# Remove old hashes
ZREMRANGEBYSCORE axion:scene:CAM001:hashes -inf (NOW-3600)

# ============================================================================
# CLAUDE CONTEXT CACHE - Hash of prepared context per camera
# Key: axion:claude:{camera_id}:context
# Type: Hash
# TTL: 600 seconds (10 minutes)
# ============================================================================

HSET axion:claude:CAM001:context
  last_description "Empty parking lot, 2 cars parked, sunny weather"
  last_objects '["car","car","tree","building"]'
  last_analysis_at "1732377600"
  pattern_detected "none"
  alert_active "false"
  context_version "v3"

GET axion:claude:CAM001:context last_description

EXPIRE axion:claude:CAM001:context 600
```

---

### 2.6 Message Queues

**Purpose:** Failed message retry and delivery guarantees

```redis
# ============================================================================
# FAILED MQTT MESSAGES - List queue
# Key: axion:queue:mqtt:failed
# Type: List
# ============================================================================

# Add failed message
LPUSH axion:queue:mqtt:failed '{"topic":"axis-is/CAM001/event","payload":"...","attempts":1,"last_error":"connection_timeout","queued_at":1732377623}'

# Get next message to retry
RPOP axion:queue:mqtt:failed
# Returns: oldest failed message

# Get queue depth
LLEN axion:queue:mqtt:failed

# Peek at next message (without removing)
LRANGE axion:queue:mqtt:failed -1 -1

# ============================================================================
# RETRY SCHEDULE - Sorted set (score = retry timestamp)
# Key: axion:queue:mqtt:retry
# Type: Sorted Set
# ============================================================================

# Schedule retry (exponential backoff)
ZADD axion:queue:mqtt:retry 1732377683 '{"topic":"axis-is/CAM001/event","payload":"...","attempts":2}'
# Retry in 60 seconds

# Get messages ready for retry
ZRANGEBYSCORE axion:queue:mqtt:retry -inf NOW

# Remove after successful retry
ZREM axion:queue:mqtt:retry '{"topic":"axis-is/CAM001/event",...}'

# ============================================================================
# DEAD LETTER QUEUE - Failed after max retries
# Key: axion:queue:mqtt:dead_letter
# Type: List
# ============================================================================

LPUSH axion:queue:mqtt:dead_letter '{"topic":"axis-is/CAM001/event","payload":"...","attempts":5,"final_error":"max_retries_exceeded","failed_at":1732377923}'

# Monitor dead letter queue size
LLEN axion:queue:mqtt:dead_letter

# Drain dead letters for manual review
LRANGE axion:queue:mqtt:dead_letter 0 -1
```

---

### 2.7 Rate Limiting

**Purpose:** Control Claude API call frequency and costs

```redis
# ============================================================================
# CLAUDE API RATE LIMIT - Counter with sliding window
# Key: axion:ratelimit:claude:calls:{window}
# Type: String (counter)
# TTL: 60 seconds
# ============================================================================

# Increment call counter
INCR axion:ratelimit:claude:calls:1732377600
EXPIRE axion:ratelimit:claude:calls:1732377600 60

# Check if under limit
GET axion:ratelimit:claude:calls:1732377600
# If < 60, allow call

# ============================================================================
# CLAUDE API TOKEN USAGE - Hash tracking token consumption
# Key: axion:ratelimit:claude:tokens
# Type: Hash
# ============================================================================

HSET axion:ratelimit:claude:tokens
  total_used "1245678"
  last_hour_used "4532"
  last_call_tokens "1523"
  budget_remaining "754322"
  reset_at "1732381200"

HINCRBY axion:ratelimit:claude:tokens total_used 1523
HINCRBY axion:ratelimit:claude:tokens last_hour_used 1523

# ============================================================================
# CAMERA-SPECIFIC RATE LIMITS - Sorted set (score = last call time)
# Key: axion:ratelimit:camera:claude_calls
# Type: Sorted Set
# ============================================================================

# Record camera API call
ZADD axion:ratelimit:camera:claude_calls 1732377623 "CAM001"

# Count calls in last minute per camera
ZCOUNT axion:ratelimit:camera:claude_calls (NOW-60) +inf

# Get cameras that haven't called recently (prioritize them)
ZRANGEBYSCORE axion:ratelimit:camera:claude_calls -inf (NOW-300)

# Remove old entries
ZREMRANGEBYSCORE axion:ratelimit:camera:claude_calls -inf (NOW-3600)
```

---

### 2.8 Redis Configuration

**Recommended redis.conf settings for Axis I.S.:**

```conf
# Memory Management
maxmemory 2gb
maxmemory-policy allkeys-lru  # Evict least recently used keys when full

# Persistence (balance between performance and durability)
save 900 1      # Save if 1 key changed in 15 minutes
save 300 10     # Save if 10 keys changed in 5 minutes
save 60 10000   # Save if 10000 keys changed in 1 minute

# AOF (Append-Only File) for better durability
appendonly yes
appendfsync everysec  # Fsync every second (good balance)

# Performance
tcp-backlog 511
timeout 300
tcp-keepalive 60

# Slow log (debug performance issues)
slowlog-log-slower-than 10000  # Log queries > 10ms
slowlog-max-len 128

# Client connections
maxclients 10000
```

---

## 3. Time-Series Optimizations

### 3.1 Partitioning Strategy Summary

| Table | Partition By | Partition Size | Retention |
|-------|--------------|----------------|-----------|
| camera_events | Daily | ~8.6M rows/day | 30 days hot, 90 warm |
| frame_metadata | Daily | ~8.6M rows/day | 7 days hot, 30 compressed |
| claude_analyses | Monthly | ~130K rows/month | 90 days |
| alerts | None | ~43K rows/month | 1 year |
| system_metrics | Hourly | ~360K rows/hour | 7 days raw, 90 aggregated |
| audit_log | Monthly | Varies | 1 year minimum |

### 3.2 Write Optimization

**Batch Inserts:**

```sql
-- Use COPY for bulk inserts (10x faster than individual INSERTs)
COPY camera_events (camera_id, event_type, event_timestamp, frame_number, ...)
FROM STDIN CSV;

-- Or use multi-row INSERT
INSERT INTO camera_events (camera_id, event_type, ...)
VALUES
  ('CAM001', 'motion', NOW(), ...),
  ('CAM001', 'motion', NOW(), ...),
  ('CAM001', 'motion', NOW(), ...)
;
```

**Unlogged Tables (for temporary data):**

```sql
-- For high-write temporary tables
CREATE UNLOGGED TABLE temp_processing_queue (
    -- No WAL overhead, 2-3x faster writes
    -- Data lost on crash (acceptable for queues)
);
```

### 3.3 Query Optimization

**Covering Indexes:**

```sql
-- Index includes all columns needed by query (no table lookup)
CREATE INDEX idx_camera_events_covering ON camera_events(
    camera_id, event_timestamp DESC
) INCLUDE (event_type, motion_score, object_count);
```

**Partial Indexes:**

```sql
-- Index only relevant rows (smaller, faster)
CREATE INDEX idx_camera_events_high_motion ON camera_events(event_timestamp DESC)
WHERE motion_score > 0.5;
```

**Materialized Views:**

```sql
-- Pre-computed aggregations for dashboards
CREATE MATERIALIZED VIEW mv_camera_stats_hourly AS
SELECT
    camera_id,
    DATE_TRUNC('hour', event_timestamp) as hour,
    COUNT(*) as event_count,
    AVG(motion_score) as avg_motion,
    AVG(object_count) as avg_objects,
    MAX(object_count) as max_objects
FROM camera_events
GROUP BY camera_id, DATE_TRUNC('hour', event_timestamp);

CREATE UNIQUE INDEX ON mv_camera_stats_hourly(camera_id, hour);

-- Refresh hourly
REFRESH MATERIALIZED VIEW CONCURRENTLY mv_camera_stats_hourly;
```

### 3.4 Data Compression

**PostgreSQL Column Compression:**

```sql
-- Enable compression on JSONB columns (PG 14+)
ALTER TABLE camera_events ALTER COLUMN objects_detected SET COMPRESSION lz4;
ALTER TABLE camera_events ALTER COLUMN motion_regions SET COMPRESSION lz4;
```

**Toast Settings:**

```sql
-- Store large text/jsonb externally
ALTER TABLE claude_analyses ALTER COLUMN claude_response SET STORAGE EXTERNAL;
```

---

## 4. Migration Scripts

### 4.1 Initial Schema Creation

**File: `migrations/001_initial_schema.sql`**

```sql
-- ============================================================================
-- Axis I.S. Database Initial Schema
-- Migration: 001
-- Description: Create core tables and partitions
-- ============================================================================

BEGIN;

-- Enable extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm"; -- For text search
CREATE EXTENSION IF NOT EXISTS "btree_gin"; -- For JSONB indexes

-- Create schemas
CREATE SCHEMA IF NOT EXISTS axion;
CREATE SCHEMA IF NOT EXISTS archive;

SET search_path TO axion, public;

-- Create tables (see section 1 for full DDL)
\i 001_create_cameras.sql
\i 002_create_camera_events.sql
\i 003_create_claude_analyses.sql
\i 004_create_alerts.sql
\i 005_create_frame_metadata.sql
\i 006_create_system_metrics.sql
\i 007_create_audit_log.sql

-- Create initial partitions
SELECT create_camera_events_partition(CURRENT_DATE + (i || ' days')::INTERVAL)
FROM generate_series(0, 6) i;

SELECT create_frame_metadata_partition(CURRENT_DATE + (i || ' days')::INTERVAL)
FROM generate_series(0, 6) i;

SELECT create_system_metrics_partition(DATE_TRUNC('hour', NOW()) + (i || ' hours')::INTERVAL)
FROM generate_series(0, 23) i;

SELECT create_audit_log_partition(DATE_TRUNC('month', NOW()) + (i || ' months')::INTERVAL)
FROM generate_series(0, 3) i;

COMMIT;
```

### 4.2 Sample Data Insertion

**File: `migrations/002_sample_data.sql`**

```sql
-- ============================================================================
-- Sample Data for Testing
-- ============================================================================

BEGIN;

-- Insert sample cameras
INSERT INTO cameras (camera_id, camera_name, ip_address, model, status) VALUES
  ('CAM001', 'Front Entrance', '192.168.1.101', 'AXIS P3245-LVE', 'online'),
  ('CAM002', 'Parking Lot', '192.168.1.102', 'AXIS Q1656-LE', 'online'),
  ('CAM003', 'Back Door', '192.168.1.103', 'AXIS M3067-P', 'online');

-- Insert sample events
INSERT INTO camera_events (
    camera_id, event_type, event_timestamp, frame_number,
    motion_score, object_count, objects_detected
) VALUES
  ('CAM001', 'motion', NOW() - INTERVAL '5 minutes', 1000, 0.75, 1, '[{"class":"person","confidence":0.92}]'::jsonb),
  ('CAM001', 'object_detected', NOW() - INTERVAL '3 minutes', 1200, 0.45, 2, '[{"class":"person","confidence":0.88},{"class":"car","confidence":0.95}]'::jsonb),
  ('CAM002', 'scene_change', NOW() - INTERVAL '1 minute', 5000, 0.20, 0, '[]'::jsonb);

-- Insert sample Claude analysis
INSERT INTO claude_analyses (
    trigger_event_id, camera_id, event_timestamp, request_type,
    claude_response, primary_insight, risk_assessment
) VALUES
  (1, 'CAM001', NOW() - INTERVAL '5 minutes', 'scene_description',
   '{"content":"A person walking near the entrance"}'::jsonb,
   'Normal activity - single person entering building',
   'none');

-- Insert sample alert
INSERT INTO alerts (
    alert_type, severity, camera_id, trigger_event_id, title, description
) VALUES
  ('motion', 3, 'CAM001', 1, 'Motion Detected at Front Entrance',
   'Person detected near entrance at unusual hour');

COMMIT;
```

### 4.3 Rollback Script

**File: `migrations/rollback_001.sql`**

```sql
-- ============================================================================
-- Rollback Migration 001
-- ============================================================================

BEGIN;

-- Drop tables in reverse order (respect dependencies)
DROP TABLE IF EXISTS audit_log CASCADE;
DROP TABLE IF EXISTS system_metrics CASCADE;
DROP TABLE IF EXISTS system_metrics_rollup_1m CASCADE;
DROP TABLE IF EXISTS frame_metadata CASCADE;
DROP TABLE IF EXISTS alerts CASCADE;
DROP TABLE IF EXISTS claude_analyses CASCADE;
DROP TABLE IF EXISTS camera_events CASCADE;
DROP TABLE IF EXISTS cameras CASCADE;

-- Drop functions
DROP FUNCTION IF EXISTS create_camera_events_partition(DATE);
DROP FUNCTION IF EXISTS create_frame_metadata_partition(DATE);
DROP FUNCTION IF EXISTS create_system_metrics_partition(TIMESTAMPTZ);
DROP FUNCTION IF EXISTS create_audit_log_partition(DATE);
DROP FUNCTION IF EXISTS cleanup_old_partitions();
DROP FUNCTION IF EXISTS cleanup_expired_frames();
DROP FUNCTION IF EXISTS rollup_system_metrics();
DROP FUNCTION IF EXISTS update_alerts_timestamp();
DROP FUNCTION IF EXISTS prevent_audit_modification();

-- Drop views
DROP VIEW IF EXISTS v_system_metrics_latest;

-- Drop schemas
DROP SCHEMA IF EXISTS archive CASCADE;

COMMIT;
```

---

## 5. Performance Tuning

### 5.1 PostgreSQL Configuration

**File: `postgresql.conf` recommendations:**

```ini
# ============================================================================
# PostgreSQL Configuration for Axis I.S.
# Hardware: 16GB RAM, 8 CPU cores, SSD storage
# ============================================================================

# Memory Settings
shared_buffers = 4GB                    # 25% of RAM
effective_cache_size = 12GB             # 75% of RAM
maintenance_work_mem = 1GB              # For CREATE INDEX, VACUUM
work_mem = 64MB                         # Per query operation

# Checkpoint Settings (balance between crash recovery and performance)
checkpoint_timeout = 15min
checkpoint_completion_target = 0.9
max_wal_size = 4GB
min_wal_size = 1GB

# Query Planner
random_page_cost = 1.1                  # SSD (default 4.0 for HDD)
effective_io_concurrency = 200          # SSD parallelism
default_statistics_target = 100         # Better query plans

# Write Performance
synchronous_commit = off                # Accept ~100ms data loss for 3x write speed
wal_writer_delay = 200ms
commit_delay = 1000                     # Microseconds (batch commits)
commit_siblings = 5

# Parallel Query
max_parallel_workers_per_gather = 4
max_parallel_workers = 8
max_worker_processes = 8

# Connection Pooling (use PgBouncer)
max_connections = 100                   # Low (pool handles this)

# Logging (for monitoring)
log_min_duration_statement = 1000       # Log queries > 1 second
log_line_prefix = '%t [%p] %u@%d '
log_checkpoints = on
log_connections = on
log_disconnections = on
log_lock_waits = on

# Autovacuum (critical for time-series data)
autovacuum = on
autovacuum_max_workers = 4
autovacuum_naptime = 10s                # Check every 10 seconds
autovacuum_vacuum_scale_factor = 0.05   # Vacuum at 5% dead rows (default 20%)
autovacuum_analyze_scale_factor = 0.02  # Analyze at 2% changes

# Time-Series Optimizations
enable_partition_pruning = on
enable_partitionwise_join = on
enable_partitionwise_aggregate = on
```

### 5.2 Connection Pooling (PgBouncer)

**File: `pgbouncer.ini`**

```ini
[databases]
axion = host=localhost port=5432 dbname=axion

[pgbouncer]
listen_addr = *
listen_port = 6432
auth_type = md5
auth_file = /etc/pgbouncer/userlist.txt
pool_mode = transaction
max_client_conn = 1000
default_pool_size = 25
reserve_pool_size = 5
reserve_pool_timeout = 3
server_lifetime = 3600
server_idle_timeout = 600
log_connections = 1
log_disconnections = 1
```

### 5.3 Index Strategies

**Most Important Indexes for Common Queries:**

```sql
-- Query: Get recent events for camera
CREATE INDEX idx_camera_events_camera_recent
ON camera_events(camera_id, event_timestamp DESC)
WHERE event_timestamp > NOW() - INTERVAL '24 hours';

-- Query: Find high-motion events
CREATE INDEX idx_camera_events_high_motion
ON camera_events(event_timestamp DESC)
WHERE motion_score > 0.7
INCLUDE (camera_id, objects_detected);

-- Query: Search for specific object classes
CREATE INDEX idx_camera_events_objects_class
ON camera_events USING GIN ((objects_detected -> 'class'));

-- Query: Unprocessed events for Claude
CREATE INDEX idx_camera_events_pending_claude
ON camera_events(event_timestamp)
WHERE sent_to_claude = FALSE;

-- Query: Recent alerts by camera
CREATE INDEX idx_alerts_camera_recent
ON alerts(camera_id, created_at DESC)
WHERE status != 'resolved';

-- Query: High-severity unresolved alerts
CREATE INDEX idx_alerts_critical
ON alerts(created_at DESC)
WHERE severity >= 4 AND status = 'new';
```

### 5.4 Query Monitoring

**Useful monitoring queries:**

```sql
-- ============================================================================
-- MONITORING QUERIES
-- ============================================================================

-- 1. Active queries (real-time)
SELECT
    pid,
    now() - query_start AS duration,
    state,
    query
FROM pg_stat_activity
WHERE state != 'idle'
ORDER BY duration DESC;

-- 2. Slow queries (from pg_stat_statements)
SELECT
    query,
    calls,
    total_exec_time / calls AS avg_time_ms,
    min_exec_time,
    max_exec_time,
    stddev_exec_time
FROM pg_stat_statements
ORDER BY avg_time_ms DESC
LIMIT 20;

-- 3. Table sizes
SELECT
    schemaname,
    tablename,
    pg_size_pretty(pg_total_relation_size(schemaname||'.'||tablename)) AS size,
    pg_total_relation_size(schemaname||'.'||tablename) AS size_bytes
FROM pg_tables
WHERE schemaname NOT IN ('pg_catalog', 'information_schema')
ORDER BY size_bytes DESC;

-- 4. Index usage statistics
SELECT
    schemaname,
    tablename,
    indexname,
    idx_scan,
    idx_tup_read,
    idx_tup_fetch,
    pg_size_pretty(pg_relation_size(indexrelid)) AS size
FROM pg_stat_user_indexes
ORDER BY idx_scan ASC;  -- Low scans = unused index

-- 5. Partition sizes
SELECT
    tablename,
    pg_size_pretty(pg_total_relation_size('public.'||tablename)) AS size
FROM pg_tables
WHERE tablename LIKE 'camera_events_%'
ORDER BY tablename DESC
LIMIT 10;

-- 6. Write performance (inserts per second)
SELECT
    relname,
    n_tup_ins AS inserts,
    n_tup_upd AS updates,
    n_tup_del AS deletes,
    n_live_tup AS live_rows,
    n_dead_tup AS dead_rows
FROM pg_stat_user_tables
ORDER BY n_tup_ins DESC;

-- 7. Cache hit ratio (should be > 99%)
SELECT
    'cache_hit_ratio' AS metric,
    ROUND(
        100.0 * sum(heap_blks_hit) / NULLIF(sum(heap_blks_hit) + sum(heap_blks_read), 0),
        2
    ) AS percentage
FROM pg_statio_user_tables;
```

---

## 6. Query Patterns

### 6.1 Common Read Queries

```sql
-- ============================================================================
-- COMMON READ PATTERNS
-- ============================================================================

-- 1. Get recent events for a camera (last hour)
SELECT
    event_id,
    event_type,
    event_timestamp,
    motion_score,
    object_count,
    objects_detected
FROM camera_events
WHERE camera_id = 'CAM001'
AND event_timestamp > NOW() - INTERVAL '1 hour'
ORDER BY event_timestamp DESC
LIMIT 100;

-- 2. Find events with specific object class
SELECT
    camera_id,
    event_timestamp,
    objects_detected
FROM camera_events
WHERE objects_detected @> '[{"class": "person"}]'::jsonb
AND event_timestamp > NOW() - INTERVAL '24 hours'
ORDER BY event_timestamp DESC;

-- 3. Get Claude analyses with high risk
SELECT
    a.analysis_id,
    a.camera_id,
    a.event_timestamp,
    a.primary_insight,
    a.risk_assessment,
    e.objects_detected
FROM claude_analyses a
JOIN camera_events e ON a.trigger_event_id = e.event_id
WHERE a.risk_assessment IN ('high', 'critical')
AND a.event_timestamp > NOW() - INTERVAL '7 days'
ORDER BY a.event_timestamp DESC;

-- 4. Get active alerts grouped by camera
SELECT
    camera_id,
    COUNT(*) as alert_count,
    MAX(severity) as max_severity,
    MIN(created_at) as oldest_alert
FROM alerts
WHERE status = 'new'
GROUP BY camera_id
ORDER BY max_severity DESC, alert_count DESC;

-- 5. Time-series aggregation (events per minute, last hour)
SELECT
    DATE_TRUNC('minute', event_timestamp) as minute,
    COUNT(*) as event_count,
    AVG(motion_score) as avg_motion,
    SUM(object_count) as total_objects
FROM camera_events
WHERE camera_id = 'CAM001'
AND event_timestamp > NOW() - INTERVAL '1 hour'
GROUP BY DATE_TRUNC('minute', event_timestamp)
ORDER BY minute DESC;

-- 6. Multi-camera correlation (find simultaneous events)
WITH camera_events_windowed AS (
    SELECT
        camera_id,
        event_timestamp,
        event_type,
        LAG(event_timestamp) OVER (ORDER BY event_timestamp) as prev_event_time
    FROM camera_events
    WHERE event_type = 'motion'
    AND event_timestamp > NOW() - INTERVAL '10 minutes'
)
SELECT
    camera_id,
    event_timestamp,
    event_timestamp - prev_event_time as time_diff
FROM camera_events_windowed
WHERE event_timestamp - prev_event_time < INTERVAL '5 seconds'
ORDER BY event_timestamp;

-- 7. Scene change detection (duplicate frames)
SELECT
    camera_id,
    scene_hash,
    COUNT(*) as duplicate_count,
    MIN(event_timestamp) as first_seen,
    MAX(event_timestamp) as last_seen
FROM camera_events
WHERE camera_id = 'CAM001'
AND event_timestamp > NOW() - INTERVAL '1 hour'
AND scene_hash IS NOT NULL
GROUP BY camera_id, scene_hash
HAVING COUNT(*) > 5
ORDER BY duplicate_count DESC;
```

### 6.2 Common Write Patterns

```sql
-- ============================================================================
-- COMMON WRITE PATTERNS
-- ============================================================================

-- 1. Insert camera event (single)
INSERT INTO camera_events (
    camera_id, event_type, event_timestamp, frame_number,
    motion_score, objects_detected, object_count,
    dlpu_inference_ms, total_processing_ms
) VALUES (
    'CAM001', 'object_detected', NOW(), 12450,
    0.65, '[{"class":"person","confidence":0.92,"bbox":[100,200,50,150]}]'::jsonb, 1,
    48, 125
) RETURNING event_id;

-- 2. Batch insert (preferred for high throughput)
INSERT INTO camera_events (
    camera_id, event_type, event_timestamp, frame_number, motion_score, object_count
)
SELECT
    camera_id,
    'motion',
    ts,
    row_number() OVER (PARTITION BY camera_id ORDER BY ts),
    random() * 0.8,
    floor(random() * 5)::int
FROM generate_series(NOW() - INTERVAL '1 hour', NOW(), INTERVAL '100 ms') ts
CROSS JOIN (VALUES ('CAM001'), ('CAM002'), ('CAM003')) cameras(camera_id);

-- 3. Update event after Claude analysis
UPDATE camera_events
SET
    sent_to_claude = TRUE,
    claude_analysis_id = $1,
    updated_at = NOW()
WHERE event_id = $2;

-- 4. Insert Claude analysis
INSERT INTO claude_analyses (
    trigger_event_id, camera_id, event_timestamp,
    request_type, claude_response, primary_insight,
    risk_assessment, tokens_used, api_call_duration_ms
) VALUES (
    $1, $2, $3, $4, $5::jsonb, $6, $7, $8, $9
) RETURNING analysis_id;

-- 5. Create alert from analysis
INSERT INTO alerts (
    alert_type, severity, camera_id, trigger_event_id, claude_analysis_id,
    title, description, detected_objects, confidence_score
)
SELECT
    'anomaly',
    CASE
        WHEN risk_assessment = 'critical' THEN 5
        WHEN risk_assessment = 'high' THEN 4
        WHEN risk_assessment = 'medium' THEN 3
        ELSE 2
    END,
    camera_id,
    trigger_event_id,
    analysis_id,
    'Anomaly Detected: ' || pattern_type,
    primary_insight,
    objects_identified,
    pattern_confidence
FROM claude_analyses
WHERE should_alert = TRUE
AND NOT EXISTS (
    SELECT 1 FROM alerts a WHERE a.claude_analysis_id = claude_analyses.analysis_id
);
```

---

## 7. Backup & Restore

### 7.1 Backup Strategy

```bash
#!/bin/bash
# ============================================================================
# Axis I.S. Database Backup Script
# Runs daily at 2 AM via cron: 0 2 * * * /opt/axis-is/scripts/backup_db.sh
# ============================================================================

BACKUP_DIR="/var/backups/axis-is/postgres"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RETENTION_DAYS=30

# 1. Full database backup (compressed)
pg_dump -h localhost -U axion -d axion \
    --format=custom \
    --compress=9 \
    --file="${BACKUP_DIR}/axion_full_${TIMESTAMP}.dump"

# 2. Schema-only backup (for quick recovery)
pg_dump -h localhost -U axion -d axion \
    --schema-only \
    --file="${BACKUP_DIR}/axion_schema_${TIMESTAMP}.sql"

# 3. Partition-specific backups (hot partitions only)
for partition in $(psql -h localhost -U axion -d axion -tAc \
    "SELECT tablename FROM pg_tables
     WHERE tablename LIKE 'camera_events_%'
     AND tablename >= 'camera_events_' || TO_CHAR(NOW() - INTERVAL '7 days', 'YYYY_MM_DD')
     ORDER BY tablename DESC LIMIT 7")
do
    pg_dump -h localhost -U axion -d axion \
        --table="${partition}" \
        --format=custom \
        --compress=9 \
        --file="${BACKUP_DIR}/${partition}_${TIMESTAMP}.dump"
done

# 4. Cleanup old backups
find "${BACKUP_DIR}" -name "*.dump" -mtime +${RETENTION_DAYS} -delete
find "${BACKUP_DIR}" -name "*.sql" -mtime +${RETENTION_DAYS} -delete

# 5. Upload to S3 (optional)
if command -v aws &> /dev/null; then
    aws s3 sync "${BACKUP_DIR}" "s3://axion-backups/postgres/" \
        --exclude "*" \
        --include "*${TIMESTAMP}*"
fi

echo "Backup completed: ${TIMESTAMP}"
```

### 7.2 Restore Procedures

```bash
# ============================================================================
# RESTORE PROCEDURES
# ============================================================================

# 1. Full database restore (destructive)
dropdb -h localhost -U postgres axion
createdb -h localhost -U postgres axion
pg_restore -h localhost -U axion -d axion \
    --format=custom \
    --jobs=4 \
    /var/backups/axis-is/postgres/axion_full_20251123_020000.dump

# 2. Restore single partition (e.g., lost current day data)
pg_restore -h localhost -U axion -d axion \
    --table=camera_events_2025_11_23 \
    /var/backups/axis-is/postgres/camera_events_2025_11_23_020000.dump

# 3. Restore schema only (for new environment)
psql -h localhost -U axion -d axion \
    -f /var/backups/axis-is/postgres/axion_schema_20251123_020000.sql

# 4. Point-in-time recovery (requires WAL archiving)
pg_basebackup -h localhost -U postgres -D /var/lib/postgresql/data_restore
# Edit recovery.conf:
# restore_command = 'cp /var/lib/postgresql/wal_archive/%f %p'
# recovery_target_time = '2025-11-23 14:30:00'
```

### 7.3 Redis Backup

```bash
#!/bin/bash
# ============================================================================
# Redis Backup Script
# ============================================================================

REDIS_BACKUP_DIR="/var/backups/axis-is/redis"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# 1. Trigger Redis save
redis-cli BGSAVE

# Wait for save to complete
while [ $(redis-cli LASTSAVE) -eq $(redis-cli LASTSAVE) ]; do
    sleep 1
done

# 2. Copy RDB file
cp /var/lib/redis/dump.rdb "${REDIS_BACKUP_DIR}/dump_${TIMESTAMP}.rdb"

# 3. Compress
gzip "${REDIS_BACKUP_DIR}/dump_${TIMESTAMP}.rdb"

# 4. Cleanup old backups (keep 7 days)
find "${REDIS_BACKUP_DIR}" -name "dump_*.rdb.gz" -mtime +7 -delete

echo "Redis backup completed: ${TIMESTAMP}"
```

---

## 8. Monitoring & Alerting

### 8.1 Key Metrics to Monitor

**PostgreSQL:**
- Write throughput (inserts/second) - Target: 100/sec
- Query latency (p50, p95, p99) - Target: <50ms p95
- Cache hit ratio - Target: >99%
- Active connections - Alert if >80
- Replication lag (if using standby) - Alert if >5 seconds
- Table bloat - Alert if >20%
- Partition creation - Ensure 7 days ahead

**Redis:**
- Memory usage - Alert if >90%
- Evicted keys - Alert if >0 (should not happen)
- Connected clients - Alert if >5000
- Command latency - Target: <1ms p99
- Key expiration rate - Monitor for leaks

### 8.2 Health Check Queries

```sql
-- PostgreSQL health check
SELECT
    'healthy' AS status,
    current_database() AS database,
    version() AS version,
    pg_database_size(current_database()) AS size_bytes,
    (SELECT count(*) FROM pg_stat_activity WHERE state = 'active') AS active_queries
WHERE pg_is_in_recovery() = false;

-- Table health check
SELECT
    COUNT(*) AS partition_count,
    MAX(tablename) AS latest_partition
FROM pg_tables
WHERE tablename LIKE 'camera_events_%';
```

```redis
# Redis health check
PING
INFO stats
INFO memory
DBSIZE
```

---

## Summary

This comprehensive database schema provides:

1. **PostgreSQL** - Persistent event storage with time-series partitioning
   - ~8.6M events/day handled efficiently
   - 30-day hot, 90-day warm, 1-year cold retention
   - Optimized indexes for common queries
   - Automated partition management

2. **Redis** - Real-time state management
   - Camera state and metrics
   - DLPU scheduler coordination
   - Bandwidth allocation tracking
   - Scene memory and message queues
   - Rate limiting

3. **Performance** - Production-ready optimizations
   - Connection pooling (PgBouncer)
   - Materialized views for dashboards
   - Covering indexes for hot queries
   - Automated cleanup and archival

4. **Operations** - Complete lifecycle management
   - Migration scripts
   - Backup/restore procedures
   - Monitoring queries
   - Retention policies

**Next Steps:**
1. Review and customize retention policies for your use case
2. Set up pg_cron for automated maintenance tasks
3. Configure PgBouncer for connection pooling
4. Implement monitoring (Prometheus + Grafana recommended)
5. Test write performance with synthetic load (100 events/sec)
6. Set up replication for high availability (optional)

---

**Document Version:** 1.0.0
**Last Updated:** 2025-11-23
**Maintainer:** Axis I.S. Development Team
