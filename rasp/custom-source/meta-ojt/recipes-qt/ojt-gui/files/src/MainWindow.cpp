#include "MainWindow.h"
#include <QApplication>
#include <QEvent>

#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
/* ------------------------------------------------------------------ */
/* Colors (YouTube dark theme)                                         */
/* ------------------------------------------------------------------ */
static const char* STYLE =
    "QMainWindow, QWidget { background: #0f0f0f; color: #f1f1f1; }"
    "QWidget#header { background: #0f0f0f; border-bottom: 1px solid #272727; }"
    "QLabel#logoText { font-size: 18px; font-weight: bold; letter-spacing: -0.5px; }"
    "QLabel#status { color: #aaaaaa; font-size: 13px; }"
    "QLabel#toast { background: #f1f1f1; color: #0f0f0f; border-radius: 12px;"
    "               padding: 3px 12px; font-size: 13px; }"
    "QLabel#message { background: #000; color: #aaaaaa; font-size: 20px; }"
    "QWidget#controls, QWidget#controls QLabel { background: #000; }"
    "QLabel#time { color: #f1f1f1; font-size: 13px; padding: 0 6px; }"
    "QLabel#videoTitle { font-size: 17px; font-weight: bold; }"
    "QLabel#videoSub { color: #aaaaaa; font-size: 13px; }"
    "QLabel#sideTitle { font-size: 16px; font-weight: bold; padding: 2px 4px 6px 4px; }"
    "QListWidget { background: #0f0f0f; border: none; font-size: 14px; outline: 0; }"
    "QListWidget::item { padding: 6px 4px; border-radius: 8px; color: #f1f1f1; }"
    "QListWidget::item:hover { background: #272727; }"
    "QListWidget::item:selected { background: #3f3f3f; color: #ffffff; }"
    "QPushButton { background: transparent; border: none; border-radius: 20px;"
    "              min-width: 40px; min-height: 40px; max-width: 40px; max-height: 40px; }"
    "QPushButton:hover { background: #3f3f3f; }"
    "QPushButton:pressed { background: #555555; }"
    "QSlider::groove:horizontal { height: 4px; background: #4d4d4d; }"
    "QSlider::sub-page:horizontal { background: #ff0000; }"
    "QSlider::add-page:horizontal { background: #4d4d4d; }"
    "QSlider::handle:horizontal { background: #ff0000; width: 14px; height: 14px;"
    "                             margin: -5px 0; border-radius: 7px; }"
    "QSlider::sub-page:horizontal:disabled { background: #4d4d4d; }"
    "QSlider::handle:horizontal:disabled { background: transparent; }"
    "QScrollBar:vertical { background: #0f0f0f; width: 8px; }"
    "QScrollBar::handle:vertical { background: #3f3f3f; border-radius: 4px; min-height: 30px; }"
    "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }";

/* ------------------------------------------------------------------ */
/* Icons: drawn with QPainter, so no image files are needed            */
/* ------------------------------------------------------------------ */
enum IconType { IconPlay, IconPause, IconPrev, IconNext, IconRewind, IconForward,
                IconStop, IconFullscreen, IconExitFullscreen };

static QIcon makeIcon(IconType type, const QColor& color = QColor("#ffffff"))
{
    QPixmap pm(64, 64);                 // drawn big, Qt scales it down nicely
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    auto triangle = [&p](QPointF a, QPointF b, QPointF c) {
        QPainterPath path;
        path.moveTo(a); path.lineTo(b); path.lineTo(c); path.closeSubpath();
        p.drawPath(path);
    };

    switch (type) {
    case IconPlay:     triangle({20, 12}, {52, 32}, {20, 52}); break;
    case IconPause:    p.drawRect(17, 14, 11, 36); p.drawRect(36, 14, 11, 36); break;
    case IconStop:     p.drawRoundedRect(16, 16, 32, 32, 3, 3); break;
    case IconPrev:     p.drawRect(14, 16, 6, 32); triangle({50, 16}, {22, 32}, {50, 48}); break;
    case IconNext:     p.drawRect(44, 16, 6, 32); triangle({14, 16}, {42, 32}, {14, 48}); break;
    case IconRewind:   triangle({32, 18}, {10, 32}, {32, 46}); triangle({54, 18}, {32, 32}, {54, 46}); break;
    case IconForward:  triangle({10, 18}, {32, 32}, {10, 46}); triangle({32, 18}, {54, 32}, {32, 46}); break;
    case IconFullscreen:
    case IconExitFullscreen: {
        // four corners: pointing out (enter) or pointing in (exit)
        QPen pen(color, 5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const bool in = (type == IconExitFullscreen);
        const int a = 14, b = 26, c = 38, d = 50;   // grid lines
        if (!in) {
            p.drawPolyline(QPolygon({QPoint(a, b), QPoint(a, a), QPoint(b, a)}));
            p.drawPolyline(QPolygon({QPoint(c, a), QPoint(d, a), QPoint(d, b)}));
            p.drawPolyline(QPolygon({QPoint(d, c), QPoint(d, d), QPoint(c, d)}));
            p.drawPolyline(QPolygon({QPoint(b, d), QPoint(a, d), QPoint(a, c)}));
        } else {
            p.drawPolyline(QPolygon({QPoint(a, b), QPoint(b, b), QPoint(b, a)}));
            p.drawPolyline(QPolygon({QPoint(c, a), QPoint(c, b), QPoint(d, b)}));
            p.drawPolyline(QPolygon({QPoint(d, c), QPoint(c, c), QPoint(c, d)}));
            p.drawPolyline(QPolygon({QPoint(b, d), QPoint(b, c), QPoint(a, c)}));
        }
        break;
    }
    }
    return QIcon(pm);
}

/* Small "thumbnail" for a playlist item: dark box with a play sign (red = now playing) */
static QIcon makeThumb(bool current)
{
    QPixmap pm(112, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(current ? QColor("#ff0000") : QColor("#303030"));
    p.drawRoundedRect(pm.rect(), 10, 10);
    p.setBrush(QColor("#ffffff"));
    QPainterPath path;
    path.moveTo(46, 18); path.lineTo(70, 32); path.lineTo(46, 46); path.closeSubpath();
    p.drawPath(path);
    QIcon icon(pm);
    icon.addPixmap(pm, QIcon::Selected);   // keep the same colors when the row is selected
    return icon;
}

/* Logo: red rounded box with a white triangle (like the YouTube logo shape) */
static QPixmap makeLogo()
{
    QPixmap pm(40, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ff0000"));
    p.drawRoundedRect(pm.rect(), 8, 8);
    p.setBrush(QColor("#ffffff"));
    QPainterPath path;
    path.moveTo(16, 8); path.lineTo(27, 14); path.lineTo(16, 20); path.closeSubpath();
    p.drawPath(path);
    return pm;
}

static QPushButton* makeButton(IconType icon, const QString& tip, int iconSize = 26)
{
    auto* b = new QPushButton;
    b->setIcon(makeIcon(icon));
    b->setIconSize(QSize(iconSize, iconSize));
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);   // keyboard keys go to the window
    b->setCursor(Qt::PointingHandCursor);
    return b;
}


/* ------------------------------------------------------------------ */
/* VideoWidget                                                         */
/* ------------------------------------------------------------------ */
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);        // own X11 window -> has its own winId()
    setAttribute(Qt::WA_PaintOnScreen);       // Qt does not paint it ...
    setAttribute(Qt::WA_NoSystemBackground);  // ... not even the background
    setMinimumSize(320, 180);
    setMouseTracking(true);                   // mouse move -> show controls in full screen
}

void VideoWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    emit resized();
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    Q_UNUSED(e);
    emit doubleClicked();
}

/* ------------------------------------------------------------------ */
/* SeekSlider                                                          */
/* ------------------------------------------------------------------ */
SeekSlider::SeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent)
{
    setRange(0, 1000);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(18);
}

int SeekSlider::valueAt(int x) const
{
    return QStyle::sliderValueFromPosition(minimum(), maximum(), x, width());
}

void SeekSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    setSliderDown(true);         // MainWindow stops moving the handle while we drag
    setValue(valueAt(e->pos().x()));
}

void SeekSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (isSliderDown())
        setValue(valueAt(e->pos().x()));
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !isSliderDown()) return;
    setValue(valueAt(e->pos().x()));
    setSliderDown(false);        // -> emits sliderReleased()
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
    connect(prevButton_,       &QPushButton::clicked, client_, &PlayerClient::previous);
    connect(nextButton_,       &QPushButton::clicked, client_, &PlayerClient::next);
    connect(stopButton_,       &QPushButton::clicked, client_, &PlayerClient::stop);
    connect(backButton_,       &QPushButton::clicked, this, [this] { client_->seek(-10000); });
    connect(forwardButton_,    &QPushButton::clicked, this, [this] { client_->seek(10000); });
    connect(playPauseButton_,  &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    connect(playlist_, &QListWidget::itemActivated, this, &MainWindow::onTrackActivated);  // double click / Enter
    // seek bar: while dragging show the target time, on release jump there
    connect(seek_, &QSlider::valueChanged, this, [this](int value) {
        if (seek_->isSliderDown())
            timeLabel_->setText(formatTime(durationMs_ * value / 1000) + " / " + formatTime(durationMs_));
    });
    connect(seek_, &QSlider::sliderReleased, this, &MainWindow::onSeekReleased);

    // video area: double click = full screen, resize = redraw video at the new size
    connect(video_, &VideoWidget::doubleClicked, this, &MainWindow::toggleFullscreen);
    connect(video_, &VideoWidget::resized, this, [this] { exposeTimer_->start(); });


    // keyboard (USB keyboard on the Pi)
    addShortcut(Qt::Key_Space,  [this] { onPlayPauseClicked(); });
    addShortcut(Qt::Key_N,      [this] { client_->next(); });
    addShortcut(Qt::Key_P,      [this] { client_->previous(); });
    addShortcut(Qt::Key_S,      [this] { client_->stop(); });
    addShortcut(Qt::Key_Left,   [this] { client_->seek(-10000); });
    addShortcut(Qt::Key_Right,  [this] { client_->seek(10000); });
    addShortcut(Qt::Key_F,      [this] { toggleFullscreen(); });
    addShortcut(Qt::Key_Escape, [this] { setFullscreen(false); });

    // ask video-player for position every 500 ms
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(500);
    connect(statusTimer_, &QTimer::timeout, client_, &PlayerClient::pollStatus);
    // full screen: hide the controls 3 s after the last mouse move / tap / key
    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(3000);
    connect(hideTimer_, &QTimer::timeout, this, &MainWindow::hideControls);

    toastTimer_ = new QTimer(this);
    toastTimer_->setSingleShot(true);
    toastTimer_->setInterval(4000);
    connect(toastTimer_, &QTimer::timeout, toastLabel_, &QLabel::hide);

    // after the video box changes size, send SetVideoWindow again:
    // video-player then re-reads the window size and redraws (see GStreamerBackend::setVideoWindow)
    exposeTimer_ = new QTimer(this);
    exposeTimer_->setSingleShot(true);
    exposeTimer_->setInterval(100);   // wait until resizing has finished
    connect(exposeTimer_, &QTimer::timeout, this, [this] {
        if (playerRunning_)
            client_->setVideoWindow(static_cast<quint64>(video_->winId()));
    });

    qApp->installEventFilter(this);   // see eventFilter(): any activity shows the controls

    updateView();
}

void MainWindow::start()
{
    client_->refreshAll();       // -> playerAvailable(true) -> setVideoWindow(winId)
    statusTimer_->start();
}
/* ------------------------------------------------------------------ */
/* Build the screen                                                    */
/*                                                                     */
/*  +------------------------------------------------+-------------+   */
/*  | header: logo "OJT Player"      toast   status  |             |   */
/*  +-----------------------------------+------------+-------------+   */
/*  |                                   |  Up next                 |   */
/*  |          video                    |  [>] video 1             |   */
/*  |                                   |  [>] video 2             |   */
/*  | ======o------------ (seek bar)    |  ...                     |   */
/*  | |< > >|  << >>  []  0:12 / 3:40 [ ]                          |   */
/*  | Title of the video                |                          |   */
/*  | USB drive - 5 videos              |                          |   */
/*  +-----------------------------------+--------------------------+   */
/* ------------------------------------------------------------------ */

void MainWindow::buildUi()
{
    setWindowTitle("OJT Player");
    setStyleSheet(STYLE);

    // --- video area (message page / video page) ---
    messageLabel_ = new QLabel;
    messageLabel_->setObjectName("message");
    messageLabel_->setAlignment(Qt::AlignCenter);
    messageLabel_->setWordWrap(true);
    messageLabel_->setMouseTracking(true);

    video_ = new VideoWidget;

    videoStack_ = new QStackedWidget;
    videoStack_->addWidget(messageLabel_);   // page 0
    videoStack_->addWidget(video_);          // page 1
    videoStack_->setMouseTracking(true);

    auto* left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(0);
    left->addWidget(videoStack_, 1);
    left->addWidget(buildControls());
    left->addWidget(buildInfo());

    bodyLayout_ = new QHBoxLayout;
    bodyLayout_->setContentsMargins(12, 10, 8, 8);
    bodyLayout_->setSpacing(12);
    bodyLayout_->addLayout(left, 1);
    bodyLayout_->addWidget(buildSidebar());

    auto* root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());
    root->addLayout(bodyLayout_, 1);

    auto* central = new QWidget;
    central->setLayout(root);
    central->setMouseTracking(true);
    setCentralWidget(central);
}

QWidget* MainWindow::buildHeader()
{
    auto* logo = new QLabel;
    logo->setPixmap(makeLogo());
    auto* logoText = new QLabel("OJT Player");
    logoText->setObjectName("logoText");

    toastLabel_ = new QLabel;
    toastLabel_->setObjectName("toast");
    toastLabel_->hide();

    statusLabel_ = new QLabel;
    statusLabel_->setObjectName("status");
    statusLabel_->setTextFormat(Qt::RichText);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(14, 0, 14, 0);
    row->setSpacing(8);
    row->addWidget(logo);
    row->addWidget(logoText);
    row->addStretch();
    row->addWidget(toastLabel_);
    row->addSpacing(12);
    row->addWidget(statusLabel_);

    header_ = new QWidget;
    header_->setObjectName("header");
    header_->setAttribute(Qt::WA_StyledBackground);
    header_->setFixedHeight(46);
    header_->setLayout(row);
    return header_;
}

QWidget* MainWindow::buildControls()
{
    seek_ = new SeekSlider;

    prevButton_       = makeButton(IconPrev,       "Previous (P)", 22);
    playPauseButton_  = makeButton(IconPlay,       "Play (Space)", 28);
    nextButton_       = makeButton(IconNext,       "Next (N)", 22);
    backButton_       = makeButton(IconRewind,     "Back 10 s (Left)", 24);
    forwardButton_    = makeButton(IconForward,    "Forward 10 s (Right)", 24);
    stopButton_       = makeButton(IconStop,       "Stop (S)", 20);
    fullscreenButton_ = makeButton(IconFullscreen, "Full screen (F)", 24);

    timeLabel_ = new QLabel("0:00 / 0:00");
    timeLabel_->setObjectName("time");

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(4, 0, 4, 2);
    buttons->setSpacing(2);
    for (QPushButton* b : {prevButton_, playPauseButton_, nextButton_,
                           backButton_, forwardButton_, stopButton_})
        buttons->addWidget(b);
    buttons->addWidget(timeLabel_);
    buttons->addStretch();
    buttons->addWidget(fullscreenButton_);

    auto* col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(seek_);
    col->addLayout(buttons);

    controls_ = new QWidget;
    controls_->setObjectName("controls");
    controls_->setAttribute(Qt::WA_StyledBackground);
    controls_->setLayout(col);
    return controls_;
}

QWidget* MainWindow::buildInfo()
{
    titleLabel_ = new QLabel;
    titleLabel_->setObjectName("videoTitle");
    subtitleLabel_ = new QLabel;
    subtitleLabel_->setObjectName("videoSub");

    auto* col = new QVBoxLayout;
    col->setContentsMargins(2, 8, 2, 0);
    col->setSpacing(2);
    col->addWidget(titleLabel_);
    col->addWidget(subtitleLabel_);

    info_ = new QWidget;
    info_->setLayout(col);
    return info_;
}

QWidget* MainWindow::buildSidebar()
{
    playlistTitle_ = new QLabel;
    playlistTitle_->setObjectName("sideTitle");

    playlist_ = new QListWidget;
    playlist_->setFocusPolicy(Qt::StrongFocus);
    playlist_->setIconSize(QSize(56, 32));
    playlist_->setTextElideMode(Qt::ElideRight);
    playlist_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    playlist_->setSpacing(2);

    auto* col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(playlistTitle_);
    col->addWidget(playlist_, 1);

    sidebar_ = new QWidget;
    sidebar_->setLayout(col);
    sidebar_->setFixedWidth(250);
    return sidebar_;
}

/* ------------------------------------------------------------------ */
/* Data from D-Bus                                                     */
/* ------------------------------------------------------------------ */
void MainWindow::onPlaylistChanged(const QVector<Track>& tracks)
{
    if (tracks.isEmpty() && hadPlaylist_)
        showToast("USB drive removed - playlist cleared");
    else if (!tracks.isEmpty())
        showToast(QString("USB scan complete: %1 video(s)").arg(tracks.size()));
    hadPlaylist_ = !tracks.isEmpty();

    tracks_ = tracks;
    playlist_->clear();
    for (const Track& t : tracks_) {
        auto* item = new QListWidgetItem(makeThumb(false), t.title);
        item->setToolTip(t.path);
        playlist_->addItem(item);
    }
    updateView();
}

void MainWindow::onStatusChanged(const QString& state, int index, const QString& title)
{
    if (state == playerState_ && index == currentIndex_ && title == currentTitle_)
        return;
    playerState_ = state;
    currentIndex_ = index;
    currentTitle_ = title;
    updateView();
}

void MainWindow::onPositionChanged(qint64 positionMs, qint64 durationMs)
{
    positionMs_ = positionMs;
    durationMs_ = durationMs;
    if (seek_->isSliderDown())
        return;                      // user is dragging: do not move the handle
    seek_->setValue(durationMs > 0 ? int(positionMs * 1000 / durationMs) : 0);
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
        msg = "Media manager is not running\nNo playlist";
    else if (mediaState_ == "SCANNING")
        msg = "Scanning USB drive...";
    else if (mediaState_ == "NO_MEDIA" || (!hasTracks && mediaState_ != "READY"))
        msg = "No playlist\nPlug in a USB drive with .mp4 / .mkv files";
    else if (!hasTracks)
        msg = "No video files (.mp4 / .mkv) on the USB drive";
    else if (!playerRunning_)
        msg = "Video player is not running";
    else if (playerState_ == "ERROR")
        msg = "Cannot play this video\nChoose another one or press Play";
    else
        msg = "Select a video from the playlist\nor press Play";

    messageLabel_->setText(msg);
    videoStack_->setCurrentIndex(videoOn ? 1 : 0);

    // 2. playlist title + mark the current track (red thumbnail, bold)
    playlistTitle_->setText(hasTracks ? QString("Up next  ·  %1").arg(tracks_.size())
                                      : QString("Up next"));
    for (int i = 0; i < playlist_->count(); ++i) {
        const bool current = (i == currentIndex_);
        QListWidgetItem* item = playlist_->item(i);
        QFont f = item->font();
        if (f.bold() != current) {           // only touch items that change
            f.setBold(current);
            item->setFont(f);
            item->setIcon(makeThumb(current));
        }
    }
    if (hasTracks && currentIndex_ >= 0 && currentIndex_ < playlist_->count())
        playlist_->setCurrentRow(currentIndex_);

    // 3. title under the video
    QString title;
    if (currentIndex_ >= 0 && currentIndex_ < tracks_.size())
        title = tracks_[currentIndex_].title;
    else if (!currentTitle_.isEmpty())
        title = currentTitle_;
    titleLabel_->setText(title.isEmpty() ? QString("No video selected") : title);

    QString sub;
    if (hasTracks)
        sub = QString("USB drive  ·  %1 video(s)").arg(tracks_.size());
    else
        sub = "No USB drive";
    if (videoOn && currentIndex_ >= 0)
        sub += QString("  ·  %1 of %2").arg(currentIndex_ + 1).arg(tracks_.size());
    subtitleLabel_->setText(sub);

    // 4. buttons
    const bool canControl = playerRunning_ && hasTracks;
    for (QPushButton* b : {prevButton_, nextButton_, playPauseButton_, stopButton_})
        b->setEnabled(canControl);
    backButton_->setEnabled(videoOn);
    forwardButton_->setEnabled(videoOn);
    seek_->setEnabled(videoOn);

    if (playerState_ == "PLAYING") {
        playPauseButton_->setIcon(makeIcon(IconPause));
        playPauseButton_->setToolTip("Pause (Space)");
    } else {
        playPauseButton_->setIcon(makeIcon(IconPlay));
        playPauseButton_->setToolTip(playerState_ == "PAUSED" ? "Resume (Space)" : "Play (Space)");
    }

    if (!videoOn)
        onPositionChanged(0, 0);

    // 5. header status (colored dot: green = OK, gray = not running)
    auto chip = [](const QString& name, bool ok, const QString& value) {
        return QString("<span style='color:%1'>●</span> %2 <b style='color:#f1f1f1'>%3</b>")
            .arg(ok ? "#2ba640" : "#717171", name, value);
    };
    statusLabel_->setText(chip("USB", mmRunning_ && hasTracks, mmRunning_ ? mediaState_ : "-") +
                          "&nbsp;&nbsp;&nbsp;" +
                          chip("Player", playerRunning_, playerRunning_ ? playerState_ : "-"));

    // 6. full screen: keep controls visible while not playing
    if (fullscreen_ && playerState_ != "PLAYING")
        showControls();
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
void MainWindow::onSeekReleased()
{
    if (durationMs_ <= 0) return;
    // D-Bus Seek is relative (+/- ms), so send the difference to the target
    const qint64 target = durationMs_ * seek_->value() / 1000;
    client_->seek(target - positionMs_);
}

/* ------------------------------------------------------------------ */
/* Full screen                                                         */
/*  The video is a native X11 window, so Qt cannot draw controls ON    */
/*  TOP of it. Full screen = hide header, playlist and title; the      */
/*  control bar stays under the video and auto-hides while playing.    */
/* ------------------------------------------------------------------ */
void MainWindow::toggleFullscreen()
{
    setFullscreen(!fullscreen_);
}

void MainWindow::setFullscreen(bool on)
{
    if (on == fullscreen_) return;
    fullscreen_ = on;

    header_->setVisible(!on);
    sidebar_->setVisible(!on);
    info_->setVisible(!on);
    bodyLayout_->setContentsMargins(on ? QMargins(0, 0, 0, 0) : QMargins(12, 10, 8, 8));

    fullscreenButton_->setIcon(makeIcon(on ? IconExitFullscreen : IconFullscreen));
    fullscreenButton_->setToolTip(on ? "Exit full screen (F / Esc)" : "Full screen (F)");

    // PC test (--windowed): also make the window itself full screen
    if (on) {
        windowWasFullScreen_ = isFullScreen();
        if (!windowWasFullScreen_) showFullScreen();
        showControls();
    } else {
        hideTimer_->stop();
        controls_->show();
        if (cursorHidden_) {
            QApplication::restoreOverrideCursor();
            cursorHidden_ = false;
        }
        if (!windowWasFullScreen_) showNormal();
    }
}

void MainWindow::showControls()
{
    if (!fullscreen_) return;
    if (!controls_->isVisible())
        controls_->show();
    if (cursorHidden_) {
        QApplication::restoreOverrideCursor();
        cursorHidden_ = false;
    }
    hideTimer_->start();   // (re)start the 3 s countdown
}

void MainWindow::hideControls()
{
    if (!fullscreen_ || playerState_ != "PLAYING" || seek_->isSliderDown())
        return;
    controls_->hide();
    if (!cursorHidden_) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        cursorHidden_ = true;
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
    case QEvent::KeyPress:
        showControls();     // does nothing when not in full screen
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, event);   // never eat the event
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
void MainWindow::showToast(const QString& text)
{
    toastLabel_->setText(text);
    toastLabel_->show();
    toastTimer_->start();
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
    // YouTube style: 3:07, 12:45, 1:02:09
    const qint64 s = ms / 1000;
    if (s >= 3600)
        return QString("%1:%2:%3").arg(s / 3600)
                                  .arg((s / 60) % 60, 2, 10, QChar('0'))
                                  .arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}
