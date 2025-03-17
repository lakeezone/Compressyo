#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
}

void check(int ret, const char* msg) {
    if (ret < 0) {
        fprintf(stderr, "%s: %s\n", msg, av_err2str(ret));
        exit(1);
    }
}

double get_video_duration(const char* input_file) {
    AVFormatContext* fmt_ctx = NULL;
    int ret = avformat_open_input(&fmt_ctx, input_file, NULL, NULL);
    check(ret, "Erro ao abrir arquivo");
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    check(ret, "Erro ao obter info do stream");

    double duration = (double)fmt_ctx->duration / AV_TIME_BASE;
    avformat_close_input(&fmt_ctx);
    return duration;
}

void compress_video(const char* input_file, const char* output_file, double target_size_mb) {
    AVFormatContext* in_fmt_ctx = NULL;
    AVFormatContext* out_fmt_ctx = NULL;
    AVCodecContext* codec_ctx = NULL;
    AVPacket* pkt = NULL;

    // Abrir arquivo de entrada
    int ret = avformat_open_input(&in_fmt_ctx, input_file, NULL, NULL);
    check(ret, "Erro ao abrir entrada");
    ret = avformat_find_stream_info(in_fmt_ctx, NULL);
    check(ret, "Erro ao obter info do stream");

    // Criar contexto de saída
    ret = avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, output_file);
    check(ret, "Erro ao criar saída");

    // Encontrar stream de vídeo
    int video_stream_idx = -1;
    for (unsigned i = 0; i < in_fmt_ctx->nb_streams; i++) {
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == -1) {
        fprintf(stderr, "Stream de vídeo não encontrado\n");
        goto cleanup;
    }

    // Configurar codec H.265
    const AVCodec* codec = avcodec_find_encoder_by_name("libx265");
    if (!codec) {
        fprintf(stderr, "Codec H.265 não encontrado\n");
        goto cleanup;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Erro ao alocar contexto de codec\n");
        goto cleanup;
    }

    AVStream* in_stream = in_fmt_ctx->streams[video_stream_idx];
    AVStream* out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "Erro ao criar stream de saída\n");
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(codec_ctx, in_stream->codecpar);
    check(ret, "Erro ao configurar contexto");
    codec_ctx->bit_rate = (int64_t)((target_size_mb * 8 * 1024 * 1024 - (128 * 1000 * get_video_duration(input_file))) / get_video_duration(input_file));
    codec_ctx->width = in_stream->codecpar->width;
    codec_ctx->height = in_stream->codecpar->height;
    codec_ctx->time_base = in_stream->time_base;
    codec_ctx->framerate = in_stream->r_frame_rate;

    ret = avcodec_open2(codec_ctx, codec, NULL);
    check(ret, "Erro ao abrir codec");

    ret = avcodec_parameters_from_context(out_stream->codecpar, codec_ctx);
    check(ret, "Erro ao copiar parâmetros");

    // Abrir arquivo de saída
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt_ctx->pb, output_file, AVIO_FLAG_WRITE);
        check(ret, "Erro ao abrir arquivo de saída");
    }

    ret = avformat_write_header(out_fmt_ctx, NULL);
    check(ret, "Erro ao escrever header");

    // Copiar e comprimir pacotes
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Erro ao alocar pacote\n");
        goto cleanup;
    }

    while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            pkt->stream_index = 0;

            ret = av_interleaved_write_frame(out_fmt_ctx, pkt);
            check(ret, "Erro ao escrever frame");
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt_ctx);
    printf("Vídeo comprimido salvo em: %s\n", output_file);

cleanup:
    if (pkt) av_packet_free(&pkt);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (out_fmt_ctx && !(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt_ctx->pb);
    if (out_fmt_ctx) avformat_free_context(out_fmt_ctx);
    if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);
}

int main() {
    char input_file[256], output_file[256];
    double target_size_mb;

    printf("Digite o caminho do vídeo de entrada: ");
    fgets(input_file, sizeof(input_file), stdin);
    input_file[strcspn(input_file, "\n")] = 0; // Remove nova linha

    printf("Digite o caminho do vídeo de saída: ");
    fgets(output_file, sizeof(output_file), stdin);
    output_file[strcspn(output_file, "\n")] = 0;

    printf("Digite o tamanho desejado (em MB): ");
    scanf("%lf", &target_size_mb);

    compress_video(input_file, output_file, target_size_mb);
    return 0;
}