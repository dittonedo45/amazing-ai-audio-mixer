#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/avstring.h>
#include <libavutil/mem.h>
#include <python3.8/Python.h>

//// Read A File Like Object
static
int pyy_read(PyObject * ob, unsigned char *buf, int sz)
{
    PyObject *args = PyTuple_New(1);
    PyTuple_SetItem(args, 0, PyLong_FromSize_t(sz));

    PyObject *ans = PyObject_Call(ob, args, 0);
    {
	char *buff = 0;
	PyBytes_AsStringAndSize(ans, &buff, &sz);
	memcpy(buf, buff, sz);
    }
    Py_DECREF(args);
    Py_DECREF(ans);

    if (PyErr_Occurred()) {
	return -1;
    }

    return sz;
}

static int OpenFile(PyObject * file, AVFormatContext ** ffmt,
		    AVCodecContext ** ddc, int *stream_indexx)
{
    AVFormatContext *fmtctx = 0;
    int ret = 0;
    AVCodecContext *dctx = 0;

    fmtctx = avformat_alloc_context();

    fmtctx->pb = avio_alloc_context(av_malloc(1054), 1054,
				    0, file,
				    (int (*)(void *, unsigned char *, int))
				    pyy_read, NULL, NULL);

    ret = avformat_open_input(&fmtctx, "<PyObject>", 0, 0);

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
				      fmtctx->
				      streams[stream_index]->codecpar);
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

static
int OpenFilterGraph(decoder, encoder, ffg, src, sink)
AVCodecContext *decoder, *encoder;
AVFilterContext **src, **sink;
AVFilterGraph **ffg;
{
    int err;
    AVFilterGraph *fg = avfilter_graph_alloc();
    AVFilterContext *abuffer_ctx = 0, *aformat_ctx =
	0, *abuffersink_ctx, *speed_ctx;
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

	{
	    speed_ctx =
		avfilter_graph_alloc_filter(fg,
					    avfilter_get_by_name
					    ("asetrate"), "speed");
	    char *ans = av_asprintf("%.g", encoder->sample_rate * 1.4);
	    err = avfilter_init_str(speed_ctx, ans);
	    av_free(ans);
	    if (err < 0)
		break;

	}

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

	err = avfilter_init_str(abuffersink_ctx, NULL);

	avfilter_link(abuffer_ctx, 0, speed_ctx, 0);
	avfilter_link(speed_ctx, 0, aformat_ctx, 0);
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
    PyObject *res = PyObject_Call(dta,
				  PyBytes_FromStringAndSize((const char *)
							    buf, sz),
				  0);
    int ans = PyLong_AsSize_t(res);
    Py_DECREF(res);

    return ans;
}

static void avpy_close_fmt(PyObject * ob)
{
    avformat_free_context(PyCapsule_GetPointer(ob, "_.fmt"));
}

static void avpy_close_cdc(PyObject * ob)
{
    avformat_free_context(PyCapsule_GetPointer(ob, "_.cbc"));
}

static PyObject *avpy_open(s, a)
PyObject *s, *a;
{
    do {
	PyObject *obj = 0;
	if (!PyArg_ParseTuple(a, "O", &obj))
	    break;
	if (!PyObject_HasAttrString(obj, "read"))
	    break;
	PyObject *rdr = PyObject_GetAttrString(obj, "read");
	if (PyCallable_Check(rdr) == 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "The Input argument must possess .read function");
	    break;
	}
	/*
	   static int OpenFile(PyObject * file, AVFormatContext ** ffmt,
	   AVCodecContext ** ddc, int *stream_indexx)
	 */
	AVFormatContext *fmt = 0;
	AVCodecContext *codec = 0;
	int stream_index = 0;

	if (OpenFile(rdr, &fmt, &codec, &stream_index) < 0) {
	    if (fmt) {
		avformat_free_context(fmt);
	    }
	    PyErr_Format(PyExc_RuntimeError, "Failed To open Media file");
	    break;
	}

	PyObject *ans = PyList_New(0);
	PyList_Append(ans, PyCapsule_New(fmt, "_.fmt", avpy_close_fmt));
	PyList_Append(ans, PyCapsule_New(codec, "_.cdc", avpy_close_cdc));
	PyList_Append(ans, PyLong_FromSize_t(stream_index));

	Py_INCREF(ans);
	return ans;

    } while (0);
    return NULL;
}

static PyObject *avpy_open_write(s, a)
PyObject *s, *a;
{

    do {
	int ret = 0;
	PyObject *file = 0;
	char *format = "sox";

	ret = PyArg_ParseTuple(a, "O|s", &file, &format);
	if (!ret)
	    break;
	if (PyObject_HasAttrString(file, "write") == 0)
	    break;
	PyObject *writer = PyObject_GetAttrString(file, "write");
	if (PyCallable_Check(writer) == 0) {
	    PyErr_Format(PyExc_RuntimeError,
			 "The Input argument must possess .write function");
	    break;
	}
	AVFormatContext *ofmt = 0;
	AVCodecContext *enctx = 0;
	do {
	    ret = avformat_alloc_output_context2(&ofmt, 0, "sox", 0);
	    if (ret < 0)
		break;

	    ofmt->pb =
		avio_alloc_context(av_malloc(1054), 1054, 1, writer, 0,
				   std_write, 0);
	    AVCodec *enc =
		avcodec_find_encoder(ofmt->oformat->audio_codec);

	    enctx = avcodec_alloc_context3(enc);
	    enctx->sample_rate = 44100;
	    enctx->sample_fmt = enc->sample_fmts[0];
	    enctx->channel_layout = AV_CH_LAYOUT_STEREO;
	    enctx->channels = 2;

	    ret = avcodec_open2(enctx, enc, 0);
	    if (ret < 0)
		break;

	    AVStream *fstream = avformat_new_stream(ofmt, enc);
	    avcodec_parameters_from_context(fstream->codecpar, enctx);
	} while (0);
	if (ret < 0) {
	    PyErr_Format(PyExc_RuntimeError, "Media Error, %s\n",
			 av_err2str(ret));
	    return 0;
	}

	PyObject *ans = PyList_New(0);
	PyList_Append(ans, PyCapsule_New(ofmt, "_.fmt", avpy_close_fmt));
	PyList_Append(ans, PyCapsule_New(enctx, "_.cdc", avpy_close_cdc));

	Py_INCREF(ans);
	return ans;
    } while (0);
    return 0;
}

static
PyMethodDef aaai_methods[] = {
    { "open_read", avpy_open, METH_VARARGS, "Audio Opener" },
    { "open_write", avpy_open_write, METH_VARARGS, "Audio write handle" },
    { NULL, NULL, -1, NULL },
};

PyModuleDef aaai_mod = {
    PyModuleDef_HEAD_INIT,
    "aaai",
    NULL, -1,
    aaai_methods,
    0, 0, 0, 0
};

PyObject *PyInit_aaai()
{
    return PyModule_Create(&aaai_mod);
}

int main(argsc, args, env)
int argsc;
char **args, **env;
{
    AVCodecContext *decoder = 0;
    AVFormatContext *fmt = 0;

    AVFilterContext *src, *sink;
    AVFilterGraph *fg = 0;
    int stream_index = 0;

    PyImport_AppendInittab("aaai", &PyInit_aaai);
    Py_Initialize();
    {
	PyRun_SimpleString(args[1]);
    }

    Py_Finalize();

#if 0

    OpenFilterGraph(decoder, enctx, &fg, &src, &sink);
    ret = avformat_write_header(ofmt, 0);
    if (ret < 0)
	break;

    Process(fmt, decoder, stream_index, fg, src, sink, enctx, ofmt);
#endif
}
