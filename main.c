#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>

static int OpenFile(char *file, AVFormatContext ** ffmt,
		    AVCodecContext ** ddc, int *stream_indexx)
{
    AVFormatContext *fmtctx = 0;
    int ret = 0;
    AVCodecContext *dctx = 0;

    ret = avformat_open_input(&fmtctx, file, 0, 0);
    if (ret < 0)
	return 0;
    do {
	AVCodec *dec = 0;

	ret = avformat_find_stream_info(fmtctx, 0);
	if (ret < 0)
	    break;
	ret =
	    av_find_best_stream(fmtctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec,
				0);
	if (ret < 0)
	    break;
	int stream_index = ret;
	dctx = avcodec_alloc_context3(dec);
	if (!dctx)
	    break;
	avcodec_parameters_to_context(dctx,
				      fmtctx->streams[stream_index]->
				      codecpar);
	ret = avcodec_open2(dctx, dec, 0);
	do {
	    if (ret < 0)
		break;
	    *ffmt = fmtctx;
	    *ddc = dctx;
	    *stream_indexx = stream_index;
	    return 0;
	} while (0);
	avcodec_free_context(&dctx);
    } while (0);
    avformat_close_input(&fmtctx);
    return ret;
}

static
int Process(AVFormatContext * fmtctx, AVCodecContext * dctx,
	    int stream_index, AVFilterGraph * fg, AVFilterContext * src,
	    AVFilterContext * sink, AVCodecContext * enc,
	    AVFormatContext * ofmt)
{
    int ret;
    do {
	AVPacket pkt;
	for (; 1; av_packet_unref(&pkt)) {
	    ret = av_read_frame(fmtctx, &pkt);
	    if (ret < 0)
		break;
	    if (pkt.stream_index != stream_index)
		continue;
	    ret = avcodec_send_packet(dctx, &pkt);

	    do {
		AVFrame *frame = av_frame_alloc();
		for (;; av_frame_unref(frame)) {
		    ret = avcodec_receive_frame(dctx, frame);
		    if (ret < 0)
			break;
		    ret = av_buffersrc_add_frame(src, frame);
		    do {
			if (ret < 0)
			    break;
			while (1) {
			    ret = av_buffersink_get_frame(sink, frame);
			    if (ret < 0)
				break;
			    ret = avcodec_send_frame(enc, frame);
			    AVPacket *pkt = av_packet_alloc();
			    do {
				if (ret < 0)
				    break;
				ret = avcodec_receive_packet(enc, pkt);
				if (ret < 0)
				    break;
				av_interleaved_write_frame(ofmt, pkt);
			    } while (0);
			    av_packet_free(&pkt);
			}
		    } while (0);
		}
		av_frame_free(&frame);
	    } while (0);
	}
    } while (0);
    return 0;
}

static void CloseFile(AVFormatContext ** fmtctx, AVCodecContext ** dctx)
{
    avcodec_free_context(dctx);
    avformat_close_input(fmtctx);
}

static
int OpenFilterGraph(decoder, encoder, ffg, src, sink)
AVCodecContext *decoder, *encoder;
AVFilterContext **src, **sink;
AVFilterGraph **ffg;
{
    int err;
    AVFilterGraph *fg = avfilter_graph_alloc();
    AVFilterContext *abuffer_ctx = 0, *aformat_ctx = 0, *abuffersink_ctx;
    do {
	abuffer_ctx =
	    avfilter_graph_alloc_filter(fg,
					avfilter_get_by_name("abuffer"),
					"src");

	char ch_layout[64];
	av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0,
				     decoder->channel_layout);
	av_opt_set(abuffer_ctx, "channel_layout", ch_layout,
		   AV_OPT_SEARCH_CHILDREN);
	av_opt_set(abuffer_ctx, "sample_fmt",
		   av_get_sample_fmt_name(decoder->sample_fmt),
		   AV_OPT_SEARCH_CHILDREN);
	av_opt_set_q(abuffer_ctx, "time_base", (AVRational) {
		     1, decoder->sample_rate}
		     , AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(abuffer_ctx, "sample_rate", decoder->sample_rate,
		       AV_OPT_SEARCH_CHILDREN);

	err = avfilter_init_str(abuffer_ctx, 0);
	if (err < 0)
	    break;

	aformat_ctx =
	    avfilter_graph_alloc_filter(fg,
					avfilter_get_by_name("aformat"),
					"aformat");

	/* A third way of passing the options is in a string of the form
	 * key1=value1:key2=value2.... */
	char options_str[1053];
	snprintf(options_str, sizeof(options_str),
		 "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"
		 PRIx64, av_get_sample_fmt_name(encoder->sample_fmt),
		 encoder->sample_rate, (uint64_t) encoder->channel_layout);
	err = avfilter_init_str(aformat_ctx, options_str);
	if (err < 0)
	    break;

	abuffersink_ctx =
	    avfilter_graph_alloc_filter(fg,
					avfilter_get_by_name
					("abuffersink"), "sink");

	/* This filter takes no options. */
	err = avfilter_init_str(abuffersink_ctx, NULL);

	/* Connect the filters;
	 * in this simple case the filters just form a linear chain. */
	err = avfilter_link(abuffer_ctx, 0, aformat_ctx, 0);
	err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);

	/* Configure the graph. */
	err = avfilter_graph_config(fg, NULL);
	if (err < 0)
	    break;

	*src = abuffer_ctx;
	*sink = abuffersink_ctx;
	*ffg = fg;

    } while (0);

    return err;
}

static
int std_write(void *dta, uint8_t * buf, int sz)
{
    return fwrite(buf, sz, 1, dta);
}

int main(argsc, args, env)
int argsc;
char **args, **env;
{
    AVCodecContext *decoder = 0;
    AVFormatContext *fmt = 0, *ofmt = 0;

    AVFilterContext *src, *sink;
    AVFilterGraph *fg = 0;
    int stream_index = 0;


    int ret = OpenFile(args[1], &fmt, &decoder, &stream_index);
    if (ret < 0)
	return 1;
    do {
	ret = avformat_alloc_output_context2(&ofmt, 0, "sox", 0);
	if (ret < 0)
	    break;

	ofmt->pb =
	    avio_alloc_context(av_malloc(1054), 1054, 1, stdout, 0,
			       std_write, 0);
	AVCodec *enc = avcodec_find_encoder(ofmt->oformat->audio_codec);

	AVCodecContext *enctx = avcodec_alloc_context3(enc);
	enctx->sample_rate = 44100;
	enctx->sample_fmt = enc->sample_fmts[0];
	enctx->channel_layout = AV_CH_LAYOUT_STEREO;
	enctx->channels = 2;

	ret = avcodec_open2(enctx, enc, 0);
	if (ret < 0)
	    break;
	AVStream *fstream = avformat_new_stream(ofmt, enc);
	avcodec_parameters_from_context(fstream->codecpar, enctx);

	OpenFilterGraph(decoder, enctx, &fg, &src, &sink);
#if 0
#endif
	ret = avformat_write_header(ofmt, 0);
	if (ret < 0)
	    break;
	Process(fmt, decoder, stream_index, fg, src, sink, enctx, ofmt);
    } while (0);
    CloseFile(&fmt, &decoder);
}
