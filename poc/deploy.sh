#!/bin/bash
#
# AXION POC Deployment Script
# Deploys the ACAP to an Axis camera
#

set -e

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <camera-ip> [username] [password]"
    echo ""
    echo "Example:"
    echo "  $0 192.168.1.101"
    echo "  $0 192.168.1.101 root mypassword"
    exit 1
fi

CAMERA_IP=$1
USERNAME="${2:-root}"
PASSWORD="${3:-pass}"

# Find .eap file
EAP_FILE=$(ls camera/*.eap 2>/dev/null | head -n 1)

if [ -z "$EAP_FILE" ]; then
    echo "Error: No .eap file found in camera/ directory"
    echo "Run ./camera/build.sh first"
    exit 1
fi

echo "====== AXION POC Deployment ======"
echo "Camera IP: $CAMERA_IP"
echo "Username: $USERNAME"
echo "ACAP File: $EAP_FILE"
echo ""

# Test connectivity
echo "Testing connectivity to camera..."
if ! ping -c 1 -W 2 "$CAMERA_IP" > /dev/null 2>&1; then
    echo "⚠️  Warning: Camera not responding to ping"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Upload ACAP
echo ""
echo "Uploading ACAP to camera..."
RESPONSE=$(curl -s -w "\n%{http_code}" \
    -u "$USERNAME:$PASSWORD" \
    -F "file=@$EAP_FILE" \
    "http://$CAMERA_IP/axis-cgi/applications/upload.cgi")

HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)
BODY=$(echo "$RESPONSE" | head -n -1)

if [ "$HTTP_CODE" != "200" ]; then
    echo "❌ Upload failed (HTTP $HTTP_CODE)"
    echo "$BODY"
    exit 1
fi

echo "✓ Upload successful"

# Wait for installation
echo ""
echo "Waiting for installation to complete..."
sleep 3

# Get application ID (usually matches package name)
APP_ID="axion_poc"

# Start the application
echo ""
echo "Starting application..."
START_RESPONSE=$(curl -s -w "\n%{http_code}" \
    -u "$USERNAME:$PASSWORD" \
    "http://$CAMERA_IP/axis-cgi/applications/control.cgi?action=start&package=$APP_ID")

START_HTTP_CODE=$(echo "$START_RESPONSE" | tail -n 1)

if [ "$START_HTTP_CODE" = "200" ]; then
    echo "✓ Application started"
else
    echo "⚠️  Warning: Could not start application (HTTP $START_HTTP_CODE)"
    echo "   You may need to start it manually via the camera web interface"
fi

# Wait for app to initialize
echo ""
echo "Waiting for application to initialize..."
sleep 5

# Check status endpoint
echo ""
echo "Checking application status..."
STATUS_RESPONSE=$(curl -s -w "\n%{http_code}" \
    -u "$USERNAME:$PASSWORD" \
    "http://$CAMERA_IP/local/$APP_ID/status" 2>/dev/null)

STATUS_HTTP_CODE=$(echo "$STATUS_RESPONSE" | tail -n 1)
STATUS_BODY=$(echo "$STATUS_RESPONSE" | head -n -1)

if [ "$STATUS_HTTP_CODE" = "200" ]; then
    echo "✓ Application is running"
    echo ""
    echo "Status:"
    echo "$STATUS_BODY" | python3 -m json.tool 2>/dev/null || echo "$STATUS_BODY"
else
    echo "⚠️  Could not get status (HTTP $STATUS_HTTP_CODE)"
    echo "   The application may still be starting up"
fi

echo ""
echo "====== Deployment Complete ======"
echo ""
echo "Next steps:"
echo "1. Check camera syslog for errors:"
echo "   ssh $USERNAME@$CAMERA_IP"
echo "   tail -f /var/log/messages | grep axion_poc"
echo ""
echo "2. Monitor MQTT messages:"
echo "   cd poc/cloud"
echo "   python mqtt_subscriber.py --broker <broker-ip>"
echo ""
echo "3. View status endpoint:"
echo "   curl -u $USERNAME:*** http://$CAMERA_IP/local/$APP_ID/status | jq"
echo ""
echo "4. View metadata endpoint:"
echo "   curl -u $USERNAME:*** http://$CAMERA_IP/local/$APP_ID/metadata | jq"
