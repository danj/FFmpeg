//
// Created by danj on 4.3.2021.
//

#ifndef FFMPEG_MBS_H
#define FFMPEG_MBS_H

int mbs_init(void);
int mbs_start_poc(int poc, int width, int height);
int mbs_add_block_info(int x, int y, int size, int bits, int qp);
int mbs_add_ctb_info(int idx, int x, int y, int size, int bits, int qp);
int mbs_add_cu_info(int x, int y, int size, int bits, int qp);

int mbs_add_cu_info_upsample(int x, int y, int size, int bits, int qp);
int mbs_add_cu_info_downsample(int x, int y, int size, int bits, int qp);

int mbs_finish_poc(void);

#endif //FFMPEG_MBS_H
