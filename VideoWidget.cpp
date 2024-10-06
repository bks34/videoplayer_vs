#include "VideoWidget.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

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

VideoWidget::VideoWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    startTimer(1000 / 60);
}

int VideoWidget::DeCode()
{
    unsigned char* buf;
    int hasVideo = 0;
    int ret, gotPicture;
    int VideoIndex = -1;
    const AVCodec* pCodec;
    AVPacket* pAVpkt;
    AVCodecContext* pAVctx;
    AVFrame* pAVframe, * pAVframeRGB;
    AVFormatContext* pFormatCtx;
    struct SwsContext* pSwsCtx;

    //����AVFormatContext
    pFormatCtx = avformat_alloc_context();

    //��ʼ��pFormatCtx
    if (avformat_open_input(&pFormatCtx, videoPath.toStdString().data(), NULL, NULL) != 0)
    {
        printf("avformat_open_input err.\n");
        return -1;
    }

    //��ȡ����Ƶ��������Ϣ
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        avformat_close_input(&pFormatCtx);
        printf("avformat_find_stream_info err.\n");
        return -2;
    }

    //�ҵ���Ƶ��������
    VideoIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    //���û����Ƶ��
    if (VideoIndex == -1)
    {
        hasVideo = 0;
        printf("There is no video streams.\n");
    }
    else
    {
        hasVideo = 1;
        printf("There is a video stream %d.\n", VideoIndex);
    }

    //��ȡ��Ƶ������
    pAVctx = avcodec_alloc_context3(NULL);;

    //���ҽ�����
    avcodec_parameters_to_context(pAVctx, pFormatCtx->streams[VideoIndex]->codecpar);
    pCodec = avcodec_find_decoder(pAVctx->codec_id);
    if (pCodec == NULL)
    {
        avcodec_free_context(&pAVctx);
        avformat_close_input(&pFormatCtx);
        printf("avcodec_find_decoder err.\n");
        return -4;
    }

    //��ʼ��pAVctx
    if (avcodec_open2(pAVctx, pCodec, NULL) < 0)
    {
        avcodec_free_context(&pAVctx);
        avformat_close_input(&pFormatCtx);
        printf("avcodec_open2 err.\n");
        return -5;
    }

    //��ʼ��pAVpkt
    pAVpkt = (AVPacket*)av_malloc(sizeof(AVPacket));

    //��ʼ������֡�ռ�
    pAVframe = av_frame_alloc();
    pAVframeRGB = av_frame_alloc();

    //����ͼ�����ݴ洢buf
    //av_image_get_buffer_sizeһ֡��С
    buf = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB32, pAVctx->width, pAVctx->height, 1));
    av_image_fill_arrays(pAVframeRGB->data, pAVframeRGB->linesize, buf, AV_PIX_FMT_RGB32, pAVctx->width, pAVctx->height, 1);

    //��ʼ��pSwsCtx
    pSwsCtx = sws_getContext(pAVctx->width, pAVctx->height, pAVctx->pix_fmt, pAVctx->width, pAVctx->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
    
    width = pAVctx->width;
    height = pAVctx->height;

    //ѭ����ȡ��Ƶ����
	while (av_read_frame(pFormatCtx, pAVpkt) >= 0)//��ȡһ֡δ���������
	{
        //���״̬����ͣ
        while (decoder_state == DecoderState::PAUSED)
            continue;

		//�������Ƶ����
		if (pAVpkt->stream_index == VideoIndex)
		{
			//����һ֡��Ƶ����
			ret = avcodec_send_packet(pAVctx, pAVpkt);
			gotPicture = avcodec_receive_frame(pAVctx, pAVframe);

			if (ret < 0)
			{
                printf("Decode Error.\n");
				continue;
			}

			if (gotPicture == 0)
			{
				sws_scale(pSwsCtx, (const unsigned char* const*)pAVframe->data, pAVframe->linesize, 0, pAVctx->height, pAVframeRGB->data, pAVframeRGB->linesize);
                uchar* tmp = new uchar[av_image_get_buffer_size(AV_PIX_FMT_RGB32, pAVctx->width, pAVctx->height, 1)];
                memcpy(tmp, pAVframeRGB->data[0], av_image_get_buffer_size(AV_PIX_FMT_RGB32, pAVctx->width, pAVctx->height, 1));
                QImage* img = new QImage(tmp, pAVctx->width, pAVctx->height, QImage::Format_RGB32);

                int decode_index = 0;
				mutex.lock();
                frames.push_back(img);
                decode_index = frames.size();
				mutex.unlock();
                if(decode_index - play_index >= 3)
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
			}
		}
		av_packet_unref(pAVpkt);
	}
    //�ͷ���Դ
    sws_freeContext(pSwsCtx);
    av_frame_free(&pAVframeRGB);
    av_frame_free(&pAVframe);
    avcodec_free_context(&pAVctx);
    avformat_close_input(&pFormatCtx);

    //����Ƶ�ѽ������
    decoder_state = DecoderState::DECODED;

    return -1;
}

void VideoWidget::Play()
{
    player_state = PlayerState::PLAYING;
    if (decoder_state == DecoderState::PAUSED)
        decoder_state = DecoderState::DECODING;
    if (decoder_state == DecoderState::DECODED)
    {
        mutex.lock();
        if (frames.empty())
        {
            decoder_state = DecoderState::DECODING;
            t_decode = std::thread(&VideoWidget::DeCode, this);
            t_decode.detach();
        }
        mutex.unlock();
    }
}

void VideoWidget::Pause()
{
    if(player_state == PlayerState::PLAYING)
        player_state = PlayerState::PAUSE;
    if(decoder_state == DecoderState::DECODING)
        decoder_state = DecoderState::PAUSED;
}

void VideoWidget::paintGL()
{
    QImage* tmp = NULL;
    resize(width, height);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glClearColor(0.0, 0.0, 0.0, 1);
    vao.bind();
    program.bind();

    mutex.lock();
    if (!frames.empty() && play_index < frames.size())
    {
        tmp = frames[play_index++];
    }
    //������Ϻ����frames        
    //�Լ�texture(��ͬ��Ƶ��texture�ĳߴ��ʽ�Ȳ�ͬ)
    if (decoder_state ==DECODED && play_index >= frames.size())
    {
        play_index = 0;
        frames.clear();
        delete texture;
        texture = NULL;
    }
    mutex.unlock();


    if (tmp)
    {
        if (!texture)
            texture = new QOpenGLTexture(tmp->mirrored());
        else
            texture->setData(tmp->mirrored());
        texture->bind();
    }
    
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    //������һ֡���������Դ,�����ڴ�ռ�ù���
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
    glViewport(0, 0, w, h);
}

void VideoWidget::timerEvent(QTimerEvent* event)
{
    if(player_state == PlayerState::PLAYING)
        repaint();
}
