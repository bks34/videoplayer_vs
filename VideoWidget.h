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
        PLAYING,
        STOP,
        DECODING,
        DECODED
    };
    PlayerState state = DECODING;

    std::vector<QImage*> frames;
    int play_index = 0;

    QOpenGLShaderProgram program;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo;

    QOpenGLTexture* texture = NULL;
};

