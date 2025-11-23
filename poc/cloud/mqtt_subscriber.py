#!/usr/bin/env python3
"""
AXION POC - Cloud-side MQTT Subscriber

Subscribes to camera metadata streams and displays real-time statistics.
Validates POC success criteria:
- Message reception rate (target: 10 FPS per camera)
- Sequence number continuity (detects dropped frames)
- Inference performance (latency < 200ms)
- Object detection functionality
"""

import asyncio
import json
import time
import sys
from datetime import datetime
from collections import defaultdict
import argparse

try:
    import aiomqtt
except ImportError:
    print("Error: aiomqtt not installed. Run: pip install aiomqtt")
    sys.exit(1)


class AXIONSubscriber:
    """Cloud-side MQTT subscriber for AXION camera metadata"""

    def __init__(self, broker="localhost", port=1883, debug=False):
        self.broker = broker
        self.port = port
        self.debug = debug

        # Global statistics
        self.message_count = 0
        self.start_time = time.time()

        # Per-camera tracking
        self.camera_stats = defaultdict(lambda: {
            'last_sequence': -1,
            'message_count': 0,
            'sequence_gaps': 0,
            'total_inference_ms': 0,
            'total_objects': 0,
            'total_motion': 0.0,
            'first_message_time': None,
            'last_message_time': None
        })

    async def run(self):
        """Main subscriber loop"""
        print(f"AXION POC Cloud Subscriber v1.0")
        print(f"Connecting to MQTT broker at {self.broker}:{self.port}")
        print("="*60)

        try:
            async with aiomqtt.Client(self.broker, self.port) as client:
                # Subscribe to all AXION topics
                await client.subscribe("axion/camera/+/metadata")
                await client.subscribe("axion/camera/+/status")

                print("✓ Subscribed to AXION topics")
                print("  - axion/camera/+/metadata")
                print("  - axion/camera/+/status")
                print("\nWaiting for messages...\n")

                async for message in client.messages:
                    self.handle_message(message)

        except KeyboardInterrupt:
            print("\n\nShutting down...")
            self.print_final_stats()
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)

    def handle_message(self, message):
        """Process incoming MQTT message"""
        topic = str(message.topic)
        payload = message.payload.decode()

        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            print(f"⚠️  Invalid JSON from {topic}")
            if self.debug:
                print(f"    Payload: {payload}")
            return

        self.message_count += 1

        if "/metadata" in topic:
            self.handle_metadata(topic, data)
        elif "/status" in topic:
            self.handle_status(topic, data)
        else:
            if self.debug:
                print(f"Unknown topic: {topic}")

    def handle_metadata(self, topic, data):
        """Process metadata message from camera"""
        # Extract camera ID from topic (axion/camera/<camera_id>/metadata)
        topic_parts = topic.split("/")
        if len(topic_parts) >= 3:
            camera_id = topic_parts[2]
        else:
            camera_id = "unknown"

        stats = self.camera_stats[camera_id]

        # Track timing
        if stats['first_message_time'] is None:
            stats['first_message_time'] = time.time()
        stats['last_message_time'] = time.time()

        # Extract fields
        seq = data.get("seq", 0)
        timestamp_ms = data.get("timestamp", 0)
        inference = data.get("inference", {})
        inference_ms = inference.get("time_ms", 0)
        detections = data.get("detections", [])
        motion = data.get("motion", {})
        motion_score = motion.get("score", 0.0)

        # Update statistics
        stats['message_count'] += 1
        stats['total_inference_ms'] += inference_ms
        stats['total_objects'] += len(detections)
        stats['total_motion'] += motion_score

        # Detect sequence gaps
        if stats['last_sequence'] >= 0:
            expected = stats['last_sequence'] + 1
            if seq != expected:
                gap = seq - expected
                stats['sequence_gaps'] += 1
                print(f"⚠️  [{camera_id}] SEQUENCE GAP: expected {expected}, got {seq} (gap={gap})")

        stats['last_sequence'] = seq

        # Print summary periodically
        if seq % 10 == 0 and seq > 0:
            self.print_camera_stats(camera_id, stats, detections, motion_score, inference_ms)

    def handle_status(self, topic, data):
        """Process status message from camera"""
        topic_parts = topic.split("/")
        camera_id = topic_parts[2] if len(topic_parts) >= 3 else "unknown"

        state = data.get("state", "unknown")
        timestamp = data.get("timestamp", 0)
        version = data.get("version", "unknown")

        dt = datetime.fromtimestamp(timestamp) if timestamp else datetime.now()

        if state == "online":
            print(f"📡 [{camera_id}] ONLINE at {dt.strftime('%H:%M:%S')} (v{version})")
        elif state == "offline":
            print(f"📴 [{camera_id}] OFFLINE at {dt.strftime('%H:%M:%S')}")
        else:
            print(f"ℹ️  [{camera_id}] Status: {state}")

    def print_camera_stats(self, camera_id, stats, detections, motion_score, inference_ms):
        """Print current statistics for a camera"""
        msg_count = stats['message_count']
        avg_inference = stats['total_inference_ms'] / msg_count if msg_count > 0 else 0

        # Calculate actual FPS
        if stats['first_message_time'] and stats['last_message_time']:
            elapsed = stats['last_message_time'] - stats['first_message_time']
            actual_fps = msg_count / elapsed if elapsed > 0 else 0
        else:
            actual_fps = 0

        # Format detection info
        det_classes = [d.get('class_id', -1) for d in detections]
        det_confidences = [d.get('confidence', 0.0) for d in detections]

        print(f"[{camera_id}] "
              f"Msg={msg_count:4d} "
              f"FPS={actual_fps:5.1f} "
              f"Inf={inference_ms:3d}ms "
              f"Avg={avg_inference:5.1f}ms "
              f"Obj={len(detections):2d} "
              f"Mot={motion_score:4.2f} "
              f"Gaps={stats['sequence_gaps']:2d}")

        if self.debug and detections:
            print(f"    Detections: classes={det_classes}, conf={[f'{c:.2f}' for c in det_confidences]}")

    def print_final_stats(self):
        """Print final statistics on shutdown"""
        print("\n" + "="*60)
        print("FINAL STATISTICS")
        print("="*60)

        total_elapsed = time.time() - self.start_time
        global_msg_rate = self.message_count / total_elapsed if total_elapsed > 0 else 0

        print(f"\nGlobal:")
        print(f"  Total Messages: {self.message_count}")
        print(f"  Total Runtime: {total_elapsed:.1f}s")
        print(f"  Overall Rate: {global_msg_rate:.1f} msg/s")

        print(f"\nPer-Camera:")
        for camera_id, stats in sorted(self.camera_stats.items()):
            msg_count = stats['message_count']
            if msg_count == 0:
                continue

            if stats['first_message_time'] and stats['last_message_time']:
                elapsed = stats['last_message_time'] - stats['first_message_time']
                fps = msg_count / elapsed if elapsed > 0 else 0
            else:
                fps = 0

            avg_inference = stats['total_inference_ms'] / msg_count
            avg_objects = stats['total_objects'] / msg_count
            avg_motion = stats['total_motion'] / msg_count

            print(f"\n  {camera_id}:")
            print(f"    Messages: {msg_count}")
            print(f"    Avg FPS: {fps:.2f}")
            print(f"    Avg Inference: {avg_inference:.1f}ms")
            print(f"    Avg Objects: {avg_objects:.1f}")
            print(f"    Avg Motion: {avg_motion:.3f}")
            print(f"    Sequence Gaps: {stats['sequence_gaps']}")

            # Success criteria
            print(f"\n    Success Criteria:")
            print(f"      FPS ≥ 5: {'✓' if fps >= 5 else '✗'} ({fps:.1f})")
            print(f"      Inference < 200ms: {'✓' if avg_inference < 200 else '✗'} ({avg_inference:.1f}ms)")
            print(f"      No gaps: {'✓' if stats['sequence_gaps'] == 0 else '⚠️'} ({stats['sequence_gaps']} gaps)")


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="AXION POC Cloud Subscriber")
    parser.add_argument("--broker", default="localhost", help="MQTT broker address")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--debug", action="store_true", help="Enable debug output")

    args = parser.parse_args()

    subscriber = AXIONSubscriber(
        broker=args.broker,
        port=args.port,
        debug=args.debug
    )

    try:
        asyncio.run(subscriber.run())
    except KeyboardInterrupt:
        print("\nExiting...")
        sys.exit(0)


if __name__ == "__main__":
    main()
