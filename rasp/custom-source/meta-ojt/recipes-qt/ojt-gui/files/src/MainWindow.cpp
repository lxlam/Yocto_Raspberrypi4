#include "MainWindow.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

/* ------------------------------------------------------------------ */
/* VideoWidget                                                         */
/* ------------------------------------------------------------------ */
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);        // own X11 window -> has its own winId()
    setAttribute(Qt::WA_PaintOnScreen);       // Qt does not paint it ...
    setAttribute(Qt::WA_NoSystemBackground);  // ... not even the background
    setMinimumSize(320, 180);
}

/* ------------------------------------------------------------------ */
/* MainWindow                                                          */
/* ------------------------------------------------------------------ */
MainWindow::MainWindow(PlayerClient* client, QWidget* parent)
    : QMainWindow(parent), client_(client)
{
    buildUi();

    // data coming from D-Bus -> update the screen
    connect(client_, &PlayerClient::mediaManagerAvailable, this, [this](bool running) {
        mmRunning_ = running;
        updateView();
    });
    connect(client_, &PlayerClient::playerAvailable, this, [this](bool running) {
        bool restarted = running && !playerRunning_;
        playerRunning_ = running;
        // video-player (re)started -> tell it again where to draw
        if (restarted)
            client_->setVideoWindow(static_cast<quint64>(video_->winId()));
        updateView();
    });
    connect(client_, &PlayerClient::mediaStateChanged, this, [this](const QString& state) {
        mediaState_ = state;
        updateView();
    });
    connect(client_, &PlayerClient::playlistChanged, this, &MainWindow::onPlaylistChanged);
    connect(client_, &PlayerClient::statusChanged, this, &MainWindow::onStatusChanged);
    connect(client_, &PlayerClient::positionChanged, this, &MainWindow::onPositionChanged);

    // buttons -> commands
    connect(prevButton_,      &QPushButton::clicked, client_, &PlayerClient::previous);
    connect(nextButton_,      &QPushButton::clicked, client_, &PlayerClient::next);
    connect(stopButton_,      &QPushButton::clicked, client_, &PlayerClient::stop);
    connect(backButton_,      &QPushButton::clicked, this, [this] { client_->seek(-10000); });
    connect(forwardButton_,   &QPushButton::clicked, this, [this] { client_->seek(10000); });
    connect(playPauseButton_, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    connect(playlist_, &QListWidget::itemActivated, this, &MainWindow::onTrackActivated);  // double click / Enter

    // keyboard (USB keyboard on the Pi)
    addShortcut(Qt::Key_Space, [this] { onPlayPauseClicked(); });
    addShortcut(Qt::Key_N,     [this] { client_->next(); });
    addShortcut(Qt::Key_P,     [this] { client_->previous(); });
    addShortcut(Qt::Key_S,     [this] { client_->stop(); });
    addShortcut(Qt::Key_Left,  [this] { client_->seek(-10000); });
    addShortcut(Qt::Key_Right, [this] { client_->seek(10000); });

    // ask video-player for position every 500 ms
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, client_, &PlayerClient::pollStatus);

    updateView();
}

void MainWindow::start()
{
    client_->refreshAll();       // -> playerAvailable(true) -> setVideoWindow(winId)
    statusTimer_->start();
}

void MainWindow::buildUi()
{
    setWindowTitle("OJT USB Video Player");
    setStyleSheet(
        "QMainWindow, QWidget { background: #15181d; color: #e8eaed; }"
        "QLabel#message { background: #000; color: #c9ced6; font-size: 22px; }"
        "QLabel#title { font-size: 18px; font-weight: bold; padding: 4px; }"
        "QListWidget { background: #1f232a; border: 1px solid #333; font-size: 17px; }"
        "QListWidget::item { padding: 10px; }"
        "QListWidget::item:selected { background: #2f6fd6; color: white; }"
        "QPushButton { background: #2a2f38; border: 1px solid #3a404b; border-radius: 6px;"
        "              min-height: 52px; min-width: 90px; font-size: 17px; }"
        "QPushButton:pressed { background: #2f6fd6; }"
        "QPushButton:disabled { color: #666; }"
        "QProgressBar { background: #2a2f38; border: none; height: 8px; border-radius: 4px; }"
        "QProgressBar::chunk { background: #2f6fd6; border-radius: 4px; }");

    // --- left: video area (message page / video page) ---
    messageLabel_ = new QLabel;
    messageLabel_->setObjectName("message");
    messageLabel_->setAlignment(Qt::AlignCenter);
    messageLabel_->setWordWrap(true);

    video_ = new VideoWidget;

    videoStack_ = new QStackedWidget;
    videoStack_->addWidget(messageLabel_);   // page 0
    videoStack_->addWidget(video_);          // page 1

    // --- right: playlist ---
    playlistTitle_ = new QLabel;
    playlistTitle_->setObjectName("title");
    playlist_ = new QListWidget;
    playlist_->setFocusPolicy(Qt::StrongFocus);

    auto* right = new QVBoxLayout;
    right->addWidget(playlistTitle_);
    right->addWidget(playlist_);
    auto* rightBox = new QWidget;
    rightBox->setLayout(right);
    rightBox->setFixedWidth(320);

    auto* top = new QHBoxLayout;
    top->addWidget(videoStack_, 1);
    top->addWidget(rightBox);

    // --- bottom: progress + buttons ---
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setRange(0, 1000);
    timeLabel_ = new QLabel("00:00 / 00:00");
    timeLabel_->setMinimumWidth(130);
    timeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* progressRow = new QHBoxLayout;
    progressRow->addWidget(progress_, 1);
    progressRow->addWidget(timeLabel_);

    prevButton_      = new QPushButton("<< Prev");
    backButton_      = new QPushButton("-10s");
    playPauseButton_ = new QPushButton("Play");
    forwardButton_   = new QPushButton("+10s");
    nextButton_      = new QPushButton("Next >>");
    stopButton_      = new QPushButton("Stop");
    playPauseButton_->setMinimumWidth(140);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    for (QPushButton* b : {prevButton_, backButton_, playPauseButton_, forwardButton_,
                           nextButton_, stopButton_}) {
        b->setFocusPolicy(Qt::NoFocus);   // keyboard keys go to the window
        buttons->addWidget(b);
    }
    buttons->addStretch();

    auto* root = new QVBoxLayout;
    root->addLayout(top, 1);
    root->addLayout(progressRow);
    root->addLayout(buttons);

    auto* central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);

    statusLabel_ = new QLabel;
    statusBar()->addPermanentWidget(statusLabel_);
}

/* ------------------------------------------------------------------ */
/* Data from D-Bus                                                     */
/* ------------------------------------------------------------------ */
void MainWindow::onPlaylistChanged(const QVector<Track>& tracks)
{
    if (tracks.isEmpty() && hadPlaylist_)
        statusBar()->showMessage(QString::fromUtf8("USB đã được rút ra / playlist đã bị xoá"), 5000);
    else if (!tracks.isEmpty())
        statusBar()->showMessage(QString::fromUtf8("Đã quét xong USB: %1 video").arg(tracks.size()), 5000);
    hadPlaylist_ = !tracks.isEmpty();

    tracks_ = tracks;
    playlist_->clear();
    for (const Track& t : tracks_) {
        auto* item = new QListWidgetItem(QString("%1. %2").arg(t.id + 1).arg(t.title));
        item->setToolTip(t.path);
        playlist_->addItem(item);
    }
    updateView();
}

void MainWindow::onStatusChanged(const QString& state, int index, const QString& title)
{
    Q_UNUSED(title);
    if (state == playerState_ && index == currentIndex_)
        return;
    playerState_ = state;
    currentIndex_ = index;
    updateView();
}

void MainWindow::onPositionChanged(qint64 positionMs, qint64 durationMs)
{
    progress_->setValue(durationMs > 0 ? int(positionMs * 1000 / durationMs) : 0);
    timeLabel_->setText(formatTime(positionMs) + " / " + formatTime(durationMs));
}

/* ------------------------------------------------------------------ */
/* One place that decides what the screen shows                        */
/* ------------------------------------------------------------------ */
void MainWindow::updateView()
{
    const bool hasTracks = !tracks_.isEmpty();
    const bool videoOn = playerRunning_ && hasTracks &&
                         (playerState_ == "PLAYING" || playerState_ == "PAUSED");

    // 1. message or video
    QString msg;
    if (!mmRunning_)
        msg = QString::fromUtf8("Media manager chưa chạy\nKhông có playlist");
    else if (mediaState_ == "SCANNING")
        msg = QString::fromUtf8("Đang quét USB...");
    else if (mediaState_ == "NO_MEDIA" || (!hasTracks && mediaState_ != "READY"))
        msg = QString::fromUtf8("Không có playlist\nHãy cắm USB có file .mp4 / .mkv");
    else if (!hasTracks)
        msg = QString::fromUtf8("USB không có file video (.mp4 / .mkv)");
    else if (!playerRunning_)
        msg = QString::fromUtf8("Video player chưa chạy");
    else if (playerState_ == "ERROR")
        msg = QString::fromUtf8("Không phát được video này\nHãy chọn bài khác hoặc nhấn Play");
    else
        msg = QString::fromUtf8("Chọn một video trong playlist\nhoặc nhấn Play");

    messageLabel_->setText(msg);
    videoStack_->setCurrentIndex(videoOn ? 1 : 0);

    // 2. playlist title + highlight current track
    playlistTitle_->setText(hasTracks ? QString("Playlist (%1)").arg(tracks_.size())
                                      : QString("Playlist"));
    if (hasTracks && currentIndex_ >= 0 && currentIndex_ < playlist_->count()) {
        for (int i = 0; i < playlist_->count(); ++i) {
            QFont f = playlist_->item(i)->font();
            f.setBold(i == currentIndex_);
            playlist_->item(i)->setFont(f);
        }
        playlist_->setCurrentRow(currentIndex_);
    }

    // 3. buttons
    const bool canControl = playerRunning_ && hasTracks;
    for (QPushButton* b : {prevButton_, nextButton_, playPauseButton_, stopButton_})
        b->setEnabled(canControl);
    backButton_->setEnabled(videoOn);
    forwardButton_->setEnabled(videoOn);

    if (playerState_ == "PLAYING")      playPauseButton_->setText("Pause");
    else if (playerState_ == "PAUSED")  playPauseButton_->setText("Resume");
    else                                playPauseButton_->setText("Play");

    if (!videoOn)
        onPositionChanged(0, 0);

    // 4. status bar
    statusLabel_->setText(QString("USB: %1  |  Player: %2")
                              .arg(mmRunning_ ? mediaState_ : "-")
                              .arg(playerRunning_ ? playerState_ : "-"));
}

/* ------------------------------------------------------------------ */
/* User actions                                                        */
/* ------------------------------------------------------------------ */
void MainWindow::onPlayPauseClicked()
{
    if (playerState_ == "PLAYING")
        client_->pause();
    else if (playerState_ != "PAUSED" && playlist_->currentRow() >= 0 &&
             playlist_->currentRow() != currentIndex_)
        client_->playIndex(playlist_->currentRow());   // a different track is selected
    else
        client_->play();                               // play / resume
}

void MainWindow::onTrackActivated(QListWidgetItem* item)
{
    client_->playIndex(playlist_->row(item));
}

void MainWindow::addShortcut(int key, const std::function<void()>& action)
{
    // ApplicationShortcut: works even when the playlist has keyboard focus
    auto* sc = new QShortcut(QKeySequence(key), this);
    sc->setContext(Qt::ApplicationShortcut);
    connect(sc, &QShortcut::activated, this, action);
}

QString MainWindow::formatTime(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
