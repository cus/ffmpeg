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

#include "config.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include <SDL.h>
#include "ffplay.h"

static void duplicate_right_border_pixels(SDL_Overlay *bmp) {
    int i, width, height;
    Uint8 *p, *maxp;
    for (i = 0; i < 3; i++) {
        width  = bmp->w;
        height = bmp->h;
        if (i > 0) {
            width  >>= 1;
            height >>= 1;
        }
        if (bmp->pitches[i] > width) {
            maxp = bmp->pixels[i] + bmp->pitches[i] * height - 1;
            for (p = bmp->pixels[i] + width - 1; p < maxp; p += bmp->pitches[i])
                *(p+1) = *p;
        }
    }
}

static FFplayVideoOutputTexture *alloc_texture(int width, int height) {
    FFplayVideoOutputTexture *tex = av_malloc(sizeof(FFplayVideoOutputTexture));

    if (tex) {
        SDL_Overlay *bmp;
        int64_t bufferdiff;
        tex->priv = bmp = SDL_CreateYUVOverlay(width, height, SDL_YV12_OVERLAY, SDL_GetVideoSurface());
        bufferdiff = bmp ? FFMAX(bmp->pixels[0], bmp->pixels[1]) - FFMIN(bmp->pixels[0], bmp->pixels[1]) : 0;
        if (!bmp || bmp->pitches[0] < width || bufferdiff < (int64_t)height * bmp->pitches[0]) {
            /* SDL allocates a buffer smaller than requested if the video
             * overlay hardware is unable to support the requested size. */
            if (bmp)
                SDL_FreeYUVOverlay(bmp);
            av_freep(&tex);
        }
    }
    return tex;
}

static void free_texture(FFplayVideoOutputTexture **texp) {
    SDL_Overlay *bmp;
    if (*texp) {
        bmp = (*texp)->priv;
        SDL_FreeYUVOverlay(bmp);
    }
    av_freep(texp);
}

static void display_texture(FFplayVideoOutputTexture *tex, SDL_Rect *rect) {
    SDL_Overlay *bmp = tex->priv;
    SDL_DisplayYUVOverlay(bmp, rect);
}

static void fill_texture(FFplayVideoOutputTexture *tex, AVFrame *src_frame, struct SwsContext* img_convert_ctx) {
    AVPicture pict = { { 0 } };
    SDL_Overlay *bmp = tex->priv;

    SDL_LockYUVOverlay (bmp);

    pict.data[0] = bmp->pixels[0];
    pict.data[1] = bmp->pixels[2];
    pict.data[2] = bmp->pixels[1];

    pict.linesize[0] = bmp->pitches[0];
    pict.linesize[1] = bmp->pitches[2];
    pict.linesize[2] = bmp->pitches[1];

    if (img_convert_ctx) {
        sws_scale(img_convert_ctx, src_frame->data, src_frame->linesize,
                  0, bmp->h, pict.data, pict.linesize);
    } else {
        // FIXME use direct rendering
        av_picture_copy(&pict, (AVPicture *)src_frame,
                        src_frame->format, bmp->w, bmp->h);
    }
    /* workaround SDL PITCH_WORKAROUND */
    duplicate_right_border_pixels(bmp);
    /* update the bitmap content */
    SDL_UnlockYUVOverlay(bmp);
}

static void blend_texture(FFplayVideoOutputTexture *tex, AVSubtitle *sub) {
    AVPicture pict = { { 0 } };
    SDL_Overlay *bmp = tex->priv;
    int i;

    SDL_LockYUVOverlay (bmp);

    pict.data[0] = bmp->pixels[0];
    pict.data[1] = bmp->pixels[2];
    pict.data[2] = bmp->pixels[1];

    pict.linesize[0] = bmp->pitches[0];
    pict.linesize[1] = bmp->pitches[2];
    pict.linesize[2] = bmp->pitches[1];

    for (i = 0; i < sub->num_rects; i++)
        blend_subrect(&pict, sub->rects[i], bmp->w, bmp->h);

    SDL_UnlockYUVOverlay (bmp);
}

FFplayVideoOutput ffplay_video_output_xv = {
    .sdl_flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL,
    .alloc_texture = alloc_texture,
    .free_texture = free_texture,
    .fill_texture = fill_texture,
    .display_texture = display_texture,
    .blend_texture = blend_texture
};
