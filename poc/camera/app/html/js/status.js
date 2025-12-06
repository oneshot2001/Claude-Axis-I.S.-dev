/**
 * Axis I.S. - Status Polling
 * Fetches system status and updates the UI
 */

const StatusManager = {
    intervalId: null,
    pollingRate: 2000, // 2 seconds

    init() {
        this.fetchStatus();
        this.intervalId = setInterval(() => this.fetchStatus(), this.pollingRate);
    },

    async fetchStatus() {
        try {
            const response = await fetch('/local/axis_is_poc/app_status');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const data = await response.json();
            this.updateUI(data);
            this.updateConnectionStatus(true);
        } catch (error) {
            console.error('Error fetching status:', error);
            this.updateConnectionStatus(false);
        }
    },

    updateUI(data) {
        // Update Header Info
        document.getElementById('camera-id').textContent = data.camera_id || 'Unknown Camera';
        document.getElementById('app-version').textContent = `v${data.version}`;

        // Update Metrics
        document.getElementById('metric-fps').textContent = data.actual_fps ? data.actual_fps.toFixed(1) : '0.0';
        document.getElementById('metric-target-fps').textContent = data.target_fps || '0';
        document.getElementById('metric-frames').textContent = data.frames_processed || '0';
        
        // Format Uptime
        const uptime = data.uptime_seconds || 0;
        const hours = Math.floor(uptime / 3600);
        const minutes = Math.floor((uptime % 3600) / 60);
        document.getElementById('metric-uptime').textContent = `${hours}h ${minutes}m`;

        // Update Module List
        const moduleList = document.getElementById('module-list');
        moduleList.innerHTML = ''; // Clear existing

        if (data.modules && Array.isArray(data.modules)) {
            data.modules.forEach(mod => {
                const li = document.createElement('li');
                li.className = 'module-item';
                li.innerHTML = `
                    <span class="module-name">${mod.name}</span>
                    <div class="module-meta">
                        <span class="version">v${mod.version}</span>
                        <span class="priority">P${mod.priority}</span>
                    </div>
                `;
                moduleList.appendChild(li);
            });
        }
    },

    updateConnectionStatus(online) {
        const badge = document.getElementById('status-badge');
        if (online) {
            badge.textContent = 'Online';
            badge.classList.remove('offline');
            badge.classList.add('online');
        } else {
            badge.textContent = 'Offline';
            badge.classList.remove('online');
            badge.classList.add('offline');
        }
    }
};

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    StatusManager.init();
});

