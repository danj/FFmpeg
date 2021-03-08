//
// Created by danj on 4.3.2021.
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "libavcodec/mbs.h"

typedef struct MBSInfo {
    int bits;           // Bits assigned to this block
    double qp;          // qp assigned to this block
    int cu_bits;        // Bits allocated to the CU this block belongs to (or is made up from)
    double cu_count;    // Number of CU contributing to this block
    int ctu_bits;       // Bits allocated to the CTU this block belongs to (reported by decoder)
    int ctu_index;      // Index of ctu this block belongs to (reported by decoder)
} MBSInfo_t;

#define MAX_X 1024
#define MAX_Y 1024
#define BLOCK_SIZE 16

typedef struct POCMBSInfo {
    int picture;
    int poc;
    int max_x;
    int max_y;
    int width;
    int height;
    MBSInfo_t mbs_data[MAX_X][MAX_Y];
} POCMBSInfo_t;

static FILE* _mbs_file = 0;
static FILE* _mbs_ctu_file = 0;
static int _picture_counter = 0;
static int _skip_poc = 0;
static POCMBSInfo_t _mbs_info;

int mbs_init(void) {
    const char *mbs_filename = getenv("MBS_FILENAME");
    if (mbs_filename) {
        _mbs_file = fopen(mbs_filename, "w");
        assert(_mbs_file);
    }

    const char *mbs_ctu_filename = getenv("MBS_CTU_FILENAME");
    if (mbs_ctu_filename) {
        _mbs_ctu_file = fopen(mbs_ctu_filename, "w");
        assert(_mbs_ctu_file);
    }

    memset(&_mbs_info, -1, sizeof(POCMBSInfo_t));
    _skip_poc = 0;

    return 0;
}

int mbs_start_poc(int poc, int width, int height) {
    assert(width < BLOCK_SIZE * MAX_X);
    assert(height < BLOCK_SIZE * MAX_Y);

    if (!_mbs_file) {
        mbs_init();
    }

    if (poc != _mbs_info.poc) {
        memset(&_mbs_info, 0, sizeof(POCMBSInfo_t));
        _mbs_info.poc = poc;
        _mbs_info.picture = _picture_counter++;
        _mbs_info.width = width;
        _mbs_info.height = height;
        _skip_poc = 0;
    } else {
        _skip_poc = 1;
    }
    return 0;
}

int mbs_add_block_info(int x, int y, int size, int bits, int qp) {
    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size == BLOCK_SIZE);

    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    assert(x < MAX_X);
    assert(y < MAX_Y);
    if (x > _mbs_info.max_x) _mbs_info.max_x = x;
    if (y > _mbs_info.max_y) _mbs_info.max_y = y;

    _mbs_info.mbs_data[y][x].bits = bits;
    _mbs_info.mbs_data[y][x].qp = qp;
    _mbs_info.mbs_data[y][x].cu_bits = bits;
    _mbs_info.mbs_data[y][x].cu_count = 1;

    return 0;
}

int mbs_add_ctb_info(int idx, int x, int y, int size, int bits, int qp) {
    if (_skip_poc) {
        return 0;
    }

    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size % BLOCK_SIZE == 0 && size > BLOCK_SIZE);

    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    int blocks = 0;
    int remaining_bits = bits;

    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _mbs_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _mbs_info.width; ++c) {
            blocks += 1;
            int x2 = x+c;
            int y2 = y+r;
            assert(x2 < MAX_X);
            assert(y2 < MAX_Y);
            _mbs_info.mbs_data[y2][x2].ctu_bits = bits;
            _mbs_info.mbs_data[y2][x2].ctu_index = idx;
            remaining_bits -= _mbs_info.mbs_data[y2][x2].bits;
        }
    }
    assert(remaining_bits >= 0);

    int extra_bits = remaining_bits / blocks;
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _mbs_info.height && remaining_bits > 0 && extra_bits > 0; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _mbs_info.width && remaining_bits > 0 && extra_bits > 0; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _mbs_info.mbs_data[y2][x2].bits += extra_bits;
            remaining_bits -= extra_bits;
        }
    }
    assert(remaining_bits >= 0);

    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _mbs_info.height && remaining_bits > 0; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _mbs_info.width && remaining_bits > 0; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _mbs_info.mbs_data[y2][x2].bits += 1;
            remaining_bits -= 1;
        }
    }
    assert(remaining_bits == 0);

    return 0;
}

int mbs_add_cu_info_downsample(int x, int y, int size, int bits, int qp) {
    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    if (x > _mbs_info.max_x) _mbs_info.max_x = x;
    if (y > _mbs_info.max_y) _mbs_info.max_y = y;

    _mbs_info.mbs_data[y][x].bits += bits;
    _mbs_info.mbs_data[y][x].cu_bits += bits;

    double prev_qp = _mbs_info.mbs_data[y][x].qp * _mbs_info.mbs_data[y][x].cu_count;
    _mbs_info.mbs_data[y][x].cu_count += 1;
    _mbs_info.mbs_data[y][x].qp = (prev_qp + qp) / _mbs_info.mbs_data[y][x].cu_count;

    return 0;
}

int mbs_add_cu_info_upsample(int x, int y, int size, int bits, int qp) {
    assert(x % BLOCK_SIZE == 0);
    assert(y % BLOCK_SIZE == 0);
    assert(size % BLOCK_SIZE == 0 && size > BLOCK_SIZE);

    x /= BLOCK_SIZE;
    y /= BLOCK_SIZE;

    int blocks = 0;
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _mbs_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _mbs_info.width; ++c) {
            blocks += 1;
            int x2 = x+c;
            int y2 = y+r;
            assert(x2 < MAX_X);
            assert(y2 < MAX_Y);
            if (x2 > _mbs_info.max_x) _mbs_info.max_x = x2;
            if (y2 > _mbs_info.max_y) _mbs_info.max_y = y2;

            _mbs_info.mbs_data[y2][x2].bits = bits;
            _mbs_info.mbs_data[y2][x2].qp = qp;
            _mbs_info.mbs_data[y2][x2].cu_bits = bits;
        }
    }

    int extra_bits = bits % blocks;
    for (int r=0; r<(size/BLOCK_SIZE) && (y+r)*BLOCK_SIZE < _mbs_info.height; ++r) {
        for (int c=0; c<(size/BLOCK_SIZE) && (x+c)*BLOCK_SIZE < _mbs_info.width; ++c) {
            int x2 = x+c;
            int y2 = y+r;
            _mbs_info.mbs_data[y2][x2].bits /= blocks;
            _mbs_info.mbs_data[y2][x2].cu_count = 1.0 / blocks;
            if (extra_bits > 0) {
                _mbs_info.mbs_data[y2][x2].bits += 1;
                extra_bits -= 1;
            }
        }
    }

    return 0;
}

int mbs_add_cu_info(int x, int y, int size, int bits, int qp) {
    if (size < BLOCK_SIZE) {
        return mbs_add_cu_info_downsample(x, y, size, bits, qp);
    }

    if (size > BLOCK_SIZE) {
        return mbs_add_cu_info_upsample(x, y, size, bits, qp);
    }

    return mbs_add_block_info(x, y, size, bits, qp);
}

int mbs_finish_poc(void) {
    int r = 0;
    int c = 0;

    if (_mbs_file && !_skip_poc) {
        fprintf(_mbs_file, "Picture %d, POC %d\n", _mbs_info.picture, _mbs_info.poc);

        for (r = 0; r <= _mbs_info.max_y; ++r) {
            for (c = 0; c <= _mbs_info.max_x; ++c) {
                if (getenv("MBS_DUMP_XY")) {
                    fprintf(_mbs_ctu_file, "%d,%d\t", c, r);
                }
                fprintf(_mbs_file, "%d\t\%d\t%d\n",
                        r * (_mbs_info.max_x + 1) + c,
                        _mbs_info.mbs_data[r][c].bits,
                        (int)_mbs_info.mbs_data[r][c].qp);
            }
        }
    }

    if (_mbs_ctu_file && !_skip_poc) {
        fprintf(_mbs_ctu_file, "Picture %d, POC %d\n", _mbs_info.picture, _mbs_info.poc);

        int blocks_per_ctu_row = 64/BLOCK_SIZE;
        int blocks_per_ctu_col = 64/BLOCK_SIZE;

        for (r = 0; r <= _mbs_info.max_y; r+=blocks_per_ctu_row) {
            for (c = 0; c <= _mbs_info.max_x; c+=blocks_per_ctu_col) {
                if (getenv("MBS_DUMP_XY")) {
                    fprintf(_mbs_ctu_file, "%d,%d\t", c, r);
                }
                fprintf(_mbs_ctu_file, "%d\t\%d\t%d\n",
                    _mbs_info.mbs_data[r][c].ctu_index,
                    _mbs_info.mbs_data[r][c].ctu_bits,
                    (int)_mbs_info.mbs_data[r][c].qp);
            }
        }
    }

    return 0;
}
