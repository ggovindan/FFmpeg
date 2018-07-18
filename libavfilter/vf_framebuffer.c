/*
 * Copyright (c) 2012-2014 Clément Bœsch <u pkh me>
 *
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

/**
 * @file
 * Format the frames received to gRpc
 *
 *
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libswscale/swscale.h"
#include "libavutil/frame.h"


enum FilterMode {
    MODE_WIRES,
    MODE_COLORMIX,
    NB_MODE
};


typedef struct FramebufferContext {
    const AVClass *class;
    int num_fps;
} FramebufferContext;

#define OFFSET(x) offsetof(FramebufferContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption framebuffer_options[] = {
        { "num_fps", "number of frames to extract", OFFSET(num_fps), AV_OPT_TYPE_INT, {.i64=2}, 1, INT_MAX, FLAGS},
        { NULL },
};

AVFILTER_DEFINE_CLASS(framebuffer);


static void save_frame(AVFilterContext *ctx, AVFrame *ref, int frame_count)
{
    AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpegCodec) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - could not get jpeg codec\n");
    }

    AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
    if (!jpegContext) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - could allocate jpeg context\n");
        return;
    }

    jpegContext->pix_fmt = ref->format;
    jpegContext->height = ref->height;
    jpegContext->width = ref->width;

    jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
    //jpegContext->time_base = (AVRational){1, 25};
    jpegContext->time_base.num = ctx->inputs[0]->time_base.num;
    jpegContext->time_base.den = ctx->inputs[0]->time_base.den;

    // open jpeg codec
    if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - could not open jpeg codec\n");
        return;
    }

    FILE *f;
    char JPEGFName[256];
    int ret;

    AVPacket packet = {.data = NULL, .size = 0};
    av_init_packet(&packet);

    if (avcodec_send_frame(jpegContext, ref) < 0) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - could not send frame to encode\n");
        return;
    }

    int got_packet = 0;
    ret = avcodec_receive_packet(jpegContext, &packet);
    if (ret != 0) {
        got_packet = 1;
    }

    if (ret == AVERROR(EAGAIN)) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - output is not available in the current state - user must try to send input\n");
    }

    /* deprecated method
    if (avcodec_encode_video2(jpegContext, &packet, ref, &got_frame) < 0) {
        av_log(ctx, AV_LOG_VERBOSE, "Guru - could not encode jpeg \n");
        return;
    } */

    snprintf(JPEGFName, sizeof(JPEGFName), "dumpframe-%06d.jpg", frame_count);
    printf("Writing %s\n",JPEGFName);
    f = fopen(JPEGFName, "wb");
    fwrite(packet.data, 1, packet.size, f);
    fclose(f);

}

static int filter_frame(AVFilterLink *inlink, AVFrame *ref)
{
    FramebufferContext *framebuffer = inlink->dst->priv;
    AVFilterContext *ctx = inlink->dst;

    av_log(ctx, AV_LOG_VERBOSE, "Guru - filter_frame inlink->frame_count_out=%10lld inlink->frame_count_in=%10lld\n", inlink->frame_count_out, inlink->frame_count_out);
    save_frame(ctx, ref, inlink->frame_count_out);
    return ff_filter_frame(inlink->dst->outputs[0], ref);
}


static const AVFilterPad framebuffer_inputs[] = {
        {
                .name         = "default",
                .type         = AVMEDIA_TYPE_VIDEO,
                .filter_frame = filter_frame,
        },
        { NULL }
};

static const AVFilterPad framebuffer_outputs[] = {
        {
                .name = "default",
                .type = AVMEDIA_TYPE_VIDEO,
        },
        { NULL }
};

AVFilter ff_vf_framebuffer = {
        .name          = "framebuffer",
        .description   = NULL_IF_CONFIG_SMALL("Buffer the frames and send to gRpc."),
        .priv_size     = sizeof(FramebufferContext),
        .inputs        = framebuffer_inputs,
        .outputs       = framebuffer_outputs,
        .priv_class    = &framebuffer_class,
        .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};

