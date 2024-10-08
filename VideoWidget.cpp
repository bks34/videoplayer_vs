#include "VideoWidget.h"
#include <SDL.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
}

//如在调试,可取消该宏的定义
/*#define _VIDEOWIDGET_DEBUG*/ 

static const char vertShader[] = R"(#version 430
                                 in vec3 vPos;
                                 in vec2 vTexture;
                                 out vec2 oTexture;
                                 void main()
                                 {
                                     gl_Position = vec4(vPos, 1.0);
                                     oTexture = vTexture;
                                 })";

static const char fragShader[] = R"(#version 430
                                 in vec2 oTexture;
                                 uniform sampler2D uTexture;
                                 void main()
                                 {
                                     gl_FragColor = texture(uTexture, oTexture);
                                 })";

#define MAX_AUDIO_FRAME_SIZE 192000

static unsigned int audioLen = 0;
static uchar* audioChunk = NULL;
static uchar* audioPos = NULL;

static std::map<int, int> AUDIO_FORMAT_MAP = {
    // AV_SAMPLE_FMT_NONE = -1,
     {AV_SAMPLE_FMT_U8,  AUDIO_U8    },
     {AV_SAMPLE_FMT_S16, AUDIO_S16SYS},
     {AV_SAMPLE_FMT_S32, AUDIO_S32SYS},
     {AV_SAMPLE_FMT_FLT, AUDIO_F32SYS}
};

static void fillAudio(void* udata, uchar* stream, int len)
{
    SDL_memset(stream, 0, len);
    if (audioLen == 0)
        return;

    len = (len > audioLen ? audioLen : len);

    SDL_MixAudio(stream, audioPos, len, SDL_MIX_MAXVOLUME);

    audioPos += len;
    audioLen -= len;
}

VideoWidget::VideoWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    timerID = startTimer(1000 * fps_den / fps_num);
}

int VideoWidget::DeCode()
{
    unsigned char* buf = NULL;
    bool hasVideo = false, hasAudio = false;
    int ret, gotPicture, gotAudio;
    int VideoIndex = -1, AudioIndex = -1;
    const AVCodec* pCodec = NULL;
    const AVCodec* pAudioCodec = NULL;
    AVPacket* pAVpkt = NULL;
    AVCodecContext* pVideoAVctx = NULL, * pAudioAVctx = NULL;
    AVFrame* pAVframe = NULL, * pAVframeRGB = NULL;
    AVFormatContext* pFormatCtx = NULL;
    struct SwsContext* pSwsCtx = NULL;

    SwrContext* pSwrctx = NULL;
    AVSampleFormat outSampleFmt;     //声音格式
    int outSampleRate;              //采样率
    int outSampleNb;
    AVChannelLayout outChannelLayout;
    int outBufferSize;   //音频输出buff
    unsigned char* outBuff;

    //创建AVFormatContext
    pFormatCtx = avformat_alloc_context();

    //初始化pFormatCtx
    if (avformat_open_input(&pFormatCtx, videoPath.toStdString().data(), NULL, NULL) != 0)
    {
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("avformat_open_input err.\n");
#endif // _VIDEOWIDGET_DEBUG
        decoder_state = DECODER_DECODED;
        player_state = PLAYER_STOP;
        return -1;
    }

    //获取音视频流数据信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("avformat_find_stream_info err.\n");
#endif // _VIDEOWIDGET_DEBUG
        avformat_close_input(&pFormatCtx);
        decoder_state = DECODER_DECODED;
        player_state = PLAYER_STOP;
        return -1;
    }

    //找到视频流的索引
    VideoIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (VideoIndex >= 0)
    {
        hasVideo = true;
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("There is a video stream %d.\n", VideoIndex);
#endif // _VIDEOWIDGET_DEBUG
    }

    //找到音频流的索引
    AudioIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (AudioIndex >= 0)
    {
        hasAudio = true;
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("There is a audio stream %d.\n", AudioIndex);
#endif // _VIDEOWIDGET_DEBUG
    }
   
    if (!(hasAudio || hasVideo))
    {
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("Format not support!!!\n");
#endif // _VIDEOWIDGET_DEBUG
        decoder_state = DECODER_DECODED;
        player_state = PLAYER_STOP;
        return -1;
    }

    //处理视频流
    if (hasVideo)
    {
        //设置帧数率
        fps_den = pFormatCtx->streams[VideoIndex]->r_frame_rate.den;
        fps_num = pFormatCtx->streams[VideoIndex]->r_frame_rate.num;
        fpsChanged = true;

        //获取音视频流编码
        pVideoAVctx = avcodec_alloc_context3(NULL);
        //查找解码器
        avcodec_parameters_to_context(pVideoAVctx, pFormatCtx->streams[VideoIndex]->codecpar);
        pCodec = avcodec_find_decoder(pVideoAVctx->codec_id);
        if (pCodec == NULL)
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("avcodec_find_decoder err.\n");
#endif // _VIDEOWIDGET_DEBUG

            avcodec_free_context(&pVideoAVctx);
            avformat_close_input(&pFormatCtx);

            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }
        //打开解码器
        if (avcodec_open2(pVideoAVctx, pCodec, NULL) < 0)
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("avcodec_open2 err.\n");
#endif // _VIDEOWIDGET_DEBUG
            avcodec_free_context(&pVideoAVctx);
            avformat_close_input(&pFormatCtx);
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }

        pAVframeRGB = av_frame_alloc();

        //创建图像数据存储buf
        //av_image_get_buffer_size一帧大小
        buf = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB32, pVideoAVctx->width, pVideoAVctx->height, 1));
        av_image_fill_arrays(pAVframeRGB->data, pAVframeRGB->linesize, buf, AV_PIX_FMT_RGB32, pVideoAVctx->width, pVideoAVctx->height, 1);

        //初始化pSwsCtx
        pSwsCtx = sws_getContext(pVideoAVctx->width, pVideoAVctx->height, pVideoAVctx->pix_fmt, pVideoAVctx->width, pVideoAVctx->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

        width = pVideoAVctx->width;
        height = pVideoAVctx->height;
    }     

    if (hasAudio)
    {
        //获取音视频流编码
        pAudioAVctx = avcodec_alloc_context3(NULL);
        //查找解码器
        avcodec_parameters_to_context(pAudioAVctx, pFormatCtx->streams[AudioIndex]->codecpar);
        pAudioCodec = avcodec_find_decoder(pAudioAVctx->codec_id);
        if (pAudioCodec == NULL)
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("avcodec_find_decoder err.\n");
#endif // _VIDEOWIDGET_DEBUG
            avcodec_free_context(&pAudioAVctx);
            avformat_close_input(&pFormatCtx);
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }
        //打开解码器
        if (avcodec_open2(pAudioAVctx, pAudioCodec, NULL) < 0)
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("avcodec_open2 err.\n");
#endif // _VIDEOWIDGET_DEBUG
            avcodec_free_context(&pAudioAVctx);
            avformat_close_input(&pFormatCtx);
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }
        
        //重采样
        outSampleFmt = AV_SAMPLE_FMT_S16;                       //声音格式
        outSampleRate = pAudioAVctx->sample_rate;               //采样率
        outSampleNb = pAudioAVctx->frame_size;
        outChannelLayout = pAudioAVctx->ch_layout;
        //音频输出buff
        outBufferSize = av_samples_get_buffer_size(NULL, outChannelLayout.nb_channels, outSampleNb, outSampleFmt, 1);  
        outBuff = (unsigned char*)av_malloc(MAX_AUDIO_FRAME_SIZE * outChannelLayout.nb_channels);

        if (swr_alloc_set_opts2(&pSwrctx, &outChannelLayout, outSampleFmt, outSampleRate,
            &pAudioAVctx->ch_layout, pAudioAVctx->sample_fmt, pAudioAVctx->sample_rate, 0, NULL))
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("swr_alloc_set_opts2 error!\n");
#endif // _VIDEOWIDGET_DEBUG
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }
        swr_init(pSwrctx);

        
        //初始SDL
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("SDL_Init(SDL_INIT_AUDIO) error\n");
#endif // _VIDEOWIDGET_DEBUG
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }
#ifdef _VIDEOWIDGET_DEBUG
        qDebug("SDL_GetNumAudioDrivers(): %d\n Drivers that SDL can use:\n", SDL_GetNumAudioDrivers());
        for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
            qDebug("%d : %s\n", i, SDL_GetAudioDriver(i));
        qDebug("The driver using now: %s\n",SDL_GetCurrentAudioDriver());
#endif // _VIDEOWIDGET_DEBUG


        SDL_AudioSpec wantSpec;
        wantSpec.freq = outSampleRate;
        wantSpec.format = AUDIO_FORMAT_MAP[outSampleFmt];
        wantSpec.channels = outChannelLayout.nb_channels;
        wantSpec.silence = 0;
        wantSpec.samples = outSampleNb;
        wantSpec.callback = fillAudio;
        if (SDL_OpenAudio(&wantSpec, NULL) < 0) 
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("can not open SDL!\n");
#endif // _VIDEOWIDGET_DEBUG
            
            decoder_state = DECODER_DECODED;
            player_state = PLAYER_STOP;
            return -1;
        }

        SDL_PauseAudio(0);
    }  

    //初始化pAVpkt
    pAVpkt = (AVPacket*)av_malloc(sizeof(AVPacket));

    //初始化数据帧空间
    pAVframe = av_frame_alloc();
    
    //循环读取视频数据
	while (av_read_frame(pFormatCtx, pAVpkt) >= 0)//读取一帧未解码的数据
	{
        //如果状态是暂停
        while (decoder_state == DECODER_PAUSED)
            continue;

		//如果是视频数据
		if (pAVpkt->stream_index == VideoIndex)
		{
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("pAVpkt->stream_index == VideoIndex\n");
#endif // _VIDEOWIDGET_DEBUG
            
			//解码一帧视频数据
			ret = avcodec_send_packet(pVideoAVctx, pAVpkt);
			gotPicture = avcodec_receive_frame(pVideoAVctx, pAVframe);

			if (ret < 0)
			{
#ifdef _VIDEOWIDGET_DEBUG
                qDebug("Decode Error.\n");
#endif // _VIDEOWIDGET_DEBUG
                
				continue;
			}

			if (gotPicture == 0)
			{
				sws_scale(pSwsCtx, (const unsigned char* const*)pAVframe->data, pAVframe->linesize, 0, pVideoAVctx->height, pAVframeRGB->data, pAVframeRGB->linesize);
                uchar* tmp = new uchar[av_image_get_buffer_size(AV_PIX_FMT_RGB32, pVideoAVctx->width, pVideoAVctx->height, 1)];
                memcpy(tmp, pAVframeRGB->data[0], av_image_get_buffer_size(AV_PIX_FMT_RGB32, pVideoAVctx->width, pVideoAVctx->height, 1));
                QImage* img = new QImage(tmp, pVideoAVctx->width, pVideoAVctx->height, QImage::Format_RGB32);

                int decode_index = 0;
				mutex.lock();
                frames.push_back(img);
                decode_index = frames.size();
				mutex.unlock();
                if (decode_index - play_index >= 2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2 * 500 * fps_den / fps_num));
			}
		}

        if (pAVpkt->stream_index == AudioIndex)
        {
#ifdef _VIDEOWIDGET_DEBUG
            qDebug("pAVpkt->stream_index == AudioIndex\n");
#endif // _VIDEOWIDGET_DEBUG
            
            //解码一帧音频数据
            ret = avcodec_send_packet(pAudioAVctx, pAVpkt);
            gotAudio = avcodec_receive_frame(pAudioAVctx, pAVframe);

            if (ret < 0)
            {
#ifdef _VIDEOWIDGET_DEBUG
                qDebug("Decode Error.\n");
#endif // _VIDEOWIDGET_DEBUG           
                continue;
            }
            ret = swr_convert(pSwrctx, &outBuff, MAX_AUDIO_FRAME_SIZE, pAVframe->data, pAVframe->nb_samples);
            if (ret < 0)
            {
#ifdef _VIDEOWIDGET_DEBUG
                qDebug("Convert Error.\n");
#endif // _VIDEOWIDGET_DEBUG      
                continue;
            }
            if (gotAudio == 0)
            {
                while (audioLen > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                audioChunk = (unsigned char*)outBuff;
                audioPos = audioChunk;
                audioLen = outBufferSize;
            }
        }
		av_packet_unref(pAVpkt);
	}
    //释放资源
    sws_freeContext(pSwsCtx);
    av_frame_free(&pAVframeRGB);
    av_frame_free(&pAVframe);
    avcodec_free_context(&pVideoAVctx);
    avformat_close_input(&pFormatCtx);
    if (hasAudio)
    {
        swr_free(&pSwrctx);
        SDL_CloseAudio();
        SDL_Quit();
    }

    //已解码完毕
    decoder_state = DECODER_DECODED;

    return 0;
}

void VideoWidget::Play()
{
    player_state = PLAYER_PLAYING;
    if (decoder_state == DECODER_PAUSED)
        decoder_state = DECODER_DECODING;
    if (decoder_state == DECODER_DECODED)
    {
        mutex.lock();
        if (frames.empty())
        {
            decoder_state = DECODER_DECODING;
            t_decode = std::thread(&VideoWidget::DeCode, this);
            t_decode.detach();
        }
        mutex.unlock();
    }
}

void VideoWidget::Pause()
{
    if(player_state == PLAYER_PLAYING)
        player_state = PLAYER_PAUSED;
    if(decoder_state == DECODER_DECODING)
        decoder_state = DECODER_PAUSED;
}

void VideoWidget::paintGL()
{
    QImage* tmp = NULL;
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glClearColor(0.0, 0.0, 0.0, 1);
    vao.bind();
    program.bind();

    mutex.lock();
    if (!frames.empty() && play_index < frames.size())
    {
        tmp = frames[play_index++];
    }
    //播放完毕后清除frames        
    //以及texture(不同视频的texture的尺寸格式等不同)
    if (decoder_state == DECODER_DECODED && play_index >= frames.size())
    {
        play_index = 0;
        frames.clear();
        if (texture)
        {
            delete texture;
            texture = NULL;
        }
    }
    mutex.unlock();


    if (tmp)
    {
        if (!texture)
        {
            texture = new QOpenGLTexture(tmp->mirrored());
            texture->setMinificationFilter(QOpenGLTexture::Nearest);
            texture->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        else
        {
            texture->destroy();
            texture->setData(tmp->mirrored());
            texture->setMinificationFilter(QOpenGLTexture::Nearest);
            texture->setMagnificationFilter(QOpenGLTexture::Linear);
        }
        texture->bind();
    }
    
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    //播放完一帧后清除其资源,避免内存占用过多
    if (tmp)                                            
    {
        delete[] tmp->bits();
        delete tmp;
        tmp = NULL;
    }

    program.release();
    vao.release();
}

void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();
    program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertShader);
    program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader);
    program.link();

    vao.create();
    vbo.create();

    float vertex[] = {
        -1, -1, 0, 0, 0,
         1, -1, 0, 1, 0,
         1,  1, 0, 1, 1,
        -1,  1, 0, 0, 1
    };
    vao.bind();

    vbo.bind();
    vbo.allocate(vertex, sizeof(vertex));

    program.bind();

    program.setAttributeBuffer("vPos", GL_FLOAT, 0 * sizeof(float), 3, 5 * sizeof(float));
    program.enableAttributeArray("vPos");

    program.setAttributeBuffer("vTexture", GL_FLOAT, 3 * sizeof(float), 2, 5 * sizeof(float));
    program.enableAttributeArray("vTexture");

    program.release();
    vao.release();
}

void VideoWidget::resizeGL(int w, int h)
{
    if (width < w && height < h)
        resize(width, height);
    else
    {
        double r = (double)width / height;
        if (r > (double)w / h)
            resize(w, w / r);
        else
            resize(h * r, h);
    }
    glViewport(0, 0, w, h);
}

void VideoWidget::timerEvent(QTimerEvent* event)
{
    if (player_state == PLAYER_PLAYING)
        update();
    if (fpsChanged)
    {
        killTimer(timerID);
        timerID = startTimer(1000 * fps_den / fps_num);
        fpsChanged = false;
    }
}
