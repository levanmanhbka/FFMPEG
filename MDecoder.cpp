#include "MDecoder.h"

int MDecoder::get_raw_data_video(AVFrame *frame, FILE* file)
{
	fwrite(frame->data[0], sizeof(unsigned char), frame->height * frame->linesize[0], file);
	fwrite(frame->data[1], sizeof(unsigned char), frame->height * frame->linesize[1] / 2, file);
	fwrite(frame->data[2], sizeof(unsigned char), frame->height * frame->linesize[2] / 2, file);
	int size = av_image_get_buffer_size((AVPixelFormat)frame->format, frame->width, frame->height, 1);
	return size;
}

int MDecoder::get_raw_data_audio(AVCodecContext* ctx, AVFrame *frame, FILE *file)
{
	int s_sample = av_get_bytes_per_sample(ctx->sample_fmt);
	int n_sample = frame->nb_samples;
	int n_channels = frame->channels;

	int count = 0;
	for (int c = 0; c < n_channels; c++)
	{
		for (int i = 0; i < n_sample; i++) {
			fwrite(frame->data[c] + i * s_sample, 1, s_sample, file);
			count += s_sample;
		}
	}
	return 0;
}

void MDecoder::decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt, FILE* file, bool is_audio)
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

		if (is_audio)
		{
			get_raw_data_audio(dec_ctx, frame, file);
		}
		else
		{
			get_raw_data_video(frame, file);
		}
	}
}

int MDecoder::open_codec_context(int *stream_idx,
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

int MDecoder::run_decode_video()
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
			decode(video_dec_ctx, frame, &pkt, video_dst_file, false);
		}
		else if (pkt.stream_index == audio_stream_idx)
		{
			decode(audio_dec_ctx, frame, &pkt, audio_dst_file, true);
		}
	}

	/* flush cached frames */
	pkt.data = NULL;
	pkt.size = 0;

	printf("Demuxing succeeded.\n");
end:
	avcodec_free_context(&video_dec_ctx);
	avcodec_free_context(&audio_dec_ctx);
	avformat_close_input(&fmt_ctx);
	if (video_dst_file)
		fclose(video_dst_file);
	if (audio_dst_file)
		fclose(audio_dst_file);
	av_frame_free(&frame);
	return 0;
}