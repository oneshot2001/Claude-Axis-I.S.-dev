/**
 * Axis I.S. - Detection Overlay
 * Fetches detection metadata and draws bounding boxes on canvas
 */

// COCO class labels for YOLOv5
const COCO_LABELS = [
    'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus', 'train', 'truck', 'boat',
    'traffic light', 'fire hydrant', 'stop sign', 'parking meter', 'bench', 'bird', 'cat',
    'dog', 'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra', 'giraffe', 'backpack',
    'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee', 'skis', 'snowboard', 'sports ball',
    'kite', 'baseball bat', 'baseball glove', 'skateboard', 'surfboard', 'tennis racket',
    'bottle', 'wine glass', 'cup', 'fork', 'knife', 'spoon', 'bowl', 'banana', 'apple',
    'sandwich', 'orange', 'broccoli', 'carrot', 'hot dog', 'pizza', 'donut', 'cake', 'chair',
    'couch', 'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop', 'mouse',
    'remote', 'keyboard', 'cell phone', 'microwave', 'oven', 'toaster', 'sink', 'refrigerator',
    'book', 'clock', 'vase', 'scissors', 'teddy bear', 'hair drier', 'toothbrush'
];

const DetectionOverlay = {
    canvas: null,
    ctx: null,
    videoStream: null,
    intervalId: null,
    pollingRate: 200, // 5 FPS detection refresh
    lastDetections: [],

    init() {
        this.canvas = document.getElementById('detectionCanvas');
        this.videoStream = document.getElementById('videoStream');

        if (!this.canvas) {
            console.warn('Detection canvas not found');
            return;
        }

        this.ctx = this.canvas.getContext('2d');
        this.resizeCanvas();

        // Resize canvas when window or video changes
        window.addEventListener('resize', () => this.resizeCanvas());
        if (this.videoStream) {
            this.videoStream.addEventListener('load', () => this.resizeCanvas());
        }

        // Start polling for detections
        this.fetchDetections();
        this.intervalId = setInterval(() => this.fetchDetections(), this.pollingRate);
    },

    resizeCanvas() {
        const container = document.getElementById('live-view-container');
        if (container) {
            this.canvas.width = container.clientWidth;
            this.canvas.height = container.clientHeight;
            // Redraw with last known detections
            this.draw(this.lastDetections);
        }
    },

    async fetchDetections() {
        try {
            const response = await fetch('/local/axis_is_poc/detections');
            if (!response.ok) return;

            const data = await response.json();

            // Filter valid detections (confidence between 0-1, reasonable coordinates)
            let validDetections = [];
            if (data.detections && Array.isArray(data.detections)) {
                validDetections = data.detections.filter(det => {
                    // Check for valid confidence (should be 0-1 range)
                    const conf = det.confidence;
                    if (conf === null || conf === undefined || conf < 0 || conf > 1) {
                        return false;
                    }
                    // Check for valid coordinates (should be 0-1 normalized or reasonable pixel values)
                    const x = det.x, y = det.y, w = det.width, h = det.height;
                    if (x === null || y === null || w === null || h === null) return false;
                    if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
                    if (x > 10000 || y > 10000 || w > 10000 || h > 10000) return false;
                    return true;
                });
            }

            this.lastDetections = validDetections;
            this.updateCount(validDetections.length, data.object_count || 0);
            this.draw(validDetections);

        } catch (error) {
            // Silent fail for smoother UX
            console.debug('Detection fetch error:', error);
        }
    },

    updateCount(validCount, totalCount) {
        const countEl = document.getElementById('detection-count');
        if (countEl) {
            if (validCount > 0) {
                countEl.textContent = `${validCount} detection${validCount !== 1 ? 's' : ''}`;
                countEl.parentElement.style.background = 'rgba(255, 210, 0, 0.9)';
                countEl.parentElement.style.color = '#000';
            } else {
                countEl.textContent = 'No detections';
                countEl.parentElement.style.background = 'rgba(0, 0, 0, 0.7)';
                countEl.parentElement.style.color = '#fff';
            }
        }
    },

    draw(detections) {
        if (!this.ctx || !this.canvas) return;

        const width = this.canvas.width;
        const height = this.canvas.height;

        // Clear canvas
        this.ctx.clearRect(0, 0, width, height);

        if (!detections || detections.length === 0) return;

        // Draw each detection
        detections.forEach(det => {
            let x, y, w, h;

            // Handle different coordinate formats
            // If values are 0-1, they're normalized; otherwise assume pixels
            if (det.x <= 1 && det.y <= 1 && det.width <= 1 && det.height <= 1) {
                // Normalized coordinates (0-1)
                x = det.x * width;
                y = det.y * height;
                w = det.width * width;
                h = det.height * height;
            } else {
                // Assume pixel coordinates, scale to canvas
                // Detection model likely uses 416x416 or 640x640
                const modelSize = 416;
                x = (det.x / modelSize) * width;
                y = (det.y / modelSize) * height;
                w = (det.width / modelSize) * width;
                h = (det.height / modelSize) * height;
            }

            // Get label from class_id
            const label = COCO_LABELS[det.class_id] || `class_${det.class_id}`;
            const confidence = (det.confidence * 100).toFixed(0);

            // Draw bounding box
            this.ctx.strokeStyle = '#FFD200'; // Axis Yellow
            this.ctx.lineWidth = 3;
            this.ctx.strokeRect(x, y, w, h);

            // Draw label background
            const labelText = `${label} ${confidence}%`;
            this.ctx.font = 'bold 14px Arial';
            const textWidth = this.ctx.measureText(labelText).width;

            this.ctx.fillStyle = '#FFD200';
            this.ctx.fillRect(x, y - 22, textWidth + 10, 22);

            // Draw label text
            this.ctx.fillStyle = '#000000';
            this.ctx.fillText(labelText, x + 5, y - 6);
        });
    },

    destroy() {
        if (this.intervalId) {
            clearInterval(this.intervalId);
            this.intervalId = null;
        }
    }
};

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    DetectionOverlay.init();
});
