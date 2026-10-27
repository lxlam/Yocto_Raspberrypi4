#pragma once
/*
 * PlayerClient - all D-Bus communication of the GUI (QtDBus)
 *
 *   media-manager  (org.automotive.MediaManager)
 *       <- signal StateChanged(s), PlaylistChanged(i)
 *       -> GetState(), GetPlaylist()
 *   video-player   (org.automotive.VideoPlayer)
 *       <- signal StatusChanged(s, i, s)
 *       -> Play Pause Stop Next Previous PlayIndex(i) Seek(x) SetVideoWindow(t) GetStatus()
 *
 * All calls are ASYNC: the GUI never freezes, even if a service hangs.
 * Results come back as Qt signals (see "signals:" below).
 */
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <functional>

class QDBusServiceWatcher;

struct Track {
    int id;
    QString title;
    QString path;
};

class PlayerClient : public QObject {
    Q_OBJECT
public:
    explicit PlayerClient(bool useSessionBus, QObject* parent = nullptr);

    // Commands to video-player. The new state comes back via statusChanged().
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void playIndex(int index);
    void seek(qint64 offsetMs);
    void setVideoWindow(quint64 windowId);

signals:
    void mediaManagerAvailable(bool running);
    void playerAvailable(bool running);
    void mediaStateChanged(const QString& state);            // NO_MEDIA / SCANNING / READY
    void playlistChanged(const QVector<Track>& tracks);
    void statusChanged(const QString& state, int index, const QString& title);
    void positionChanged(qint64 positionMs, qint64 durationMs);

public slots:
    void refreshAll();   // ask both services for their current data
    void pollStatus();   // GetStatus (called by a timer)

private slots:
    // D-Bus signal handlers (names must match the SLOT() strings in the .cpp)
    void onMmStateChanged(const QString& state);
    void onMmPlaylistChanged(int count);
    void onPlayerStatusChanged(const QString& state, int index, const QString& title);

private:
    using ReplyHandler = std::function<void(const QDBusMessage& reply)>;

    void asyncCall(const QString& service, const QString& path, const QString& iface,
                   const QString& method, const QList<QVariant>& args,
                   ReplyHandler onReply);
    void callPlayer(const QString& method, const QList<QVariant>& args = {});
    void requestMediaState();
    void requestPlaylist();

    QDBusConnection bus_;
    QDBusServiceWatcher* watcher_ = nullptr;
    bool mmRunning_ = false;
    bool playerRunning_ = false;
};
