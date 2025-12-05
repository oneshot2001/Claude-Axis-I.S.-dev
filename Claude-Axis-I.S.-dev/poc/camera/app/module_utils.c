/**
 * Axis I.S. Module Utility Functions
 *
 * Helper functions for module implementations
 */

#include "module.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>

/**
 * Create metadata frame
 */
MetadataFrame* metadata_create(void) {
    MetadataFrame* meta = (MetadataFrame*)calloc(1, sizeof(MetadataFrame));
    if (!meta) return NULL;

    meta->custom_data = cJSON_CreateObject();
    meta->detection_capacity = 32;  // Initial capacity
    meta->detections = (Detection*)calloc(meta->detection_capacity, sizeof(Detection));

    if (!meta->detections || !meta->custom_data) {
        metadata_free(meta);
        return NULL;
    }

    return meta;
}

/**
 * Free metadata frame
 */
void metadata_free(MetadataFrame* meta) {
    if (!meta) return;

    if (meta->detections) {
        free(meta->detections);
    }

    if (meta->custom_data) {
        cJSON_Delete(meta->custom_data);
    }

    free(meta);
}

/**
 * Add detection to metadata
 */
void metadata_add_detection(MetadataFrame* meta, Detection det) {
    if (!meta) return;

    // Expand array if needed
    if (meta->detection_count >= meta->detection_capacity) {
        int new_capacity = meta->detection_capacity * 2;
        Detection* new_dets = (Detection*)realloc(meta->detections,
                                                   new_capacity * sizeof(Detection));
        if (!new_dets) return;

        meta->detections = new_dets;
        meta->detection_capacity = new_capacity;
    }

    meta->detections[meta->detection_count++] = det;
    meta->object_count = meta->detection_count;
}

/**
 * Get module configuration string value
 */
const char* module_config_get_string(cJSON* config, const char* key, const char* default_val) {
    if (!config) return default_val;

    cJSON* item = cJSON_GetObjectItem(config, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }

    return default_val;
}

/**
 * Get module configuration integer value
 */
int module_config_get_int(cJSON* config, const char* key, int default_val) {
    if (!config) return default_val;

    cJSON* item = cJSON_GetObjectItem(config, key);
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }

    return default_val;
}

/**
 * Get module configuration float value
 */
float module_config_get_float(cJSON* config, const char* key, float default_val) {
    if (!config) return default_val;

    cJSON* item = cJSON_GetObjectItem(config, key);
    if (item && cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }

    return default_val;
}

/**
 * Get module configuration boolean value
 */
bool module_config_get_bool(cJSON* config, const char* key, bool default_val) {
    if (!config) return default_val;

    cJSON* item = cJSON_GetObjectItem(config, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }

    return default_val;
}

/**
 * JPEG encoding error handler
 */
struct jpeg_error_mgr_wrapper {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    struct jpeg_error_mgr_wrapper* err = (struct jpeg_error_mgr_wrapper*)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

/**
 * Encode pixels to JPEG
 */
int encode_jpeg(void* pixels, int width, int height, VdoFormat format,
                char** jpeg_data, size_t* jpeg_size) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr_wrapper jerr;
    unsigned char* jpeg_buffer = NULL;
    unsigned long jpeg_buffer_size = 0;

    // Only support YUV420 format for now
    if (format != VDO_FORMAT_YUV) {
        return -1;
    }

    // Initialize JPEG compression
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_compress(&cinfo);
        if (jpeg_buffer) free(jpeg_buffer);
        return -1;
    }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &jpeg_buffer, &jpeg_buffer_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 85, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    // Write scanlines
    JSAMPROW row_pointer[1];
    unsigned char* yuv_data = (unsigned char*)pixels;
    int row_stride = width * 3;

    // Simplified: assumes pixels are already in RGB/YUV format
    // In production, you'd convert YUV420 to RGB first
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &yuv_data[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    *jpeg_data = (char*)jpeg_buffer;
    *jpeg_size = jpeg_buffer_size;

    return 0;
}

/**
 * Base64 encoding table
 */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Encode data to base64
 */
int encode_base64(const char* input, size_t input_len, char** output) {
    size_t output_len = 4 * ((input_len + 2) / 3);
    *output = (char*)malloc(output_len + 1);
    if (!*output) return -1;

    size_t i = 0, j = 0;
    unsigned char a, b, c;

    while (i < input_len) {
        a = i < input_len ? (unsigned char)input[i++] : 0;
        b = i < input_len ? (unsigned char)input[i++] : 0;
        c = i < input_len ? (unsigned char)input[i++] : 0;

        (*output)[j++] = base64_table[a >> 2];
        (*output)[j++] = base64_table[((a & 0x3) << 4) | (b >> 4)];
        (*output)[j++] = (i > input_len + 1) ? '=' : base64_table[((b & 0xF) << 2) | (c >> 6)];
        (*output)[j++] = (i > input_len) ? '=' : base64_table[c & 0x3F];
    }

    (*output)[j] = '\0';
    return 0;
}

/**
 * HTTP POST JSON helper
 */
int http_post_json(const char* url, const char* api_key, cJSON* request, cJSON** response) {
    // Build request body
    char* request_str = cJSON_PrintUnformatted(request);
    if (!request_str) return -1;

    // Build headers
    char headers[512];
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "Authorization: Bearer %s", api_key);

    // Make HTTP POST
    char* response_str = NULL;
    // Note: This requires core_api_http_post which uses CURL
    // For now, return error - modules will implement their own HTTP logic
    free(request_str);

    if (!response_str) {
        return -1;
    }

    // Parse response
    *response = cJSON_Parse(response_str);
    free(response_str);

    return *response ? 0 : -1;
}
