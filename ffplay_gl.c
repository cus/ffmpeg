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
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include "ffplay.h"

typedef struct GLOverlay {
    GLuint textures[3];
    AVFrame frame;
    int w;
    int h;
    int texture_ready;
    int texture_initialized;
} GLOverlay;

static PFNGLGENPROGRAMSARBPROC glGenProgramsARB;
static PFNGLBINDPROGRAMARBPROC glBindProgramARB;
static PFNGLPROGRAMSTRINGARBPROC glProgramStringARB;
static PFNGLDELETEPROGRAMSARBPROC glDeleteProgramsARB;
static PFNGLPROGRAMLOCALPARAMETER4FVARBPROC glProgramLocalParameter4fvARB;
static GLint max_texture_size;
static GLint max_texture_units;
static GLuint fragment_program;
static int initialized = 0;

static const float matrix_bt601_tv2full[4][4] = {
    {  1.164383561643836,  1.164383561643836,  1.164383561643836, 0.0000 },
    {  0.0000,            -0.391762290094914,  2.017232142857142, 0.0000 },
    {  1.596026785714286, -0.812967647237771,  0.0000,            0.0000 },
    { -0.874202217873451,  0.531667823499146, -1.085630789302022, 0.0000 },
};
static const float matrix_bt709_tv2full[4][4] = {
    {  1.164383561643836,  1.164383561643836,  1.164383561643836, 0.0000 },
    {  0.0000,            -0.213248614273730,  2.112401785714286, 0.0000 },
    {  1.792741071428571, -0.532909328559444,  0.0000,            0.0000 },
    { -0.972945075016308,  0.301482665475862, -1.133402217873451, 0.0000 },
};

static const char *fragment_program_source =
    "!!ARBfp1.0"
    "OPTION ARB_precision_hint_fastest;"

    "TEMP src;"
    "TEX src.x, fragment.texcoord[0], texture[0], 2D;"
    "TEX src.y, fragment.texcoord[1], texture[1], 2D;"
    "TEX src.z, fragment.texcoord[2], texture[2], 2D;"

    "PARAM coefficient[4] = { program.local[0..3] };"

    "TEMP tmp;"
    "MAD  tmp.rgb,          src.xxxx, coefficient[0], coefficient[3];"
    "MAD  tmp.rgb,          src.yyyy, coefficient[1], tmp;"
    "MAD  result.color.rgb, src.zzzz, coefficient[2], tmp;"
    "END";

static int has_extension(const char *apis, const char *api)
{
    size_t apilen = strlen(api);
    while (apis) {
        while (*apis == ' ')
            apis++;
        if (!strncmp(apis, api, apilen) && memchr(" ", apis[apilen], 2))
            return 1;
        apis = strchr(apis, ' ');
    }
    return 0;
}

static int initialize_gl(void) {
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    glGenProgramsARB = (PFNGLGENPROGRAMSARBPROC) SDL_GL_GetProcAddress("glGenProgramsARB");
    glBindProgramARB = (PFNGLBINDPROGRAMARBPROC) SDL_GL_GetProcAddress("glBindProgramARB");
    glProgramStringARB = (PFNGLPROGRAMSTRINGARBPROC) SDL_GL_GetProcAddress("glProgramStringARB");
    glDeleteProgramsARB = (PFNGLDELETEPROGRAMSARBPROC) SDL_GL_GetProcAddress("glDeleteProgramsARB");
    glProgramLocalParameter4fvARB = (PFNGLPROGRAMLOCALPARAMETER4FVARBPROC) SDL_GL_GetProcAddress("glProgramLocalParameter4fvARB");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &max_texture_units);
    if (!extensions ||
        !glGenProgramsARB ||
        !glBindProgramARB ||
        !glProgramStringARB ||
        !glDeleteProgramsARB ||
        !glProgramLocalParameter4fvARB ||
        !has_extension(extensions, "GL_ARB_multitexture") ||
        !has_extension(extensions, "GL_ARB_texture_non_power_of_two") ||
        !has_extension(extensions, "GL_ARB_fragment_program"))
        return 0;
    glGenProgramsARB(1, &fragment_program);
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, fragment_program);
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,
            GL_PROGRAM_FORMAT_ASCII_ARB,
            strlen(fragment_program_source), fragment_program_source);
    return 1;
}

static FFplayVideoOutputTexture *alloc_texture(int width, int height) {
    FFplayVideoOutputTexture *tex = av_malloc(sizeof(FFplayVideoOutputTexture));
    GLOverlay *ctx;
    if (tex) {
        tex->priv = ctx = av_calloc(1, sizeof(GLOverlay));
        if (!ctx)
            goto err;
        if (!initialized) {
            if (!initialize_gl())
                goto err;
            initialized = 1;
        }
        if (width > max_texture_size || height > max_texture_size || max_texture_units < 3)
            goto err;
        glGenTextures(3, ctx->textures);
        ctx->w = width;
        ctx->h = height;
        avcodec_get_frame_defaults(&ctx->frame);
        ctx->frame.width = width;
        ctx->frame.height = height;
        ctx->frame.format = AV_PIX_FMT_YUV420P;
        if (av_frame_get_buffer(&ctx->frame, 16) < 0)
            goto err;
    }
    return tex;
err:
    av_free(ctx);
    av_free(tex);
    return NULL;
}

static void free_texture(FFplayVideoOutputTexture **texp) {
    if (*texp) {
        GLOverlay *ctx = (*texp)->priv;
        glDeleteTextures(3, ctx->textures);
        av_frame_unref(&ctx->frame);
        av_freep(&(*texp)->priv);
    }
    av_freep(texp);
}

static int get_alignment(int linesize) {
  if (linesize % 8 == 0)
    return 8;
  else if (linesize % 4 == 0)
    return 4;
  else if (linesize % 2 == 0)
    return 2;
  else
    return 1;
}

static void load_texture(GLOverlay *ctx, uint8_t **data, int *linesize) {
    int i;
    for (i=0; i<3; i++) {
        int shift = (i>0?1:0);
        glBindTexture(GL_TEXTURE_2D, ctx->textures[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, get_alignment(linesize[i]));
        glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[i]);
        if (ctx->texture_initialized) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ctx->w >> shift, ctx->h >> shift, GL_LUMINANCE, GL_UNSIGNED_BYTE, data[i]);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, 1, ctx->w >> shift, ctx->h >> shift, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data[i]);
        }
    }
    ctx->texture_initialized = 1;
    ctx->texture_ready = 1;
}

static void display_texture(FFplayVideoOutputTexture *tex, SDL_Rect *rect) {
    GLOverlay *ctx = tex->priv;
    const float (*matrix)[4] = ctx->h > 576 ? matrix_bt709_tv2full : matrix_bt601_tv2full;
    int i;

    glViewport(rect->x, rect->y, rect->w, rect->h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, rect->w, rect->h, 0, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (!ctx->texture_ready)
        load_texture(ctx, ctx->frame.data, ctx->frame.linesize);

    for (i=0; i<3; i++) {
        glActiveTextureARB(GL_TEXTURE0_ARB + i);
        glBindTexture(GL_TEXTURE_2D, ctx->textures[i]);
        glEnable(GL_TEXTURE_2D);
    }

    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, fragment_program);
    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    for (i=0; i<4; i++)
        glProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, i, matrix[i]);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_QUADS);
        for (i=0;i<3;i++)
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB+i, 0.0, 0.0);
        glVertex2d(0, 0);
        for (i=0;i<3;i++)
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB+i, 1.0, 0.0);
        glVertex2d(rect->w, 0);
        for (i=0;i<3;i++)
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB+i, 1.0, 1.0);
        glVertex2d(rect->w, rect->h);
        for (i=0;i<3;i++)
            glMultiTexCoord2fARB(GL_TEXTURE0_ARB+i, 0.0, 1.0);
        glVertex2d(0, rect->h);
    glEnd();
    SDL_GL_SwapBuffers();
}

static void fill_texture(FFplayVideoOutputTexture *tex, AVFrame *src_frame, struct SwsContext* img_convert_ctx) {
    GLOverlay *ctx = tex->priv;

    if (img_convert_ctx) {
        av_frame_make_writable(&ctx->frame);
        sws_scale(img_convert_ctx, src_frame->data, src_frame->linesize,
                  0, ctx->h, ctx->frame.data, ctx->frame.linesize);
    } else {
        av_frame_unref(&ctx->frame);
        av_frame_ref(&ctx->frame, src_frame);
    }
    ctx->texture_ready = 0;
}

static void blend_texture(FFplayVideoOutputTexture *tex, AVSubtitle *sub) {
    GLOverlay *ctx = tex->priv;
    int i;
    av_frame_make_writable(&ctx->frame);
    for (i = 0; i < sub->num_rects; i++)
        blend_subrect((AVPicture *)&ctx->frame, sub->rects[i], ctx->w, ctx->h);
    ctx->texture_ready = 0;
}

FFplayVideoOutput ffplay_video_output_gl = {
    .sdl_flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL | SDL_OPENGL | SDL_GL_DOUBLEBUFFER,
    .alloc_texture = alloc_texture,
    .free_texture = free_texture,
    .fill_texture = fill_texture,
    .display_texture = display_texture,
    .blend_texture = blend_texture
};
