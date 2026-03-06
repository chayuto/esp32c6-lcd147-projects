#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Encodes the current canvas to JPEG (86x160, quality 35) and base64-encodes it.
// On success: allocates *b64_out (caller must free()), sets *b64_len_out, returns true.
// On failure: returns false, *b64_out is NULL.
bool snapshot_encode(unsigned char **b64_out, size_t *b64_len_out);

// Returns raw JPEG bytes (86x160) into caller-supplied buffer.
// out_buf must be at least 16384 bytes. Sets *jpeg_len on success.
bool snapshot_encode_raw(uint8_t *out_buf, size_t out_cap, int *jpeg_len);
