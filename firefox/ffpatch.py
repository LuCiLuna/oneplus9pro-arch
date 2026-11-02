#!/usr/bin/env python3
# Makes FFmpeg's V4L2 M2M decoders offer AV_PIX_FMT_DRM_PRIME, which is what
# Firefox's V4L2 path requires. Callers that don't ask for it are unaffected.
import sys

R = sys.argv[1] + '/libavcodec/'


def sub(f, old, new):
    s = open(R + f).read()
    assert s.count(old) == 1, (f, old, s.count(old))
    open(R + f, 'w').write(s.replace(old, new))


# Context state
sub('v4l2_m2m.h', '''    /* reference back to V4L2m2mPriv */
''', '''    /* capture frames are returned as AV_PIX_FMT_DRM_PRIME dmabufs */
    int output_drm;

    /* input packet durations, the driver only carries the timestamp back */
    struct {
        int64_t pts;
        int64_t duration;
    } pkt_info[64];
    unsigned pkt_info_idx;

    /* reference back to V4L2m2mPriv */
''')
sub('v4l2_buffers.h', '''#include "libavutil/frame.h"
''', '''#include "libavutil/frame.h"
#include "libavutil/hwcontext_drm.h"
''')
sub('v4l2_buffers.h', '''    int num_planes;

    /* the v4l2_buffer buf.m.planes''', '''    int num_planes;

    /* dmabuf view of the capture buffer, fds are owned by this struct */
    AVDRMFrameDescriptor drm_frame;

    /* the v4l2_buffer buf.m.planes''')

# Firefox passes microseconds and sets no timebase, which made av_rescale_q()
# divide by zero and return AV_NOPTS_VALUE for every frame.
sub('v4l2_buffers.c', '''    if (s->avctx->pkt_timebase.num)
        return s->avctx->pkt_timebase;
    return s->avctx->time_base;
''', '''    if (s->avctx->pkt_timebase.num)
        return s->avctx->pkt_timebase;
    if (s->avctx->time_base.num)
        return s->avctx->time_base;
    /* no timebase set: pass timestamps through untouched */
    return v4l2_timebase;
''')

# FFmpeg builds with _XOPEN_SOURCE=600, which hides O_CLOEXEC
sub('v4l2_buffers.c', '''#include <poll.h>
''', '''#include <poll.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif

''')

# Frame output
sub('v4l2_buffers.c', '''int ff_v4l2_buffer_buf_to_avframe(AVFrame *frame, V4L2Buffer *avbuf)
{
    int ret;

    av_frame_unref(frame);

    /* 1. get references to the actual data */
    ret = v4l2_buffer_buf_to_swframe(frame, avbuf);
    if (ret)
        return ret;
''', '''static int v4l2_buffer_buf_to_drmframe(AVFrame *frame, V4L2Buffer *avbuf)
{
    int ret;

    if (avbuf->context->av_pix_fmt != AV_PIX_FMT_NV12 || !avbuf->drm_frame.nb_objects)
        return AVERROR(ENOSYS);

    frame->buf[0] = av_buffer_create((uint8_t *)&avbuf->drm_frame, sizeof(avbuf->drm_frame),
                                     v4l2_free_buffer, avbuf, AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    ret = v4l2_buf_increase_ref(avbuf);
    if (ret) {
        av_buffer_unref(&frame->buf[0]);
        return ret;
    }

    frame->format = AV_PIX_FMT_DRM_PRIME;
    frame->data[0] = (uint8_t *)&avbuf->drm_frame;

    return 0;
}

static int v4l2_buffer_export_drm(V4L2Buffer *avbuf)
{
    V4L2Context *ctx = avbuf->context;
    AVDRMFrameDescriptor *desc = &avbuf->drm_frame;
    AVDRMLayerDescriptor *layer = &desc->layers[0];
    int pitch = avbuf->plane_info[0].bytesperline;
    int i;

    if (ctx->av_pix_fmt != AV_PIX_FMT_NV12 || avbuf->num_planes != 1)
        return AVERROR(ENOSYS);

    for (i = 0; i < avbuf->num_planes; i++) {
        struct v4l2_exportbuffer expbuf = {
            .type  = ctx->type,
            .index = avbuf->buf.index,
            .plane = i,
            .flags = O_CLOEXEC | O_RDWR,
        };

        if (ioctl(buf_to_m2mctx(avbuf)->fd, VIDIOC_EXPBUF, &expbuf) < 0)
            return AVERROR(errno);

        desc->objects[i].fd = expbuf.fd;
        desc->objects[i].size = avbuf->plane_info[i].length;
        desc->objects[i].format_modifier = 0; /* DRM_FORMAT_MOD_LINEAR */
        desc->nb_objects++;
    }

    desc->nb_layers = 1;
    layer->format = MKTAG('N', 'V', '1', '2'); /* DRM_FORMAT_NV12 */
    layer->nb_planes = 2;
    layer->planes[0].object_index = 0;
    layer->planes[0].offset = 0;
    layer->planes[0].pitch = pitch;
    layer->planes[1].object_index = 0;
    layer->planes[1].offset = pitch * ctx->format.fmt.pix_mp.height;
    layer->planes[1].pitch = pitch;

    return 0;
}

int ff_v4l2_buffer_buf_to_avframe(AVFrame *frame, V4L2Buffer *avbuf)
{
    V4L2m2mContext *s = buf_to_m2mctx(avbuf);
    int ret;

    av_frame_unref(frame);

    /* 1. get references to the actual data */
    if (s->output_drm)
        ret = v4l2_buffer_buf_to_drmframe(frame, avbuf);
    else
        ret = v4l2_buffer_buf_to_swframe(frame, avbuf);
    if (ret)
        return ret;
''')
sub('v4l2_buffers.c', '''    frame->pts = v4l2_get_pts(avbuf);
    frame->pkt_dts = AV_NOPTS_VALUE;
''', '''    frame->pts = v4l2_get_pts(avbuf);
    frame->pkt_dts = s->output_drm ? frame->pts : AV_NOPTS_VALUE;
''')
sub('v4l2_buffers.c', '''    avbuf->status = V4L2BUF_AVAILABLE;

    if (V4L2_TYPE_IS_OUTPUT(ctx->type))
        return 0;
''', '''    avbuf->status = V4L2BUF_AVAILABLE;

    if (V4L2_TYPE_IS_OUTPUT(ctx->type))
        return 0;

    if (buf_to_m2mctx(avbuf)->output_drm) {
        ret = v4l2_buffer_export_drm(avbuf);
        if (ret)
            return ret;
    }
''')
sub('v4l2_buffers.c', '''            else if (avbuf->context->streamon)
                ff_v4l2_buffer_enqueue(avbuf);
''', '''            else if (avbuf->context->streamon)
                ff_v4l2_buffer_enqueue(avbuf);
            else
                avbuf->status = V4L2BUF_AVAILABLE; /* requeued by the flush */
''')
sub('v4l2_context.c', '''                    av_log(logger(ctx), AV_LOG_ERROR, "%s unmap plane (%s))\\n", ctx->name, av_err2str(AVERROR(errno)));
        }
''', '''                    av_log(logger(ctx), AV_LOG_ERROR, "%s unmap plane (%s))\\n", ctx->name, av_err2str(AVERROR(errno)));
        }

        for (j = 0; j < buffer->drm_frame.nb_objects; j++)
            close(buffer->drm_frame.objects[j].fd);
        buffer->drm_frame.nb_objects = 0;
''')

sub('v4l2_buffers.c', '''    /* 3. report errors upstream */
    if (avbuf->buf.flags & V4L2_BUF_FLAG_ERROR) {
''', '''    /* 3. report errors upstream */
    if (s->output_drm && !avbuf->planes[0].bytesused)
        frame->decode_error_flags |= FF_DECODE_ERROR_INVALID_BITSTREAM;
    if (avbuf->buf.flags & V4L2_BUF_FLAG_ERROR) {
''')

# Decoder
sub('v4l2_m2m_dec.c', '''#include "libavutil/pixfmt.h"
''', '''#include <string.h>
#include "libavutil/macros.h"
#include "libavutil/pixfmt.h"
''')
sub('v4l2_m2m_dec.c', '''    /* 2.1 update the AVCodecContext */
    avctx->pix_fmt = ff_v4l2_format_v4l2_to_avfmt(capture->format.fmt.pix_mp.pixelformat, AV_CODEC_ID_RAWVIDEO);
    capture->av_pix_fmt = avctx->pix_fmt;
''', '''    /* 2.1 update the AVCodecContext */
    avctx->pix_fmt = ff_v4l2_format_v4l2_to_avfmt(capture->format.fmt.pix_mp.pixelformat, AV_CODEC_ID_RAWVIDEO);
    capture->av_pix_fmt = avctx->pix_fmt;

    /* 2.2 let the caller opt in to dmabuf frames */
    if (avctx->pix_fmt == AV_PIX_FMT_NV12 && V4L2_TYPE_IS_MULTIPLANAR(capture->type)) {
        const enum AVPixelFormat fmts[] = { AV_PIX_FMT_DRM_PRIME, AV_PIX_FMT_NV12, AV_PIX_FMT_NONE };

        avctx->sw_pix_fmt = AV_PIX_FMT_NV12;
        ret = ff_get_format(avctx, fmts);
        if (ret == AV_PIX_FMT_DRM_PRIME) {
            s->output_drm = 1;
            avctx->pix_fmt = AV_PIX_FMT_DRM_PRIME;
        }
    }
''')
sub('v4l2_m2m_dec.c', '''static int v4l2_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
''', '''/* Dequeue a frame, fixing up what the dmabuf path's callers rely on. */
static int v4l2_dequeue_frame(V4L2m2mContext *s, AVFrame *frame, int timeout)
{
    int ret = ff_v4l2_context_dequeue_frame(&s->capture, frame, timeout);
    int i;

    if (ret < 0 || !s->output_drm)
        return ret;

    /* Frames the driver flags as errors have no picture to show, for example
     * VP9 frames that are only decoded to be referenced. */
    if (frame->decode_error_flags) {
        av_frame_unref(frame);
        return AVERROR(EAGAIN);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->pkt_info); i++) {
        if (s->pkt_info[i].pts == frame->pts) {
            frame->duration = s->pkt_info[i].duration;
            break;
        }
    }

    return 0;
}

static int v4l2_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
''')
sub('v4l2_m2m_dec.c', '''    V4L2Context *const capture = &s->capture;
    V4L2Context *const output = &s->output;
    int ret;

    if (!s->buf_pkt.size) {
''', '''    V4L2Context *const output = &s->output;
    int ret;

    if (!s->buf_pkt.size) {
''')
sub('v4l2_m2m_dec.c', '''                return ff_v4l2_context_dequeue_frame(capture, frame, 0);
''', '''                return v4l2_dequeue_frame(s, frame, 0);
''')
sub('v4l2_m2m_dec.c', '''    if (s->draining)
        goto dequeue;

    ret = ff_v4l2_context_enqueue_packet(output, &s->buf_pkt);
''', '''    if (s->draining)
        goto dequeue;

    if (s->buf_pkt.size) {
        unsigned idx = s->pkt_info_idx++ % FF_ARRAY_ELEMS(s->pkt_info);

        s->pkt_info[idx].pts = s->buf_pkt.pts;
        s->pkt_info[idx].duration = s->buf_pkt.duration;
    }

    ret = ff_v4l2_context_enqueue_packet(output, &s->buf_pkt);
''')
sub('v4l2_m2m_dec.c', '''dequeue:
    return ff_v4l2_context_dequeue_frame(capture, frame, -1);
''', '''dequeue:
    return v4l2_dequeue_frame(s, frame, -1);
''')
sub('v4l2_m2m_dec.c', '''static av_cold int v4l2_decode_close(AVCodecContext *avctx)
''', '''/* Drop everything queued so that a seek doesn't output frames from before it. */
static void v4l2_decode_flush(AVCodecContext *avctx)
{
    V4L2m2mContext *s = ((V4L2m2mPriv*)avctx->priv_data)->context;
    V4L2Context *const capture = &s->capture;
    V4L2Context *const output = &s->output;
    AVFrame *frame;
    int i;

    av_packet_unref(&s->buf_pkt);
    memset(s->pkt_info, 0, sizeof(s->pkt_info));

    if (!capture->streamon)
        return;

    ff_v4l2_context_set_status(output, VIDIOC_STREAMOFF);
    output->done = 0;
    for (i = 0; i < output->num_buffers; i++)
        if (output->buffers[i].status == V4L2BUF_IN_DRIVER)
            output->buffers[i].status = V4L2BUF_AVAILABLE;

    /*
     * Mid-stream, stopping the input is the whole seek sequence. A drained
     * decoder has stopped though, and the capture queue must restart too.
     */
    if (s->draining) {
        ff_v4l2_context_set_status(capture, VIDIOC_STREAMOFF);
        capture->done = 0;
        for (i = 0; i < capture->num_buffers; i++)
            if (capture->buffers[i].status == V4L2BUF_IN_DRIVER)
                capture->buffers[i].status = V4L2BUF_AVAILABLE;
        for (i = 0; i < capture->num_buffers; i++)
            if (capture->buffers[i].status == V4L2BUF_AVAILABLE)
                ff_v4l2_buffer_enqueue(&capture->buffers[i]);
        ff_v4l2_context_set_status(capture, VIDIOC_STREAMON);
        s->draining = 0;
    }

    ff_v4l2_context_set_status(output, VIDIOC_STREAMON);

    /* Return what the driver finished before the stop; poll errors while the
     * input is off, so this has to come after the restart. */
    frame = av_frame_alloc();
    if (frame) {
        while (ff_v4l2_context_dequeue_frame(capture, frame, 0) >= 0)
            av_frame_unref(frame);
        av_frame_free(&frame);
    }
    capture->done = 0;
}

static av_cold int v4l2_decode_close(AVCodecContext *avctx)
''')
sub('v4l2_m2m_dec.c', '''        .close          = v4l2_decode_close, \\
''', '''        .close          = v4l2_decode_close, \\
        .flush          = v4l2_decode_flush, \\
''')
print('patched')
