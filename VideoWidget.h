#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QMatrix>

#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <map>

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);

    int DeCode();
    QString videoPath;
    int width = 800;
    int height = 600;

public slots:
    void Play();
    void Pause();
    
protected:
    void paintGL() override;
    void initializeGL() override;
    void resizeGL(int w, int h) override;

    void timerEvent(QTimerEvent* event) override;

signals:

private: 
    std::thread t_decode;
    std::mutex mutex;

    enum PlayerState {
        PLAYER_PLAYING,
        PLAYER_STOP,
        PLAYER_PAUSED
    };

    enum DecoderState {
        DECODER_DECODING,
        DECODER_DECODED,
        DECODER_PAUSED
    };
    PlayerState player_state = PLAYER_STOP;        //²¥·ÅÆ÷µÄ×´Ì¬
    DecoderState decoder_state = DECODER_DECODED;   //½âÂëÆ÷µÄ×´Ì¬

    int fps_den = 1, fps_num = 60;
    int timerID;
    bool fpsChanged = false;

    std::vector<QImage*> frames;
    int play_index = 0;

    QOpenGLShaderProgram program;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo;

    QOpenGLTexture* texture = NULL;
};

