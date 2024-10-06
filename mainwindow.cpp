#include "mainwindow.h"

MainWindow::MainWindow(QWidget* parent) :QMainWindow(parent)
{
    resize(800, 600);
    setWindowTitle(tr("Video Player"));
    videoWidget = new VideoWidget;
    setCentralWidget(videoWidget);

    createActions();
    createToolBars();
}

MainWindow::~MainWindow()
{
}

void MainWindow::createActions()
{
    openFileAction = new QAction(tr("Open File"), this);
    openFileAction->setShortcut(tr("Ctrl+O"));
    connect(openFileAction, SIGNAL(triggered()), this, SLOT(slot_openFile()));

    playAction = new QAction(tr("Play"), this);
    playAction->setShortcut(tr("Ctrl+P"));
    connect(playAction, SIGNAL(triggered()), videoWidget, SLOT(Play()));

    pauseAction = new QAction(tr("Pause"), this);
    pauseAction->setShortcut(tr("Ctrl+Space"));
    connect(pauseAction, SIGNAL(triggered()), videoWidget, SLOT(Pause()));
}

void MainWindow::createToolBars()
{
    toolBar = addToolBar("tool");
    toolBar->setMovable(true);
    toolBar->addAction(openFileAction);
    toolBar->addAction(playAction);
    toolBar->addAction(pauseAction);
}

void MainWindow::slot_openFile()
{
    videoPath = QFileDialog::getOpenFileName(this, tr("Select the file"), tr("."));
    videoWidget->videoPath = videoPath;
}
