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

}

void MainWindow::createToolBars()
{
    toolBar = addToolBar("tool");
    toolBar->addAction(openFileAction);
    toolBar->addAction(playAction);
}

void MainWindow::slot_openFile()
{
    videoPath = QFileDialog::getOpenFileName(this, tr("Select the file"), tr("."));
    videoWidget->videoPath = videoPath;
}
