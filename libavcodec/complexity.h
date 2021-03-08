//
// Created by danj on 4.3.2021.
//

#ifndef FFMPEG_MBS_H
#define FFMPEG_MBS_H

int complexity_init(void);
int complexity_start_picture(int poc, int width, int height);
int complexity_add_block_info(int x, int y, int size, int bits, int qp);
int complexity_add_ctb_info(int idx, int x, int y, int size, int bits, int qp);
int complexity_add_cu_info(int x, int y, int size, int bits, int qp);

int complexity_add_cu_info_upsample(int x, int y, int size, int bits, int qp);
int complexity_add_cu_info_downsample(int x, int y, int size, int bits, int qp);

int complexity_finish_poc(void);

#endif //FFMPEG_MBS_H
