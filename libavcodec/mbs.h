//
// Created by danj on 4.3.2021.
//

#ifndef FFMPEG_MBS_H
#define FFMPEG_MBS_H

int mbs_init(void);
int mbs_start_poc(int poc);
int mbs_add_block_info(int x, int y, int size, int bits, int qp);
int mbs_finish_poc(void);

#endif //FFMPEG_MBS_H
