ARG ARCH=aarch64
ARG SDK_VERSION=12.2.0
FROM axisecp/acap-native-sdk:${SDK_VERSION}-${ARCH} AS builder

WORKDIR /opt/app
COPY app/ ./

# Build the ACAP
RUN . /opt/axis/acapsdk/environment-setup* && \
    acap-build . \
        -a settings/settings.json \
        -a settings/mqtt.json

# Extract the built .eap file
FROM scratch
COPY --from=builder /opt/app/*.eap /
