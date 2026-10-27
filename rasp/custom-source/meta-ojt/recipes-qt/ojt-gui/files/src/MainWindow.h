#pragma once
#include "PlayerClient.h"

#include <QMainWindow>
#include <QWidget>
#include <functional>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;

/*
 * VideoWidget - an empty native X11 child window.
 * Its window id (winId) is sent to video-player, and GStreamer draws the video into it.
 * Qt must NOT paint here, otherwise it would draw over the video.
 */
class VideoWidget : public QWidget {
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    QPaintEngine* paintEngine() const override { return nullptr; }
protected:
    void paintEvent(QPaintEvent*) override {}
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(PlayerClient* client, QWidget* parent = nullptr);
    void start();   // call after show(): send window id, load data, start timer

private:
    void addShortcut(int key, const std::function<void()>& action);
    void buildUi();
    void updateView();                                    // refresh texts/buttons from state
    void onPlaylistChanged(const QVector<Track>& tracks);
    void onStatusChanged(const QString& state, int index, const QString& title);
    void onPositionChanged(qint64 positionMs, qint64 durationMs);
    void onPlayPauseClicked();
    void onTrackActivated(QListWidgetItem* item);
    static QString formatTime(qint64 ms);

    PlayerClient* client_;

    // widgets
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
    QProgressBar*   progress_;
    QLabel*         timeLabel_;
    QLabel*         statusLabel_;
    QTimer*         statusTimer_;

    // state received from the services
    bool           mmRunning_ = false;
    bool           playerRunning_ = false;
    QString        mediaState_ = "NO_MEDIA";
    QVector<Track> tracks_;
    QString        playerState_ = "STOPPED";
    int            currentIndex_ = -1;
    bool           hadPlaylist_ = false;
};
