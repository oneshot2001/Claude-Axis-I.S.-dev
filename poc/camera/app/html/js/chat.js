/**
 * Axis I.S. - AI Chat Interface
 * Natural language queries for scene analysis via Claude/Gemini
 */

const Chat = {
    // Cloud service URL - configurable
    cloudServiceUrl: 'http://localhost:8000',
    cameraId: 'axis-camera-001',
    isProcessing: false,
    conversationHistory: [],

    init() {
        // Get camera ID from status
        this.updateCameraId();

        // Check cloud service availability
        this.checkCloudStatus();
    },

    async updateCameraId() {
        try {
            const response = await fetch('/local/axis_is_poc/app_status');
            if (response.ok) {
                const data = await response.json();
                if (data.camera_id) {
                    this.cameraId = data.camera_id;
                }
            }
        } catch (e) {
            console.debug('Could not fetch camera ID');
        }
    },

    async checkCloudStatus() {
        const statusEl = document.getElementById('ai-status');
        try {
            const response = await fetch(`${this.cloudServiceUrl}/health`, {
                method: 'GET',
                mode: 'cors'
            });
            if (response.ok) {
                const data = await response.json();
                statusEl.textContent = `Connected - ${data.ai_provider || 'Claude'}`;
                statusEl.style.color = '#27ae60';
            } else {
                throw new Error('Not OK');
            }
        } catch (e) {
            statusEl.textContent = 'Cloud service offline';
            statusEl.style.color = '#e74c3c';
        }
    },

    async sendMessage() {
        const input = document.getElementById('chat-input');
        const message = input.value.trim();

        if (!message || this.isProcessing) return;

        // Clear input and show user message
        input.value = '';
        this.addMessage('user', message);

        // Show typing indicator
        this.isProcessing = true;
        this.showTyping();

        try {
            // Get current detection context
            const context = await this.getDetectionContext();

            // Send to cloud service
            const response = await this.queryAI(message, context);

            // Remove typing indicator and show response
            this.hideTyping();
            this.addMessage('assistant', response);

        } catch (error) {
            this.hideTyping();
            this.addMessage('assistant', `Sorry, I couldn't process that request. ${error.message}`, true);
        }

        this.isProcessing = false;
    },

    async getDetectionContext() {
        // Gather current detection data for context
        const context = {
            camera_id: this.cameraId,
            timestamp: new Date().toISOString(),
            detections: [],
            metadata: {}
        };

        try {
            // Get current detections
            const detectResponse = await fetch('/local/axis_is_poc/detections');
            if (detectResponse.ok) {
                const data = await detectResponse.json();
                context.detections = data.detections || [];
                context.metadata = {
                    motion_score: data.motion_score,
                    object_count: data.object_count,
                    scene_hash: data.scene_hash
                };
            }
        } catch (e) {
            console.debug('Could not fetch detection context');
        }

        try {
            // Get app status
            const statusResponse = await fetch('/local/axis_is_poc/app_status');
            if (statusResponse.ok) {
                const status = await statusResponse.json();
                context.app_status = {
                    fps: status.actual_fps,
                    uptime: status.uptime_seconds,
                    modules: status.modules
                };
            }
        } catch (e) {
            console.debug('Could not fetch app status');
        }

        return context;
    },

    async queryAI(question, context) {
        // First try the cloud service chat endpoint
        try {
            const response = await fetch(`${this.cloudServiceUrl}/chat`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                mode: 'cors',
                body: JSON.stringify({
                    camera_id: this.cameraId,
                    question: question,
                    context: context,
                    conversation_history: this.conversationHistory.slice(-6) // Last 3 exchanges
                })
            });

            if (response.ok) {
                const data = await response.json();

                // Add to conversation history
                this.conversationHistory.push({ role: 'user', content: question });
                this.conversationHistory.push({ role: 'assistant', content: data.response });

                return data.response;
            } else {
                const error = await response.json();
                throw new Error(error.detail || 'Cloud service error');
            }
        } catch (fetchError) {
            // If cloud service unavailable, provide local analysis
            console.warn('Cloud service unavailable, using local analysis');
            return this.localAnalysis(question, context);
        }
    },

    localAnalysis(question, context) {
        // Fallback local analysis when cloud is unavailable
        const q = question.toLowerCase();
        const detections = context.detections || [];

        // Filter valid detections
        const validDetections = detections.filter(d =>
            d.confidence > 0 && d.confidence <= 1 &&
            d.x >= 0 && d.y >= 0
        );

        // COCO labels for reference
        const LABELS = [
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

        // Count by class
        const counts = {};
        validDetections.forEach(d => {
            const label = LABELS[d.class_id] || `object_${d.class_id}`;
            counts[label] = (counts[label] || 0) + 1;
        });

        // Generate response based on question type
        if (q.includes('how many') || q.includes('count')) {
            if (q.includes('people') || q.includes('person')) {
                const count = counts['person'] || 0;
                return `I can see ${count} ${count === 1 ? 'person' : 'people'} in the current frame.`;
            }
            if (q.includes('car') || q.includes('vehicle')) {
                const cars = (counts['car'] || 0) + (counts['truck'] || 0) + (counts['bus'] || 0);
                return `I can see ${cars} vehicle${cars !== 1 ? 's' : ''} in the current frame.`;
            }
            const total = validDetections.length;
            return `I can see ${total} object${total !== 1 ? 's' : ''} in the current frame.`;
        }

        if (q.includes('what') && (q.includes('see') || q.includes('detect'))) {
            if (validDetections.length === 0) {
                return "I don't see any objects in the current frame. The scene appears to be empty or the confidence threshold hasn't been met.";
            }
            const items = Object.entries(counts)
                .map(([label, count]) => `${count} ${label}${count > 1 ? 's' : ''}`)
                .join(', ');
            return `Currently I can see: ${items}.`;
        }

        if (q.includes('describe') || q.includes('scene')) {
            const motion = context.metadata?.motion_score || 0;
            const motionDesc = motion > 0.5 ? 'significant movement' : motion > 0.2 ? 'some movement' : 'little to no movement';

            if (validDetections.length === 0) {
                return `The scene shows ${motionDesc}. No objects are currently being detected above the confidence threshold.`;
            }

            const items = Object.entries(counts)
                .map(([label, count]) => `${count} ${label}${count > 1 ? 's' : ''}`)
                .join(', ');
            return `The scene shows ${motionDesc}. I can detect: ${items}. The system is processing at ${context.app_status?.fps?.toFixed(1) || 'N/A'} FPS.`;
        }

        if (q.includes('suspicious') || q.includes('unusual') || q.includes('activity')) {
            const motion = context.metadata?.motion_score || 0;
            if (motion > 0.7) {
                return "There is significant motion detected in the scene, which could indicate activity worth monitoring.";
            }
            return "The scene appears normal with no unusual activity patterns detected at this time.";
        }

        // Default response
        return `I'm running in offline mode (cloud service unavailable). I can see ${validDetections.length} objects. For advanced analysis, please ensure the cloud service is running at ${this.cloudServiceUrl}.`;
    },

    addMessage(role, content, isError = false) {
        const container = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.className = `chat-message ${role}`;

        const bgColor = role === 'user' ? '#FFD200' : (isError ? '#fee' : '#e8e8e8');
        const textColor = role === 'user' ? '#000' : (isError ? '#c00' : '#333');
        const align = role === 'user' ? 'margin-left: auto;' : '';

        div.style.cssText = `margin-bottom: 10px; padding: 8px 12px; background: ${bgColor}; color: ${textColor}; border-radius: 8px; max-width: 85%; ${align}`;

        const label = document.createElement('div');
        label.style.cssText = 'font-size: 0.7rem; color: #666; margin-bottom: 4px;';
        label.textContent = role === 'user' ? 'You' : 'AI Assistant';

        const text = document.createElement('div');
        text.style.cssText = 'white-space: pre-wrap; word-wrap: break-word;';
        text.textContent = content;

        div.appendChild(label);
        div.appendChild(text);
        container.appendChild(div);

        // Scroll to bottom
        container.scrollTop = container.scrollHeight;
    },

    showTyping() {
        const container = document.getElementById('chat-messages');
        const div = document.createElement('div');
        div.id = 'typing-indicator';
        div.style.cssText = 'margin-bottom: 10px; padding: 8px 12px; background: #e8e8e8; border-radius: 8px; max-width: 85%;';
        div.innerHTML = '<div style="font-size: 0.7rem; color: #666; margin-bottom: 4px;">AI Assistant</div><div>Thinking<span class="dots">...</span></div>';
        container.appendChild(div);
        container.scrollTop = container.scrollHeight;

        // Animate dots
        let dots = 0;
        this.typingInterval = setInterval(() => {
            dots = (dots + 1) % 4;
            const dotsEl = div.querySelector('.dots');
            if (dotsEl) dotsEl.textContent = '.'.repeat(dots + 1);
        }, 400);
    },

    hideTyping() {
        const indicator = document.getElementById('typing-indicator');
        if (indicator) indicator.remove();
        if (this.typingInterval) {
            clearInterval(this.typingInterval);
            this.typingInterval = null;
        }
    }
};

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    Chat.init();
});
