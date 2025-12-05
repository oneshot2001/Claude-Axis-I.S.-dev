#!/bin/bash
#
# Copy template files from make_acap/mqtt to POC
#

set -e

echo "====== Copying Template Files to POC ======"

MQTT_TEMPLATE="../make_acap/mqtt/app"
POC_APP="camera/app"

if [ ! -d "$MQTT_TEMPLATE" ]; then
    echo "Error: MQTT template directory not found at $MQTT_TEMPLATE"
    exit 1
fi

if [ ! -d "$POC_APP" ]; then
    echo "Error: POC app directory not found at $POC_APP"
    exit 1
fi

# Copy framework files
echo "Copying ACAP framework..."
cp "$MQTT_TEMPLATE/ACAP.c" "$POC_APP/"
cp "$MQTT_TEMPLATE/ACAP.h" "$POC_APP/"

echo "Copying MQTT client..."
cp "$MQTT_TEMPLATE/MQTT.c" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTT.h" "$POC_APP/"

echo "Copying certificate handler..."
cp "$MQTT_TEMPLATE/CERTS.c" "$POC_APP/"
cp "$MQTT_TEMPLATE/CERTS.h" "$POC_APP/"

echo "Copying JSON library..."
cp "$MQTT_TEMPLATE/cJSON.c" "$POC_APP/"
cp "$MQTT_TEMPLATE/cJSON.h" "$POC_APP/"

echo "Copying MQTT headers..."
cp "$MQTT_TEMPLATE/MQTTAsync.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTClient.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTClientPersistence.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTExportDeclarations.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTProperties.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTReasonCodes.h" "$POC_APP/"
cp "$MQTT_TEMPLATE/MQTTSubscribeOpts.h" "$POC_APP/"

echo ""
echo "✓ All template files copied successfully"
echo ""
echo "Files copied:"
echo "  - ACAP.c/h (ACAP framework)"
echo "  - MQTT.c/h (MQTT client wrapper)"
echo "  - CERTS.c/h (Certificate handling)"
echo "  - cJSON.c/h (JSON library)"
echo "  - MQTT*.h (Paho MQTT headers)"
echo ""
echo "Next steps:"
echo "  1. cd camera"
echo "  2. ./build.sh"
echo "  3. cd .."
echo "  4. ./deploy.sh <camera-ip>"
