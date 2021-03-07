//
// Created by danj on 4.3.2021.
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "libavcodec/mbs.h"

typedef struct MBSInfo {
    int bits;
    int qp;
} MBSInfo_t;

#define MAX_X 1024
#define MAX_Y 1024

typedef struct POCMBSInfo {
    int picture;
    int poc;
    int max_x;
    int max_y;
    MBSInfo_t mbs_data[MAX_X][MAX_Y];
} POCMBSInfo_t;

static FILE* _mbs_file = 0;
static int _picture_counter = 0;
static int _skip_poc = 0;
static POCMBSInfo_t _mbs_info;

int mbs_init(void) {
    const char *filename = getenv("MBS_FILENAME");
    if (filename) {
        _mbs_file = fopen(filename, "w"); assert(_mbs_file);
    }

    memset(&_mbs_info, -1, sizeof(POCMBSInfo_t));
    _skip_poc = 0;

    return 0;
}

int mbs_start_poc(int poc) {
    if (!_mbs_file) {
        mbs_init();
    }

    if (poc != _mbs_info.poc) {
        memset(&_mbs_info, 0, sizeof(POCMBSInfo_t));
        _mbs_info.poc = poc;
        _mbs_info.picture = _picture_counter++;
        _skip_poc = 0;
    } else {
        _skip_poc = 1;
    }
    return 0;
}

int mbs_add_block_info(int x, int y, int size, int bits, int qp) {
    assert(x < MAX_X);
    assert(y < MAX_Y);
    if (x > _mbs_info.max_x) _mbs_info.max_x = x;
    if (y > _mbs_info.max_y) _mbs_info.max_y = y;

    _mbs_info.mbs_data[y][x].bits = bits;
    _mbs_info.mbs_data[y][x].qp = qp;

    return 0;
}

int mbs_finish_poc(void) {
    int r = 0;
    int c = 0;

    if (_mbs_file && !_skip_poc) {
        fprintf(_mbs_file, "Picture %d, POC %d\n", _mbs_info.picture, _mbs_info.poc);

        for (r = 0; r <= _mbs_info.max_y; ++r) {
            for (c = 0; c <= _mbs_info.max_x; ++c) {
                fprintf(_mbs_file, "%d\t\%d\t%d\n",
                        r * (_mbs_info.max_x + 1) + c,
                        _mbs_info.mbs_data[r][c].bits, _mbs_info.mbs_data[r][c].qp);
            }
        }
    }
    return 0;
}
