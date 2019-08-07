#pragma once
#include "FFInclude.h"
class MDecoder
{
public:
	int get_raw_data_video(AVFrame *frame, FILE* file);
	int get_raw_data_audio(AVCodecContext* ctx, AVFrame *frame, FILE *file);
	void decode(AVCodecContext *dec_ctx,
		AVFrame *frame, AVPacket *pkt, FILE* file, bool is_audio);
	int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx,
		AVFormatContext *fmt_ctx, enum AVMediaType type);
	int run_decode_video();

private:
	AVFormatContext *fmt_ctx = NULL;
	AVCodecContext *video_dec_ctx = NULL;
	AVCodecContext *audio_dec_ctx = NULL;
	AVStream *video_stream = NULL;
	AVStream *audio_stream = NULL;
	AVPacket pkt;
	AVFrame *frame = NULL;

	// "rtsp://admin:lam1993@@192.168.100.5:554/live";
	const char *src_filename = "D:\\ffmpeg\\Temp\\video_test.mp4";
	const char *video_dst_filename = "D:\\ffmpeg\\Temp\\video_frame.raw";
	const char *audio_dst_filename = "D:\\ffmpeg\\Temp\\audio_frame.raw";
	FILE *video_dst_file = NULL;
	FILE *audio_dst_file = NULL;

	int video_stream_idx = -1;
	int audio_stream_idx = -1;

	/* Enable or disable frame reference counting. You are not supposed to support
	* both paths in your application but pick the one most appropriate to your
	* needs. Look for the use of refcount in this example to see what are the
	* differences of API usage between them. */
	int refcount = 0;
};