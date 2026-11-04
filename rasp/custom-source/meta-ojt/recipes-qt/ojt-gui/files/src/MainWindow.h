#pragma once
#include "PlayerClient.h"

#include <QMainWindow>
#include <QSlider>
#include <QWidget>
#include <functional>
class QHBoxLayout;

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTimer;

/*
 * VideoWidget - an empty native X11 child window.
 * Its window id (winId) is sent to video-player, and GStreamer draws the video into it.
 * Qt must NOT paint here, otherwise it would draw over the video.
 */
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    QPaintEngine* paintEngine() const override { return nullptr; }
signals:
    void resized();          // size changed (e.g. full screen) -> video must be redrawn
    void doubleClicked();    // double click / double tap -> toggle full screen
protected:
    void paintEvent(QPaintEvent*) override {}
    void resizeEvent(QResizeEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
};

/*
 * SeekSlider - YouTube-like progress bar.
 * Click or drag anywhere on the bar: the handle jumps to that point.
 * When the finger / mouse is released, sliderReleased() is emitted.
 */
class SeekSlider : public QSlider {
public:
    explicit SeekSlider(QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
private:
    int valueAt(int x) const;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(PlayerClient* client, QWidget* parent = nullptr);
    void start();   // call after show(): send window id, load data, start timer
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;   // mouse / key activity


private:
    void addShortcut(int key, const std::function<void()>& action);
    void buildUi();
    QWidget* buildHeader();
    QWidget* buildControls();
    QWidget* buildInfo();
    QWidget* buildSidebar();
    void updateView();                                    // refresh texts/buttons from state
    void onPlaylistChanged(const QVector<Track>& tracks);
    void onStatusChanged(const QString& state, int index, const QString& title);
    void onPositionChanged(qint64 positionMs, qint64 durationMs);
    void onPlayPauseClicked();
    void onTrackActivated(QListWidgetItem* item);
    void onSeekReleased();

    // full screen
    void toggleFullscreen();
    void setFullscreen(bool on);
    void showControls();          // show the control bar (full screen) + restart hide timer
    void hideControls();          // called by hideTimer_ while playing in full screen

    void showToast(const QString& text);   // small message in the header, hides after 4 s
    static QString formatTime(qint64 ms);

    PlayerClient* client_;

    // widgets
    QWidget*        header_;
    QWidget*        sidebar_;
    QWidget*        controls_;
    QWidget*        info_;
    QHBoxLayout*    bodyLayout_;
    QStackedWidget* videoStack_;   // page 0 = message, page 1 = video
    QLabel*         messageLabel_;
    VideoWidget*    video_;
    QLabel*         playlistTitle_;
    QListWidget*    playlist_;
    QPushButton*    prevButton_;
    QPushButton*    backButton_;
    QPushButton*    playPauseButton_;
    QPushButton*    forwardButton_;
    QPushButton*    nextButton_;
    QPushButton*    stopButton_;
    QPushButton*    fullscreenButton_;
    SeekSlider*     seek_;
    QLabel*         timeLabel_;
    QLabel*         titleLabel_;      // big title under the video
    QLabel*         subtitleLabel_;   // "USB drive - 5 videos"
    QLabel*         statusLabel_;     // header: USB / Player state
    QLabel*         toastLabel_;      // header: short notification
    QTimer*         statusTimer_;     // GetStatus every 500 ms
    QTimer*         hideTimer_;       // auto-hide controls in full screen
    QTimer*         toastTimer_;      // hide toast
    QTimer*         exposeTimer_;     // redraw video after a resize

    // state received from the services
    bool           mmRunning_ = false;
    bool           playerRunning_ = false;
    QString        mediaState_ = "NO_MEDIA";
    QVector<Track> tracks_;
    QString        playerState_ = "STOPPED";
    int            currentIndex_ = -1;
    QString        currentTitle_;
    bool           hadPlaylist_ = false;
    qint64         positionMs_ = 0;
    qint64         durationMs_ = 0;

    // full screen state
    bool           fullscreen_ = false;
    bool           windowWasFullScreen_ = false;   // window state before entering (PC test)
    bool           cursorHidden_ = false;
};
