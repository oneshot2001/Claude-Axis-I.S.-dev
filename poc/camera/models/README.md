# Axis I.S. POC - Model Files

## YOLOv5n INT8 Model

The Axis I.S. POC requires a YOLOv5n model quantized to INT8 for optimal performance on ARTPEC-9 DLPU.

### Download Model

You can obtain the model from one of these sources:

#### Option 1: Axis Model Zoo (Recommended)
```bash
# Visit Axis Model Zoo repository
# https://github.com/AxisCommunications/axis-model-zoo

# Download YOLOv5n INT8 TFLite model for object detection
wget https://github.com/AxisCommunications/axis-model-zoo/releases/download/v1.0/yolov5n_416_416_int8.tflite

# Place in this directory
cp yolov5n_416_416_int8.tflite /usr/local/packages/axis_is_poc/models/
```

#### Option 2: Convert from PyTorch
```bash
# Clone YOLOv5 repository
git clone https://github.com/ultralytics/yolov5
cd yolov5

# Export to TFLite with INT8 quantization
python export.py --weights yolov5n.pt --include tflite --int8 --img 416
```

### Model Specifications

**Input:**
- Format: INT8 (quantized)
- Shape: [1, 416, 416, 3]
- Color: RGB
- Range: 0-255 (INT8)

**Output:**
- Format: FLOAT32
- Shape: [1, 25200, 85]
- 25200 = (80x80 + 40x40 + 20x20) × 3 anchors
- 85 = (x, y, w, h, objectness, 80 class scores)

**Classes (COCO Dataset):**
0: person, 1: bicycle, 2: car, 3: motorcycle, 4: airplane, 5: bus, 6: train, 7: truck, 8: boat, 9: traffic light, 10: fire hydrant, 11: stop sign, 12: parking meter, 13: bench, 14: bird, 15: cat, 16: dog, 17: horse, 18: sheep, 19: cow, 20: elephant, 21: bear, 22: zebra, 23: giraffe, 24: backpack, 25: umbrella, 26: handbag, 27: tie, 28: suitcase, 29: frisbee, 30: skis, 31: snowboard, 32: sports ball, 33: kite, 34: baseball bat, 35: baseball glove, 36: skateboard, 37: surfboard, 38: tennis racket, 39: bottle, 40: wine glass, 41: cup, 42: fork, 43: knife, 44: spoon, 45: bowl, 46: banana, 47: apple, 48: sandwich, 49: orange, 50: broccoli, 51: carrot, 52: hot dog, 53: pizza, 54: donut, 55: cake, 56: chair, 57: couch, 58: potted plant, 59: bed, 60: dining table, 61: toilet, 62: tv, 63: laptop, 64: mouse, 65: remote, 66: keyboard, 67: cell phone, 68: microwave, 69: oven, 70: toaster, 71: sink, 72: refrigerator, 73: book, 74: clock, 75: vase, 76: scissors, 77: teddy bear, 78: hair drier, 79: toothbrush

### Performance Targets

On ARTPEC-9 DLPU:
- **Inference Time:** 30-50ms per frame
- **Throughput:** 20-25 FPS (single camera)
- **Memory Usage:** ~120 MB
- **Model Size:** ~1 MB (INT8 quantized)

### Deployment

Once downloaded, the model should be included in the ACAP package:
```
poc/camera/app/models/yolov5n_int8.tflite
```

The build system will automatically include it in the .eap file during compilation.

### Verification

To verify the model works correctly:
1. Check model loads without errors in logs
2. Verify inference runs in < 100ms
3. Confirm detections appear in MQTT messages
4. Validate confidence scores are reasonable (> 0.1 for visible objects)
