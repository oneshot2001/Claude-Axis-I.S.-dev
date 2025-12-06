/**
 * Axis I.S. - Quick Actions
 * Handles restart service and download logs functionality
 */

const Actions = {
    /**
     * Restart the ACAP service via Axis control CGI
     */
    async restartService() {
        const btn = document.getElementById('btn-restart');
        const originalText = btn.textContent;

        if (!confirm('Are you sure you want to restart the Axis I.S. service?')) {
            return;
        }

        btn.disabled = true;
        btn.textContent = 'Restarting...';

        try {
            // Use the Axis control CGI to restart the application
            const response = await fetch('/axis-cgi/applications/control.cgi?action=restart&package=axis_is_poc');

            if (response.ok) {
                btn.textContent = 'Restarted!';
                // Page will likely disconnect as the app restarts
                setTimeout(() => {
                    btn.textContent = 'Reconnecting...';
                    // Try to reload after service comes back
                    setTimeout(() => {
                        window.location.reload();
                    }, 3000);
                }, 1000);
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            console.error('Restart failed:', error);
            btn.textContent = originalText;
            btn.disabled = false;
            alert('Failed to restart service. You may need to restart from the camera Apps page.');
        }
    },

    /**
     * Download application logs from syslog
     */
    async downloadLogs() {
        const btn = document.getElementById('btn-logs');
        const originalText = btn.textContent;

        btn.disabled = true;
        btn.textContent = 'Fetching logs...';

        try {
            // Fetch logs from our logs endpoint
            const response = await fetch('/local/axis_is_poc/logs');

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const logData = await response.text();

            // Create a blob and download
            const blob = new Blob([logData], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `axis_is_poc_logs_${new Date().toISOString().slice(0, 19).replace(/[:-]/g, '')}.txt`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);

            btn.textContent = 'Downloaded!';
            setTimeout(() => {
                btn.textContent = originalText;
                btn.disabled = false;
            }, 2000);
        } catch (error) {
            console.error('Log download failed:', error);
            btn.textContent = originalText;
            btn.disabled = false;
            alert('Failed to download logs. The logs endpoint may not be available.');
        }
    }
};
