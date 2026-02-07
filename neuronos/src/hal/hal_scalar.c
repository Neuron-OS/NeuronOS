/**
 * @file hal_scalar.c
 * @brief NeuronOS HAL — Portable C scalar backend
 *
 * This is NEW CODE that does NOT exist in BitNet.
 * It provides a pure-C implementation of all ternary I2_S kernel
 * operations, serving as:
 *   1. The universal fallback for platforms without SIMD
 *   2. A reference implementation for testing SIMD backends
 *   3. The bootstrap backend for RISC-V, WASM, and any new ISA
 *
 * Uses the same x86/ACT_PARALLEL weight packing layout:
 *   - QK_I2_S = 128 (block size: 128 weights per quantization group)
 *   - Packed 4 weights per byte: bits [7:6]=w0, [5:4]=w1, [3:2]=w2, [1:0]=w3
 *   - Each packed byte has 32-byte alignment within a QK_I2_S block
 *   - Scale stored as float at end of quantized data
 *
 * Ternary encoding: {0, 1, 2} → {-1, 0, +1}
 */

#include "neuronos/neuronos_hal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────── Constants ─────────────────────────── */

#define SCALAR_QK_I2_S 128 /* Quantization block size (matches x86 layout) */

/* ──────────────────────── Weight unpacking ──────────────────────── */

/**
 * Unpack 2-bit ternary value from packed byte.
 *
 * Within a QK_I2_S=128 block, 128 weights are packed into 32 bytes.
 * Layout: 4 groups of 32 weights, each group uses 32 bytes.
 * For weight index j in the block:
 *   group_idx = j / 32   (which 2-bit slice: bits 6-7, 4-5, 2-3, 0-1)
 *   group_pos = j % 32   (byte offset within the 32-byte group)
 *   byte = packed[group_pos]
 *   value = (byte >> (6 - 2*group_idx)) & 0x03
 */
/*
 * Unpack a single 2-bit ternary value from packed storage.
 * Useful for debugging and verification. Production code uses
 * the inline unpacking loop in each kernel for better locality.
 *
 * static inline int8_t unpack_i2(const uint8_t * packed, int j) {
 *     int group_idx = j / 32;
 *     int group_pos = j % 32;
 *     uint8_t byte = packed[group_pos];
 *     uint8_t raw  = (byte >> (6 - 2 * group_idx)) & 0x03;
 *     return (int8_t)raw - 1;  // Map: 0→-1, 1→0, 2→+1
 * }
 */

/* ──────────── vec_dot: dot product of I2_S weights and I8 activations ─── */

/**
 * Computes dot products between packed I2_S weights and int8 activations.
 *
 * This mirrors ggml_vec_dot_i2_i8_s_1x1 but in pure C.
 * The caller provides nrc rows of weights to process.
 *
 * @param n   Number of elements per row
 * @param s   Output array: one float per row
 * @param bs  Stride between output elements (bytes)
 * @param vx  Packed I2_S weights (nrc rows, each row is n/4 bytes + scale)
 * @param bx  Stride between weight rows (bytes)
 * @param vy  Int8 activations (single vector, length n)
 * @param by  Stride between activation rows (bytes)
 * @param nrc Number of rows to compute
 */
static void scalar_vec_dot_i2_i8(int n, float * s, size_t bs, const void * vx, size_t bx, const void * vy, size_t by,
                                 int nrc) {
    const uint8_t * x = (const uint8_t *)vx;
    const int8_t * y = (const int8_t *)vy;

    const int nb = n / SCALAR_QK_I2_S; /* number of blocks per row */

    for (int row = 0; row < nrc; row++) {
        const uint8_t * x_row = x + row * (bx / 4);
        int32_t sum = 0;

        for (int block = 0; block < nb; block++) {
            const uint8_t * packed = x_row + block * 32; /* 32 bytes per block */
            const int8_t * yi = y + block * SCALAR_QK_I2_S;

            /*
             * Unpack each of the 128 weights and multiply with activation.
             * The encoding is {0,1,2} → {-1,0,+1}, so the dot product
             * with maddubs-style unsigned×signed becomes:
             *   result += raw_value * activation
             * Then we subtract 1*activation to map to {-1,0,+1}.
             *
             * But to match the SIMD output exactly (which uses maddubs on
             * unsigned 2-bit values × signed int8), we compute:
             *   sum += raw_u2 * activation_s8
             * This matches the accumulator in the SIMD kernels.
             */
            for (int j = 0; j < SCALAR_QK_I2_S; j++) {
                int group_idx = j / 32;
                int group_pos = j % 32;
                uint8_t byte = packed[group_pos];
                uint8_t raw = (byte >> (6 - 2 * group_idx)) & 0x03;
                sum += (int32_t)raw * (int32_t)yi[j];
            }
        }

        s[row] = (float)sum;
    }
}

/* ──────────── quantize: f32 → I2_S packed ternary ──────────────── */

/**
 * Quantize float weights to I2_S ternary format.
 * Uses the same algorithm as quantize_i2_s() in ggml-bitnet-mad.cpp.
 *
 * For each weight:
 *   if |w| < epsilon → 0 (maps to ternary: 0, encoded as 1)
 *   if sign(w) > 0   → +1 (encoded as 2)
 *   if sign(w) < 0   → -1 (encoded as 0)
 *
 * Layout: x86 ACT_PARALLEL packing
 *   - Groups of QK_I2_S=128 weights
 *   - 4 sub-groups of 32 weights packed into 32 bytes via bit shifting
 */
static size_t scalar_quantize_i2(const float * src, void * dst, int64_t nrow, int64_t n_per_row,
                                 const float * quant_weights) {
    (void)quant_weights; /* Not used in ternary quantization */

    int64_t n = nrow * n_per_row;

    /* Step 1: Find max absolute value for scale */
    double max_val = 0.0;
    for (int64_t i = 0; i < n; i++) {
        double abs_val = fabs((double)src[i]);
        if (abs_val > max_val)
            max_val = abs_val;
    }
    double i2_scale = max_val;

    /* Step 2: Quantize to {0, 1, 2} */
    uint8_t * q8 = (uint8_t *)malloc((size_t)n * sizeof(uint8_t));
    if (!q8)
        return 0;

    for (int64_t i = 0; i < n; i++) {
        if (fabs((double)src[i]) < 1e-6) {
            q8[i] = 1; /* zero weight → encoded as 1 */
        } else {
            q8[i] = ((double)src[i] * i2_scale > 0) ? 2 : 0;
        }
    }

    /* Step 3: Pack into I2_S format */
    uint8_t * out = (uint8_t *)dst;
    memset(out, 0, (size_t)(n / 4));

    int64_t num_blocks = n / SCALAR_QK_I2_S;
    for (int64_t blk = 0; blk < num_blocks; blk++) {
        for (int j = 0; j < SCALAR_QK_I2_S; j++) {
            int group_idx = j / 32;
            int group_pos = j % 32;
            uint8_t val = q8[blk * SCALAR_QK_I2_S + j];
            out[blk * 32 + group_pos] |= (val << (6 - 2 * group_idx));
        }
    }

    /* Step 4: Store scale after packed data */
    float * scale_ptr = (float *)((char *)out + n / 4);
    scale_ptr[0] = (float)i2_scale;

    free(q8);

    /* Return size: matches formula from ggml-bitnet-mad.cpp */
    /* Note: ggml_row_size not available here, approximate */
    return (size_t)(n / 4 + 32);
}

/* ──────────── gemv: matrix-vector multiply ─────────────────────── */

/**
 * Scalar GEMV for ternary weights × int8 activations.
 * Processes nr rows, nc columns (nc unused, inferred from n).
 */
static void scalar_gemv_i2_i8(int n, float * s, size_t bs, const void * vx, const void * vy, int nr, int nc) {
    (void)nc;
    /* For scalar, GEMV is just vec_dot called for each row */
    const uint8_t * x = (const uint8_t *)vx;
    const int8_t * y = (const int8_t *)vy;

    const int nb = n / SCALAR_QK_I2_S;
    const size_t row_bytes = (size_t)(nb * 32); /* packed bytes per row */

    for (int row = 0; row < nr; row++) {
        const uint8_t * x_row = x + row * row_bytes;
        int32_t sum = 0;

        for (int block = 0; block < nb; block++) {
            const uint8_t * packed = x_row + block * 32;
            const int8_t * yi = y + block * SCALAR_QK_I2_S;

            for (int j = 0; j < SCALAR_QK_I2_S; j++) {
                int group_idx = j / 32;
                int group_pos = j % 32;
                uint8_t byte = packed[group_pos];
                uint8_t raw = (byte >> (6 - 2 * group_idx)) & 0x03;
                sum += (int32_t)raw * (int32_t)yi[j];
            }
        }

        /* Write to output with stride */
        *((float *)((char *)s + row * bs)) = (float)sum;
    }
}

/* ──────────── gemm: matrix-matrix multiply ─────────────────────── */

/**
 * Scalar GEMM for ternary weights × int8 activations.
 * This is a naive O(n*nr*nc) implementation — purely for correctness.
 */
static void scalar_gemm_i2_i8(int n, float * s, size_t bs, const void * vx, const void * vy, int nr, int nc) {
    /* For the scalar fallback, GEMM = batched GEMV */
    /* This is intentionally naive; SIMD backends will be fast */
    scalar_gemv_i2_i8(n, s, bs, vx, vy, nr, nc);
}

/* ──────────── Backend descriptor ────────────────────────────────── */

const neuronos_backend_t neuronos_backend_scalar = {
    .name = "scalar",
    .type = NEURONOS_BACKEND_SCALAR,
    .priority = 0,          /* Lowest priority — always the last resort */
    .required_features = 0, /* No SIMD required */
    .config =
        {
            .row_block_size = 1,   /* Process 1 row at a time */
            .col_block_size = 128, /* Match QK_I2_S block size */
            .parallel_size = 1,    /* No parallelism */
            .qk_i2_s = SCALAR_QK_I2_S,
        },
    .vec_dot_i2_i8 = scalar_vec_dot_i2_i8,
    .quantize_i2 = scalar_quantize_i2,
    .gemv_i2_i8 = scalar_gemv_i2_i8,
    .gemm_i2_i8 = scalar_gemm_i2_i8,
    .init = NULL,
    .shutdown = NULL,
};
