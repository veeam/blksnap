/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_CTRL_H
#define __BLK_SNAP_CTRL_H

int get_blk_snap_major(void);

int ctrl_init(void);
void ctrl_done(void);
#endif /* __BLK_SNAP_CTRL_H */
