#pragma once

#include <QtWidgets/QMainWindow>
#include <QAction>
#include <QToolBar>
#include <QFileDialog>
#include "VideoWidget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void createActions();
    void createToolBars();

public slots:
    void slot_openFile();
private:
    QString videoPath;
    VideoWidget* videoWidget = NULL;
    
    QAction* openFileAction;
    QAction* playAction;
    QAction* pauseAction;

    QToolBar* toolBar;
};