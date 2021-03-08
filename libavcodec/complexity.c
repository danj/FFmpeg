/*
 * Complexity (aka MBS)
 *
 * The following structures represent the "complexity" (bits and qp per block) reported by ffmpeg
 *
 * A bit of history: *
 * At first, complexity was reported to the Beamr Optimizer per 16x16 macro-block (as coded in AVC)
 * The original file format was text. Later a binary file format was introduced,
 * and complexity for HEVC was experimented with at the CTU level 64x64.
 *
 * Moving to Beamr4x and Beamr5x we decided to keep the 16x16 block-size.
 * When Beamr5x uses larger CUs we up-sample the complexity to match the 16x16 grid.
 *
 * BQM supports reading complexity from text files (binary not supported)
 * This module is part of the patch to ffmpeg 4.3.2 to write complexity to text files
 *
 * NOT THREAD SAFE.
 * SHOULD ONLY WORK IF DECODING IN SINGLE THREADED MODE
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "libavcodec/complexity.h"

typedef struct BlockComplexityInfo {
    int bits;           // Bits assigned to this block (explicitly or calculated)
    double qp;          // qp assigned to this block (explicitly or calculated)
    int cu_bits;        // Bits allocated to the CU this block belongs to (or is made up from, in case of 8x8)
    double cu_count;    // Number of CU contributing to this block (4 for 8x8, 1 for 16x16, 0.25 for 32x32, 0.0625 for 64x64)
    int ctu_bits;       // Bits allocated to the CTU this block belongs to (as reported explicity by decoder)
    int ctu_index;      // Index of ctu this block belongs to (as reported explicity by decoder)
} BlockComplexityInfo_t;

#define BLOCK_SIZE 16   // in pixels
#define MAX_X 1024      // in blocks
#define MAX_Y 1024      // in blocks

typedef struct PictureComplexityInfo {
    int picture_idx;    // index of picture in sequence
    int poc;            // Reported POC
    int max_x;          // in blocks
    int max_y;          // in blocks
    int width;          // in pixels
    int height;         // in pixels
    BlockComplexityInfo_t blocks[MAX_X][MAX_Y];   // complexity data per block
} PictureComplexityInfo_t;

static FILE* _complexity_file = 0;             // output file for complexity per block
static FILE* _complexity_ctu_file = 0;         // output file for complexity per CTU (good for debugging)
static int _picture_index = 0;          // running counter of the current picture

static int _skip_poc = 0;               // skip this picture?
                                        // ffmpeg decodes the first picture twice.
                                        // we skip the 2nd time, as we don't want the extra data in the dump file

static PictureComplexityInfo_t _complexity_info;          // data of the current picture. NOT THREAD SAFE.

// Initialize  the files and memory structures
int complexity_init(void) {
    const char *complexity_filename = getenv("COMPLEXITY_FILENAME");
    if (complexity_filename) {
        _complexity_file = fopen(complexity_filename, "w");
        assert(_complexity_file);
    }

    const char *complexity_ctu_filename = getenv("COMPLEXITY_CTU_FILENAME");
    if (complexity_ctu_filename) {
        _complexity_ctu_file = fopen(complexity_ctu_filename, "w");
        assert(_complexity_ctu_file);
    }

    memset(&_complexity_info, -1, sizeof(PictureComplexityInfo_t));
    _skip_poc = 0;

    return 0;
}

// Called at the beginning of each picture (frame)
// The picture width and height are recorded for later usage when up-sampling
// Since ffmpeg decodes the first frame twice we skip the 2nd time (poc has not changed)
int complexity_start_picture(int poc, int width, int height) {
    assert(width < BLOCK_SIZE * MAX_X);
    assert(height < BLOCK_SIZE * MAX_Y);

    if (!_complexity_file) {
        complexity_init();
    }

    if (poc != _complexity_info.poc) {
        memset(&_complexity_info, 0, sizeof(PictureComplexityInfo_t));
        _complexity_info.poc = poc;
        _complexity_info.picture_idx = _picture_index++;
        _complexity_info.width = width;
        _complexity_info.height = height;
        _skip_poc = 0;
    } else {
        _skip_poc = 1;
    }
    return 0;
}

// Called for blocks of exactly BLOCK_SIZE
// The bits and QP are recorded as-is
int complexity_add_block_info(int x, int y, int size, int bits, int qp) {
    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size == BLOCK_SIZE);

    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    assert(x < MAX_X);
    assert(y < MAX_Y);
    if (x > _complexity_info.max_x) _complexity_info.max_x = x;
    if (y > _complexity_info.max_y) _complexity_info.max_y = y;

    _complexity_info.blocks[y][x].bits = bits;
    _complexity_info.blocks[y][x].qp = qp;
    _complexity_info.blocks[y][x].cu_bits = bits;
    _complexity_info.blocks[y][x].cu_count = 1;              // exactly 1 CU contributing bits to this block

    return 0;
}

// Called per CTB (64x64)
// This is called AFTER the CUs have already been processed
// The CTB uses more bits than the sum of bits in the CUs due to the header
// These extra bits are spread evenly between the blocks
int complexity_add_ctb_info(int idx, int x, int y, int size, int bits, int qp) {
    if (_skip_poc) {
        return 0;
    }

    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size % BLOCK_SIZE == 0 && size > BLOCK_SIZE);

    x /= BLOCK_SIZE;    // from pixel to block coordinates
    y /= BLOCK_SIZE;    // from pixel to block coordinates

    int blocks = 0;                 // blocks in the ctb
    int remaining_bits = bits;      // bits not counted for

    // iterate over smaller blocks
    // special attention to the edge cases where we might have less blocks
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _complexity_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _complexity_info.width; ++c) {
            blocks += 1;
            int x2 = x+c;
            int y2 = y+r;
            assert(x2 < MAX_X);
            assert(y2 < MAX_Y);
            _complexity_info.blocks[y2][x2].ctu_bits = bits;
            _complexity_info.blocks[y2][x2].ctu_index = idx;
            remaining_bits -= _complexity_info.blocks[y2][x2].bits;
        }
    }
    assert(remaining_bits >= 0);

    int extra_bits = remaining_bits / blocks;   // extra bits to add to each block
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _complexity_info.height && remaining_bits > 0 && extra_bits > 0; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _complexity_info.width && remaining_bits > 0 && extra_bits > 0; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _complexity_info.blocks[y2][x2].bits += extra_bits;
            remaining_bits -= extra_bits;
        }
    }
    assert(remaining_bits >= 0);

    // and possibly one more pass to add even a bit more blocks
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _complexity_info.height && remaining_bits > 0; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _complexity_info.width && remaining_bits > 0; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _complexity_info.blocks[y2][x2].bits += 1;
            remaining_bits -= 1;
        }
    }
    assert(remaining_bits == 0); // all CTB bits accounted for

    return 0;
}

// Called for CUs which are 8x8
// Since the block size is 16x16 we need to down-sample the bits from 4 CUs to a single block
int complexity_add_cu_info_downsample(int x, int y, int size, int bits, int qp) {
    x /= BLOCK_SIZE;    // in blocks (4 8x8 CUs per block)
    y /= BLOCK_SIZE;

    if (x > _complexity_info.max_x) _complexity_info.max_x = x;
    if (y > _complexity_info.max_y) _complexity_info.max_y = y;

    _complexity_info.blocks[y][x].bits += bits;      // sum the CU bits in the block
    _complexity_info.blocks[y][x].cu_bits += bits;

    // update the QP to the average QP of the smaller CUs
    double prev_qp = _complexity_info.blocks[y][x].qp * _complexity_info.blocks[y][x].cu_count;
    _complexity_info.blocks[y][x].cu_count += 1;
    _complexity_info.blocks[y][x].qp = (prev_qp + qp) / _complexity_info.blocks[y][x].cu_count;

    return 0;
}

// Called for CUs which are large than 16x16
// The CU bits are spread evenly between the blocks
// The QP is assigned as-in to each block
int complexity_add_cu_info_upsample(int x, int y, int size, int bits, int qp) {
    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size % BLOCK_SIZE == 0 && size > BLOCK_SIZE);

    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    // First, assign bits per block, and count the number of blocks)
    int blocks = 0;
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _complexity_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _complexity_info.width; ++c) {
            blocks += 1;
            int x2 = x+c;
            int y2 = y+r;
            assert(x2 < MAX_X);
            assert(y2 < MAX_Y);
            if (x2 > _complexity_info.max_x) _complexity_info.max_x = x2;
            if (y2 > _complexity_info.max_y) _complexity_info.max_y = y2;

            _complexity_info.blocks[y2][x2].bits = bits;
            _complexity_info.blocks[y2][x2].qp = qp;
            _complexity_info.blocks[y2][x2].cu_bits = bits;
        }
    }

    // Next, normalize the bits per block by the number of blocks in the CU
    // Add extra bit per block if we have some extra bits that need to be accounted for
    int extra_bits = bits % blocks;
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _complexity_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _complexity_info.width; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _complexity_info.blocks[y2][x2].bits /= blocks;
            _complexity_info.blocks[y2][x2].cu_count = 1.0 / blocks;
            if (extra_bits > 0) {
                _complexity_info.blocks[y2][x2].bits += 1;
                extra_bits -= 1;
            }
        }
    }

    return 0;
}

// Called per CU
// Depending on CU size dispatch the appropriate method
int complexity_add_cu_info(int x, int y, int size, int bits, int qp) {
    if (size < BLOCK_SIZE) {
        return complexity_add_cu_info_downsample(x, y, size, bits, qp);
    }

    if (size > BLOCK_SIZE) {
        return complexity_add_cu_info_upsample(x, y, size, bits, qp);
    }

    return complexity_add_block_info(x, y, size, bits, qp);
}

// Called at the end of each frame
// This method writes the complexity info out to the files
// File format:
// Picture %, POC %d
// block_index bits qp
// block_index bits qp
// ...
int complexity_finish_poc(void) {
    int r = 0;
    int c = 0;

    // Full list of complexity blocks per picture
    if (_complexity_file && !_skip_poc) {
        fprintf(_complexity_file, "Picture %d, POC %d\n", _complexity_info.picture_idx, _complexity_info.poc);

        for (r = 0; r <= _complexity_info.max_y; ++r) {
            for (c = 0; c <= _complexity_info.max_x; ++c) {
                if (getenv("complexity_DUMP_XY")) {
                    fprintf(_complexity_ctu_file, "%d,%d\t", c, r);
                }
                fprintf(_complexity_file, "%d\t\%d\t%d\n",
                        r * (_complexity_info.max_x + 1) + c,
                        _complexity_info.blocks[r][c].bits,
                        (int)_complexity_info.blocks[r][c].qp);
            }
        }
    }

    // Short list of complexity blocks per CTB (64x64)
    if (_complexity_ctu_file && !_skip_poc) {
        fprintf(_complexity_ctu_file, "Picture %d, POC %d\n", _complexity_info.picture_idx, _complexity_info.poc);

        int blocks_per_ctu_row = 64/BLOCK_SIZE;
        int blocks_per_ctu_col = 64/BLOCK_SIZE;

        for (r = 0; r <= _complexity_info.max_y; r+=blocks_per_ctu_row) {
            for (c = 0; c <= _complexity_info.max_x; c+=blocks_per_ctu_col) {
                if (getenv("complexity_DUMP_XY")) {
                    fprintf(_complexity_ctu_file, "%d,%d\t", c, r);
                }
                fprintf(_complexity_ctu_file, "%d\t\%d\t%d\n",
                    _complexity_info.blocks[r][c].ctu_index,
                    _complexity_info.blocks[r][c].ctu_bits,
                    (int)_complexity_info.blocks[r][c].qp);
            }
        }
    }

    return 0;
}
