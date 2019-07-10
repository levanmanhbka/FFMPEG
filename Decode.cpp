/*
* Copyright (c) 2001 Fabrice Bellard
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

/**
* @file
* video decoding with libavcodec API example
*
* @example decode_video.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "postproc.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")

#define INBUF_SIZE 4096


static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static const char *src_filename = "D:\\ffmpeg\\Temp\\video_test.mp4";
static const char *video_dst_filename = "D:\\ffmpeg\\Temp\\video_frame.raw";
static const char *audio_dst_filename = "D:\\ffmpeg\\Temp\\audio_frame.raw";
static FILE *video_dst_file = NULL;
static FILE *audio_dst_file = NULL;

static uint8_t *video_dst_data[4] = { NULL };
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static int audio_frame_count = 0;

/* Enable or disable frame reference counting. You are not supposed to support
* both paths in your application but pick the one most appropriate to your
* needs. Look for the use of refcount in this example to see what are the
* differences of API usage between them. */
static int refcount = 0;

static int dump_avframe_to_file(AVFrame *frame, FILE* file)
{
	fwrite(frame->data[0], sizeof(unsigned char), frame->height * frame->linesize[0], file);
	fwrite(frame->data[1], sizeof(unsigned char), frame->height * frame->linesize[1] / 2, file);
	fwrite(frame->data[2], sizeof(unsigned char), frame->height * frame->linesize[2] / 2, file);
	int size = av_image_get_buffer_size((AVPixelFormat)frame->format, frame->width, frame->height, 1);
	return size;
}

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt, FILE* file)
{
	char buf[1024];
	int ret;

	ret = avcodec_send_packet(dec_ctx, pkt);
	if (ret < 0) {
		fprintf(stderr, "Error sending a packet for decoding\n");
		exit(1);
	}

	while (ret >= 0) {
		ret = avcodec_receive_frame(dec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error during decoding\n");
			exit(1);
		}

		printf("saving frame %3d \t pts= %3d\n", dec_ctx->frame_number, frame->pts);
		fflush(stdout);

		/* the picture is allocated by the decoder. no need to
		free it */
		dump_avframe_to_file(frame, file);
	}
}


static int open_codec_context(int *stream_idx,
	AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
	int ret, stream_index;
	AVStream *st;
	AVCodec *dec = NULL;
	AVDictionary *opts = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(type), src_filename);
		return ret;
	}
	else {
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		dec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!dec) {
			fprintf(stderr, "Failed to find %s codec\n",
				av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Allocate a codec context for the decoder */
		*dec_ctx = avcodec_alloc_context3(dec);
		if (!*dec_ctx) {
			fprintf(stderr, "Failed to allocate the %s codec context\n",
				av_get_media_type_string(type));
			return AVERROR(ENOMEM);
		}

		/* Copy codec parameters from input stream to output codec context */
		if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
			fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
				av_get_media_type_string(type));
			return ret;
		}

		/* Init the decoders, with or without reference counting */
		av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
		if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
				av_get_media_type_string(type));
			return ret;
		}
		*stream_idx = stream_index;
	}

	return 0;
}

static int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt)
{
	int i;
	struct sample_fmt_entry {
		enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
	} 
	sample_fmt_entries[] = {
		{ AV_SAMPLE_FMT_U8,  "u8",    "u8" },
		{ AV_SAMPLE_FMT_S16, "s16be", "s16le" },
		{ AV_SAMPLE_FMT_S32, "s32be", "s32le" },
		{ AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
		{ AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
	};
	*fmt = NULL;

	for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
		struct sample_fmt_entry *entry = &sample_fmt_entries[i];
		if (sample_fmt == entry->sample_fmt) {
			*fmt = AV_NE(entry->fmt_be, entry->fmt_le);
			return 0;
		}
	}

	fprintf(stderr,
		"sample format %s is not supported as output format\n",
		av_get_sample_fmt_name(sample_fmt));
	return -1;
}

int main(int argc, char **argv)
{
	int ret = 0, got_frame;

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", src_filename);
		exit(1);
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];

		video_dst_file = fopen(video_dst_filename, "wb");
		if (!video_dst_file) {
			fprintf(stderr, "Could not open destination file %s\n", video_dst_filename);
			ret = 1;
			goto end;
		}

		/* allocate image where the decoded image will be put */
		width = video_dec_ctx->width;
		height = video_dec_ctx->height;
		pix_fmt = video_dec_ctx->pix_fmt;
		ret = av_image_alloc(video_dst_data, video_dst_linesize, width, height, pix_fmt, 1);
		if (ret < 0) {
			fprintf(stderr, "Could not allocate raw video buffer\n");
			goto end;
		}
		video_dst_bufsize = ret;
	}

	if (open_codec_context(&audio_stream_idx, &audio_dec_ctx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
		audio_stream = fmt_ctx->streams[audio_stream_idx];
		audio_dst_file = fopen(audio_dst_filename, "wb");
		if (!audio_dst_file) {
			fprintf(stderr, "Could not open destination file %s\n", audio_dst_filename);
			ret = 1;
			goto end;
		}
	}

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, src_filename, 0);

	if (!audio_stream && !video_stream) {
		fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
		ret = 1;
		goto end;
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	/* initialize packet, set data to NULL, let the demuxer fill it */
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	if (video_stream)
		printf("Demuxing video from file '%s' into '%s'\n", src_filename, video_dst_filename);
	if (audio_stream)
		printf("Demuxing audio from file '%s' into '%s'\n", src_filename, audio_dst_filename);

	/* read frames from the file */
	while (av_read_frame(fmt_ctx, &pkt) >= 0) {
		if (pkt.stream_index == video_stream_idx)
		{
			decode(video_dec_ctx, frame, &pkt, video_dst_file);
		}
		else if (pkt.stream_index == audio_stream_idx)
		{
			decode(audio_dec_ctx, frame, &pkt, audio_dst_file);
		}
	}

	/* flush cached frames */
	pkt.data = NULL;
	pkt.size = 0;
	//decode()


	printf("Demuxing succeeded.\n");

	if (video_stream) {
		printf("Play the output video file with the command:\n"
			"ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
			av_get_pix_fmt_name(pix_fmt), width, height,
			video_dst_filename);
	}

	if (audio_stream) {
		enum AVSampleFormat sfmt = audio_dec_ctx->sample_fmt;
		int n_channels = audio_dec_ctx->channels;
		const char *fmt;

		if (av_sample_fmt_is_planar(sfmt)) {
			const char *packed = av_get_sample_fmt_name(sfmt);
			printf("Warning: the sample format the decoder produced is planar "
				"(%s). This example will output the first channel only.\n",
				packed ? packed : "?");
			sfmt = av_get_packed_sample_fmt(sfmt);
			n_channels = 1;
		}

		if ((ret = get_format_from_sample_fmt(&fmt, sfmt)) < 0)
			goto end;

		printf("Play the output audio file with the command:\n"
			"ffplay -f %s -ac %d -ar %d %s\n",
			fmt, n_channels, audio_dec_ctx->sample_rate,
			audio_dst_filename);
	}
end:
	avcodec_free_context(&video_dec_ctx);
	avcodec_free_context(&audio_dec_ctx);
	avformat_close_input(&fmt_ctx);
	if (video_dst_file)
		fclose(video_dst_file);
	if (audio_dst_file)
		fclose(audio_dst_file);
	av_frame_free(&frame);
	av_free(video_dst_data[0]);
	return 0;
}
