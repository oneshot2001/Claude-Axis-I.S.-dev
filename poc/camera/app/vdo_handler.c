/**
 * vdo_handler.c
 *
 * VDO stream implementation for Axis I.S. POC
 */

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include "vdo_handler.h"
#include "vdo-error.h"

/* Undefine system LOG macros */
#ifdef LOG_ERR
#undef LOG_ERR
#endif

#define LOG(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERR(fmt, args...) { syslog(3, fmt, ## args); fprintf(stderr, fmt, ## args); }

VdoContext* Vdo_Init(unsigned int width, unsigned int height, unsigned int fps) {
    VdoContext* ctx = (VdoContext*)calloc(1, sizeof(VdoContext));
    if (!ctx) {
        LOG_ERR("Failed to allocate VDO context\n");
        return NULL;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->fps = fps;

    GError* error = NULL;

    // Create VDO stream settings
    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "width", width);
    vdo_map_set_uint32(settings, "height", height);
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);
    vdo_map_set_uint32(settings, "framerate", fps);
    vdo_map_set_string(settings, "channel", "1");  // Primary channel

    // Create VDO stream
    ctx->stream = vdo_stream_new(settings, NULL, &error);
    g_object_unref(settings);

    if (error) {
        LOG_ERR("VDO stream creation failed: %s\n", error->message);
        g_error_free(error);
        free(ctx);
        return NULL;
    }

    if (!ctx->stream) {
        LOG_ERR("VDO stream is NULL\n");
        free(ctx);
        return NULL;
    }

    // Start the stream
    if (!vdo_stream_start(ctx->stream, &error)) {
        LOG_ERR("VDO stream start failed: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_object_unref(ctx->stream);
        free(ctx);
        return NULL;
    }

    LOG("VDO stream initialized: %ux%u @ %u FPS\n", width, height, fps);
    return ctx;
}

VdoBuffer* Vdo_Get_Frame(VdoContext* ctx) {
    if (!ctx || !ctx->stream) {
        LOG_ERR("Invalid VDO context\n");
        return NULL;
    }

    GError* error = NULL;

    // Get buffer from stream
    VdoBuffer* buffer = vdo_stream_get_buffer(ctx->stream, &error);

    if (!buffer) {
        if (error) {
            if (!vdo_error_is_expected(&error)) {
                LOG_ERR("Unexpected VDO error: %s\n", error->message);
            }
            g_error_free(error);
        } else {
            LOG_ERR("VDO get buffer failed without error\n");
        }
        ctx->frames_dropped++;
        return NULL;
    }

    ctx->frames_captured++;
    return buffer;
}

void Vdo_Release_Frame(VdoContext* ctx, VdoBuffer* buffer) {
    if (!ctx || !ctx->stream || !buffer) {
        LOG_ERR("Invalid parameters to Vdo_Release_Frame\n");
        return;
    }

    GError* error = NULL;
    if (!vdo_stream_buffer_unref(ctx->stream, &buffer, &error)) {
        if (error) {
            if (!vdo_error_is_expected(&error)) {
                LOG_ERR("Unexpected error releasing VDO buffer: %s\n", error->message);
            }
            g_error_free(error);
        }
    }
}

void Vdo_Cleanup(VdoContext* ctx) {
    if (!ctx) return;

    LOG("VDO cleanup: Captured=%u Dropped=%u\n",
        ctx->frames_captured, ctx->frames_dropped);

    if (ctx->stream) {
        vdo_stream_stop(ctx->stream);
        g_object_unref(ctx->stream);
    }

    free(ctx);
}
