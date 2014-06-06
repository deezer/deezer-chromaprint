#define _WIN32_IE _WIN32_IE_IE70
#define NTDDI_VERSION NTDDI_VISTA
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define _WINVER _WIN32_WINNT_VISTA
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <Windows.h>
#include "chromaprint.h"
#include "fpcalc.h"
#include "debug.h"

extern "C" {
#include "libavcodec\avcodec.h"
#include "libavformat\avformat.h"
#include "libavutil\opt.h"
#include "libswresample\swresample.h"
}

#define HRESULT_FROM_AVERROR(x) x

namespace Chromaprint
{

HRESULT chromaprint_get_input_formats(int *formats_count, FPCalcInputFormat *formats)
{
	HRESULT hr = S_OK;
	AVInputFormat *ifmt = NULL;
	if (!formats_count)
	{
		DEBUG() << "ERROR: chromaprint_get_input_formats formats_count null";
		hr = E_INVALIDARG;
		goto done;
	}

	av_register_all(); // this is protected against being called more than once

	*formats_count = 0;

	while ((ifmt = av_iformat_next(ifmt)))
	{
		if (formats)
		{
			formats[*formats_count].name = ifmt->name;
			formats[*formats_count].long_name = ifmt->long_name;
			formats[*formats_count].extensions = ifmt->extensions;
		}
		(*formats_count)++;
	}

done:
	return hr;
}

HRESULT chromaprint_get_file_fingerprint(FPCalcInput *input, int options_count, FPCalcOption *options, FPCalcOutput *output)
{
	HRESULT hr = S_OK;
	ChromaprintContext *ctx = NULL;
	if (!input || !output)
	{
		DEBUG() << "ERROR: chromaprint_get_file_fingerprint arguments nulls";
		hr = E_INVALIDARG;
		goto done;
	}

	if (input->cb_size != sizeof(FPCalcInput) || output->cb_size != sizeof(FPCalcOutput))
	{
		DEBUG() << "ERROR: chromaprint_get_file_fingerprint wrong cb_size";
		hr = E_INVALIDARG;
		goto done;
	}

	if (!input->file_name)
	{
		DEBUG() << "ERROR: chromaprint_get_file_fingerprint file_name null";
		hr = E_INVALIDARG;
		goto done;
	}

	if (input->max_length <= 0)
	{
		input->max_length = 120;
	}

	ctx = chromaprint_new(input->algo);
	if (!ctx)
	{
		DEBUG() << "ERROR: chromaprint_get_file_fingerprint chromaprint_new returns null";
		hr = E_OUTOFMEMORY;
		goto done;
	}

	if (options && options_count > 0)
	{
		for (int i = 0; i < options_count; i++)
		{
			chromaprint_set_option(ctx, options[i].name, options[i].value);
		}
	}

	av_register_all(); // this is protected against being called more than once

#ifdef NDEBUG
	av_log_set_level(0);
#endif

	hr = decode_audio_file(ctx, input->file_name, input->max_length, &output->duration);
	if (FAILED(hr))
		goto done;

	if (!chromaprint_get_fingerprint(ctx, &output->fingerprint))
	{
		DEBUG() << "ERROR: chromaprint_get_file_fingerprint chromaprint_get_fingerprint fails";
		hr = E_FAIL;
		goto done;
	}

done:
	if (ctx)
	{
		chromaprint_free(ctx);
	}
	return hr;
}

HRESULT decode_audio_file(ChromaprintContext *chromaprint_ctx, const char *file_name, int max_length, int *duration)
{
	HRESULT hr = S_OK;
	int averror = 0, remaining, length, consumed, codec_ctx_opened = 0, got_frame, stream_index;
	AVFormatContext *format_ctx = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVCodec *codec = NULL;
	AVStream *stream = NULL;
	AVFrame *frame = NULL;
	SwrContext *convert_ctx = NULL;
	int max_dst_nb_samples = 0, dst_linsize = 0;
	uint8_t *dst_data[1] = { NULL };
	uint8_t **data;
	AVPacket packet;

	averror = avformat_open_input(&format_ctx, file_name, NULL, NULL);
	if (averror)
	{
		DEBUG() << "ERROR: decode_audio_file avformat_open_input failed";
		hr = HRESULT_FROM_AVERROR(averror);
		goto done;
	}

	averror = avformat_find_stream_info(format_ctx, NULL);
	if (averror < 0)
	{
		DEBUG() << "ERROR: decode_audio_file avformat_find_stream_info failed";
		hr = HRESULT_FROM_AVERROR(AVERROR_STREAM_NOT_FOUND); // standard return is AVERROR_EOF, but AVERROR_STREAM_NOT_FOUND tells more
		goto done;
	}

	stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (stream_index < 0)
	{
		DEBUG() << "ERROR: decode_audio_file av_find_best_stream AVMEDIA_TYPE_AUDIO failed";
		hr = HRESULT_FROM_AVERROR(stream_index);
		goto done;
	}

	stream = format_ctx->streams[stream_index];
	codec_ctx = stream->codec;
	codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_S16;

	averror = avcodec_open2(codec_ctx, codec, NULL);
	if (averror < 0)
	{
		DEBUG() << "ERROR: decode_audio_file avcodec_open2 failed";
		hr = HRESULT_FROM_AVERROR(averror);
		goto done;
	}

	codec_ctx_opened = 1;

	if (codec_ctx->channels <= 0)
	{
		DEBUG() << "ERROR: decode_audio_file no channels found in the audio stream";
		hr = E_FAIL;
		goto done;
	}

	if (codec_ctx->sample_fmt != AV_SAMPLE_FMT_S16)
	{
		int64_t channel_layout = codec_ctx->channel_layout;
		if (!channel_layout)
		{
			channel_layout = av_get_default_channel_layout(codec_ctx->channels);
		}

		convert_ctx = swr_alloc_set_opts(NULL,
			channel_layout, AV_SAMPLE_FMT_S16, codec_ctx->sample_rate,
			channel_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
			0, NULL);

		if (!convert_ctx)
		{
			DEBUG() << "ERROR: decode_audio_file swr_alloc_set_opts failed";
			hr = E_OUTOFMEMORY;
			goto done;
		}

		averror = swr_init(convert_ctx);
		if (averror < 0)
		{
			DEBUG() << "ERROR: decode_audio_file swr_init failed";
			hr = HRESULT_FROM_AVERROR(averror);
			goto done;
		}
	}

	if (stream->duration != AV_NOPTS_VALUE)
	{
		*duration = (int)(stream->time_base.num * stream->duration / stream->time_base.den);
	}
	else if (format_ctx->duration != AV_NOPTS_VALUE)
	{
		*duration = (int)(format_ctx->duration / AV_TIME_BASE);
	}
	else
	{
		DEBUG() << "ERROR: decode_audio_file couldn't detect the audio duration";
		hr = E_FAIL;
		goto done;
	}

	remaining = max_length * codec_ctx->channels * codec_ctx->sample_rate;
	chromaprint_start(chromaprint_ctx, codec_ctx->sample_rate, codec_ctx->channels);

	frame = avcodec_alloc_frame();

	while (1)
	{
		if (av_read_frame(format_ctx, &packet) < 0)
			break;

		if (packet.stream_index == stream_index)
		{
			avcodec_get_frame_defaults(frame);

			got_frame = 0;
			consumed = avcodec_decode_audio4(codec_ctx, frame, &got_frame, &packet);
			if (consumed < 0)
			{
				DEBUG() << "ERROR: decode_audio_file avcodec_decode_audio4 failed";
				hr = HRESULT_FROM_AVERROR(consumed);
				continue;
			}

			if (got_frame)
			{
				data = frame->data;
				if (convert_ctx)
				{
					if (frame->nb_samples > max_dst_nb_samples)
					{
						av_freep(&dst_data[0]);
						averror = av_samples_alloc(dst_data, &dst_linsize, codec_ctx->channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
						if (averror < 0)
						{
							DEBUG() << "ERROR: decode_audio_file av_samples_alloc failed";
							hr = HRESULT_FROM_AVERROR(averror);
							goto done;
						}

						max_dst_nb_samples = frame->nb_samples;
					}

					averror = swr_convert(convert_ctx, dst_data, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
					if (averror < 0)
					{
						DEBUG() << "ERROR: decode_audio_file swr_convert failed";
						hr = HRESULT_FROM_AVERROR(averror);
						goto done;
					}
					data = dst_data;
				}

				length = MIN(remaining, frame->nb_samples * codec_ctx->channels);
				if (!chromaprint_feed(chromaprint_ctx, data[0], length))
				{
					DEBUG() << "ERROR: decode_audio_file chromaprint_feed failed";
					hr = E_FAIL;
					goto done;
				}

				if (max_length)
				{
					remaining -= length;
					if (remaining <= 0)
						goto finish;
				}
			}
		}
		av_free_packet(&packet);
	}

finish:
	if (!chromaprint_finish(chromaprint_ctx))
	{
		DEBUG() << "ERROR: decode_audio_file chromaprint_finish failed";
		hr = E_FAIL;
		goto done;
	}

done:
	if (frame)
	{
		avcodec_free_frame(&frame);
	}

	if (dst_data[0])
	{
		av_freep(&dst_data[0]);
	}

	if (convert_ctx)
	{
		swr_free(&convert_ctx);
	}

	if (codec_ctx_opened)
	{
		avcodec_close(codec_ctx);
	}

	if (format_ctx)
	{
		avformat_close_input(&format_ctx);
	}

	return hr;
}
}