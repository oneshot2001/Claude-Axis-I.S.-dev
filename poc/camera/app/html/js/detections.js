/**
 * Axis I.S. - Detection Overlay
 * Fetches detection metadata and draws on canvas
 */

const DetectionOverlay = {
    canvas: null,
    ctx: null,
    intervalId: null,
    pollingRate: 100, // 10 FPS refresh

    init() {
        this.canvas = document.getElementById('detectionCanvas');
        if (!this.canvas) return;
        
        this.ctx = this.canvas.getContext('2d');
        this.resizeCanvas();
        
        window.addEventListener('resize', () => this.resizeCanvas());
        this.intervalId = setInterval(() => this.fetchDetections(), this.pollingRate);
    },

    resizeCanvas() {
        const container = this.canvas.parentElement;
        this.canvas.width = container.clientWidth;
        this.canvas.height = container.clientHeight;
    },

    async fetchDetections() {
        try {
            const response = await fetch('/local/axis_is_poc/detections');
            if (!response.ok) return; // Silent fail for detections to keep logs clean
            
            const data = await response.json();
            this.draw(data);
        } catch (error) {
            // Ignore errors for smoother UX
        }
    },

    draw(data) {
        const width = this.canvas.width;
        const height = this.canvas.height;

        // Clear canvas
        this.ctx.clearRect(0, 0, width, height);

        if (!data || !data.objects) return;

        // Draw each object
        data.objects.forEach(obj => {
            // Normalize coordinates (assuming API returns 0-1 normalized relative coords)
            // Adjust based on actual API output. Assuming [x, y, w, h] or similar.
            // The previous C code suggests it just passes the struct. 
            // Let's assume standard normalized center-x, center-y, width, height or top-left.
            // For this POC, we'll assume the C structure `bbox` has {x, y, w, h} in normalized form.
            
            if (!obj.bbox) return;

            const x = obj.bbox.x * width;
            const y = obj.bbox.y * height;
            const w = obj.bbox.w * width;
            const h = obj.bbox.h * height;

            // Draw Box
            this.ctx.strokeStyle = '#FFD200'; // Axis Yellow
            this.ctx.lineWidth = 2;
            this.ctx.strokeRect(x, y, w, h);

            // Draw Label Background
            this.ctx.fillStyle = '#FFD200';
            const label = `${obj.label} (${(obj.confidence * 100).toFixed(0)}%)`;
            const textWidth = this.ctx.measureText(label).width;
            this.ctx.fillRect(x, y - 20, textWidth + 10, 20);

            // Draw Label Text
            this.ctx.fillStyle = '#000000';
            this.ctx.font = '12px Arial';
            this.ctx.fillText(label, x + 5, y - 5);
        });
    }
};

document.addEventListener('DOMContentLoaded', () => {
    DetectionOverlay.init();
});

