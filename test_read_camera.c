//
// Created by Admin on 2025/1/12.
//
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"

//Output YUV420P
#define OUTPUT_YUV420P 0
//'1' Use Dshow
//'0' Use VFW
#define USE_DSHOW 1

//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;

int sfp_refresh_thread(void *opaque) {
    thread_exit = 0;
    while (!thread_exit) {
        SDL_Event event;
        event.type = SFM_REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }
    thread_exit = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}


//Show Dshow Device
void show_dshow_device() {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVDictionary *options = NULL;
    av_dict_set(&options, "list_devices", "true", 0);
    AVInputFormat *iformat = av_find_input_format("dshow");
    printf("========Device Info=============\n");
    avformat_open_input(&pFormatCtx, "video=dummy", iformat, &options);
    printf("================================\n");
}

//Show Dshow Device Option
void show_dshow_device_option() {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVDictionary *options = NULL;
    av_dict_set(&options, "list_options", "true", 0);
    AVInputFormat *iformat = av_find_input_format("dshow");
    printf("========Device Option Info======\n");
    avformat_open_input(&pFormatCtx, "video=Integrated Camera", iformat, &options);
    printf("================================\n");
}

//Show VFW Device
void show_vfw_device() {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVInputFormat *iformat = av_find_input_format("vfwcap");
    printf("========VFW Device Info======\n");
    avformat_open_input(&pFormatCtx, "list", iformat, NULL);
    printf("=============================\n");
}

//Show AVFoundation Device
void show_avfoundation_device() {
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVDictionary *options = NULL;
    av_dict_set(&options, "list_devices", "true", 0);
    AVInputFormat *iformat = av_find_input_format("avfoundation");
    printf("==AVFoundation Device Info===\n");
    avformat_open_input(&pFormatCtx, "", iformat, &options);
    printf("=============================\n");
}

#undef main
int main(int argc, char *argv[]) {
    AVFormatContext *pFormatCtx;
    int videoindex;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    int ret, got_picture;

    AVFrame *pFrame, *pFrameYUV;
    int  buf_size;
    uint8_t *out_buffer;
    AVPacket *packet;

    //------------SDL----------------
    int screen_w, screen_h;
    SDL_Window *screen;
    SDL_Renderer *sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;
    SDL_Event event;

    struct SwsContext *img_convert_ctx;

    // AVFormatContext holds the header information from the format (Container)
    // Allocating memory for this component
    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    pFormatCtx = avformat_alloc_context();

    //Register Device
    avdevice_register_all();

    //Windows
#ifdef _WIN32

    //Show Dshow Device
    show_dshow_device();
    //Show Device Options
    show_dshow_device_option();
    //Show VFW Options
    show_vfw_device();

#if USE_DSHOW
    AVInputFormat *ifmt = av_find_input_format("dshow");

    AVDictionary *options = NULL;

    av_dict_set_int(&options, "rtbufsize", 104857600, 0);

    //Set own video device's name
    int result_device = avformat_open_input(&pFormatCtx, "video=HIK 1080P Camera", ifmt, &options);
    printf("Result of open device:%d\n", result_device);
    if (result_device != 0) {
        printf("Couldn't open input stream.\n");
        return -1;
    }
#else
    AVInputFormat *ifmt=av_find_input_format("vfwcap");
    if(avformat_open_input(&pFormatCtx,"0",ifmt,NULL)!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
#endif
#elif defined linux
    //Linux
    AVInputFormat *ifmt=av_find_input_format("video4linux2");
    if(avformat_open_input(&pFormatCtx,"/dev/video0",ifmt,NULL)!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
#else
    show_avfoundation_device();
    //Mac
    AVInputFormat *ifmt=av_find_input_format("avfoundation");
    //Avfoundation
    //[video]:[audio]
    if(avformat_open_input(&pFormatCtx,"0",ifmt,NULL)!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
#endif

    videoindex = -1;

    AVCodecParameters *pCodecParameters = NULL;

    printf("finding stream info from format.\n");

    // loop though all the streams and print its main information
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = pFormatCtx->streams[i]->codecpar;
        printf("AVStream->time_base before open coded %d/%d\n", pFormatCtx->streams[i]->time_base.num,
               pFormatCtx->streams[i]->time_base.den);
        printf("AVStream->r_frame_rate before open coded %d/%d\n", pFormatCtx->streams[i]->r_frame_rate.num,
               pFormatCtx->streams[i]->r_frame_rate.den);
        //printf("AVStream->start_time %lld\n" PRId64, pFormatCtx->streams[i]->start_time);
        //printf("AVStream->duration %lld\n" PRId64, pFormatCtx->streams[i]->duration);

        printf("finding the proper decoder (CODEC)\n");

        AVCodec *pLocalCodec = NULL;

        // finds the registered decoder for a codec ID
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL) {
            printf("ERROR unsupported codec!");
            // In this example if the codec is not found we just skip it
            continue;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (videoindex == -1) {
                videoindex = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }

            printf("Video Codec: resolution %d x %d\n", pLocalCodecParameters->width, pLocalCodecParameters->height);
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Audio Codec: %d channels, sample rate %d\n", pLocalCodecParameters->channels,
                   pLocalCodecParameters->sample_rate);
        }

        // print its name, id and bitrate
        printf("\tCodec %s ID %d bit_rate %lld\n", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if (videoindex == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);

    if (!pCodecCtx) {
        printf("failed to allocated memory for AVCodecContext\n");
        return -1;
    }

    // 使用视频流的codecpar为解码器上下文赋值
    ret = avcodec_parameters_to_context(pCodecCtx, pCodecParameters);
    if (ret < 0) {
        printf("failed to open codec through avcodec_parameters_to_context\n");
        return -1;
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("failed to open codec through avcodec_open2\n");
        return -1;
    }

    /*
    * 在此处添加输出视频信息的代码
    * 取自于pFormatCtx，使用fprintf()
    */
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    // 为AVFrame.*data[]手工分配缓冲区，用于存储sws_scale()中目的帧视频数据
    //     pFrame的data_buffer由av_read_frame()分配，因此不需手工分配
    //     pFrameYUV的data_buffer无处分配，因此在此处手工分配
    buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                        pCodecCtx->width,
                                        pCodecCtx->height,
                                        1
    );
    // buffer将作为p_frm_yuv的视频数据缓冲区
    out_buffer = (uint8_t *)av_malloc(buf_size);
    // 使用给定参数设定pFrameYUV->data和pFrameYUV->linesize
    av_image_fill_arrays(pFrameYUV->data,           // dst data[]
                         pFrameYUV->linesize,       // dst linesize[]
                         out_buffer,                    // src buffer
                         AV_PIX_FMT_YUV420P,        // pixel format
                         pCodecCtx->width,        // width
                         pCodecCtx->height,       // height
                         1                          // align
    );
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    //SDL 2.0 Support for multiple windows
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              screen_w, screen_h, SDL_WINDOW_OPENGL);

    if (!screen) {
        printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
        return -1;
    }

    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width,
                                   pCodecCtx->height);

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;

    packet = (AVPacket *) av_malloc(sizeof(AVPacket));

    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
    //------------SDL End------------
    //Event Loop
    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
            //------------------------------
            if (av_read_frame(pFormatCtx, packet) >= 0) {

                if (packet->stream_index == videoindex) {

                    // Supply raw packet data as input to a decoder
                    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
                    ret = avcodec_send_packet(pCodecCtx, packet);
                    if (ret < 0) {
                        printf("Error while sending a packet to the decoder: %s\n", av_err2str(ret));
                        return ret;
                    }

                    while (ret >= 0) {
                        // Return decoded output data (into a frame) from a decoder
                        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
                        ret = avcodec_receive_frame(pCodecCtx, pFrame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                            break;
                        } else if (ret < 0) {
                            printf("Error while receiving a frame from the decoder: %s \n", av_err2str(ret));
                            return ret;
                        }

                        /*printf(
                                "Frame %d (type=%c, size=%d bytes, format=%d) pts %lld key_frame %d [DTS %d]",
                                pCodecCtx->frame_number,
                                av_get_picture_type_char(pFrame->pict_type),
                                pFrame->pkt_size,
                                pFrame->format,
                                pFrame->pts,
                                pFrame->key_frame,
                                pFrame->coded_picture_number
                        );*/

                        sws_scale(img_convert_ctx, (const uint8_t *const *) pFrame->data, pFrame->linesize, 0,
                                  pCodecCtx->height,
                                  pFrameYUV->data, pFrameYUV->linesize);

                        //SDL---------------------------
                        SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                        SDL_RenderClear(sdlRenderer);
                        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent(sdlRenderer);
                        //SDL End-----------------------

                    }

                    av_packet_unref(packet);
                    av_frame_unref(pFrame);
                }
            } else {
                //Exit Thread
                thread_exit = 1;
            }
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if (event.type == SFM_BREAK_EVENT) {
            break;
        }

    }

    sws_freeContext(img_convert_ctx);
    SDL_Quit();
    //--------------
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}
