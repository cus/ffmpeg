/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FFPLAY_H
#define FFPLAY_H

typedef struct FFplayVideoOutputTexture {
    void *priv;
} FFplayVideoOutputTexture;

typedef struct FFplayVideoOutput {
    Uint32 sdl_flags;
    FFplayVideoOutputTexture *(*alloc_texture) (int width, int height);
    void (*free_texture) (FFplayVideoOutputTexture **texture);
    void (*display_texture) (FFplayVideoOutputTexture *texture, SDL_Rect *rect);
    void (*fill_texture) (FFplayVideoOutputTexture *texture, AVFrame *src_frame, struct SwsContext* img_convert_ctx);
    void (*blend_texture) (FFplayVideoOutputTexture *texture, AVSubtitle *sub);
} FFplayVideoOutput;

void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh);

extern FFplayVideoOutput ffplay_video_output_xv;

#endif /* FFPLAY_H */

