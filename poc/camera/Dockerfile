ARG ARCH=aarch64
ARG SDK_VERSION=12.2.0
ARG UBUNTU_VERSION=24.04
FROM axisecp/acap-native-sdk:${SDK_VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION} AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    git \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/build

# Create a CMake toolchain file for cross-compilation
RUN echo 'set(CMAKE_SYSTEM_NAME Linux)' > /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_SYSTEM_PROCESSOR aarch64)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_SYSROOT /opt/axis/acapsdk/sysroots/aarch64)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_FIND_ROOT_PATH /opt/axis/acapsdk/sysroots/aarch64)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)' >> /opt/build/toolchain.cmake && \
    echo 'set(CMAKE_C_FLAGS "-mcpu=cortex-a53+crc+crypto -fstack-protector-strong")' >> /opt/build/toolchain.cmake

# Build Paho MQTT C library for aarch64 (SHARED library for dynamic loading)
RUN git clone --depth 1 --branch v1.3.13 https://github.com/eclipse/paho.mqtt.c.git && \
    cd paho.mqtt.c && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=/opt/build/toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/opt/axis/acapsdk/sysroots/aarch64/usr \
        -DPAHO_WITH_SSL=OFF \
        -DPAHO_BUILD_STATIC=OFF \
        -DPAHO_BUILD_SHARED=ON \
        -DPAHO_ENABLE_TESTING=OFF && \
    make -j$(nproc) && \
    make install

# Build libjpeg-turbo for aarch64
RUN git clone --depth 1 --branch 3.0.1 https://github.com/libjpeg-turbo/libjpeg-turbo.git && \
    cd libjpeg-turbo && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=/opt/build/toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/opt/axis/acapsdk/sysroots/aarch64/usr \
        -DENABLE_SHARED=ON \
        -DENABLE_STATIC=OFF && \
    make -j$(nproc) && \
    make install

WORKDIR /opt/app
COPY app/ ./

# Download YOLOv5n models from Axis Model Zoo for multi-chip support
# Source: https://github.com/AxisCommunications/axis-model-zoo
RUN mkdir -p models && \
    curl -L -o models/yolov5n_artpec8_coco_640.tflite \
        "https://acap-ml-models.s3.amazonaws.com/yolov5/yolov5n_artpec8_coco_640.tflite" && \
    echo "Downloaded YOLOv5n ARTPEC-8 model (640x640 INT8)" && \
    curl -L -o models/yolov5n_artpec9_coco_640.tflite \
        "https://acap-ml-models.s3.amazonaws.com/yolov5/yolov5n_artpec9_coco_640.tflite" && \
    echo "Downloaded YOLOv5n ARTPEC-9 model (640x640 INT8)"

# Copy shared libraries that need to be bundled with ACAP
RUN mkdir -p lib && \
    cp /opt/axis/acapsdk/sysroots/aarch64/usr/lib/libpaho-mqtt3a.so* lib/ && \
    cp /opt/axis/acapsdk/sysroots/aarch64/usr/lib/libjpeg.so* lib/ 2>/dev/null || true && \
    cp /opt/axis/acapsdk/sysroots/aarch64/usr/lib/libturbojpeg.so* lib/ 2>/dev/null || true

# Build the ACAP with all settings files, model, and libraries
RUN . /opt/axis/acapsdk/environment-setup* && \
    acap-build . \
        -a settings/settings.json \
        -a settings/mqtt.json \
        -a settings/core.json \
        -a settings/detection.json \
        -a settings/frame_publisher.json \
        -a models/yolov5n_artpec8_coco_640.tflite \
        -a models/yolov5n_artpec9_coco_640.tflite \
        -a lib/ \
        -a html/

# Extract the built .eap file
FROM scratch
COPY --from=builder /opt/app/*.eap /
