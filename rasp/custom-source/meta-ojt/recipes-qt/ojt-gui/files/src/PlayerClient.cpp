#include "PlayerClient.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDebug>

static const char* MM_NAME  = "org.automotive.MediaManager";
static const char* MM_PATH  = "/org/automotive/MediaManager";
static const char* MM_IFACE = "org.automotive.MediaManager1";

static const char* VP_NAME  = "org.automotive.VideoPlayer";
static const char* VP_PATH  = "/org/automotive/VideoPlayer";
static const char* VP_IFACE = "org.automotive.VideoPlayer1";

static const int CALL_TIMEOUT_MS = 3000;

PlayerClient::PlayerClient(bool useSessionBus, QObject* parent)
    : QObject(parent),
      bus_(useSessionBus ? QDBusConnection::sessionBus() : QDBusConnection::systemBus())
{
    if (!bus_.isConnected()) {
        qWarning() << "[PlayerClient] Cannot connect to D-Bus:" << bus_.lastError().message();
    }

    // 1. Listen to D-Bus signals (works even if the service starts later)
    bus_.connect(MM_NAME, MM_PATH, MM_IFACE, "StateChanged",
                 this, SLOT(onMmStateChanged(QString)));
    bus_.connect(MM_NAME, MM_PATH, MM_IFACE, "PlaylistChanged",
                 this, SLOT(onMmPlaylistChanged(int)));
    bus_.connect(VP_NAME, VP_PATH, VP_IFACE, "StatusChanged",
                 this, SLOT(onPlayerStatusChanged(QString,int,QString)));

    // 2. Know when a service starts / stops / restarts
    watcher_ = new QDBusServiceWatcher(this);
    watcher_->setConnection(bus_);
    watcher_->addWatchedService(MM_NAME);
    watcher_->addWatchedService(VP_NAME);
    watcher_->setWatchMode(QDBusServiceWatcher::WatchForRegistration |
                           QDBusServiceWatcher::WatchForUnregistration);

    connect(watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString& name) {
        qInfo() << "[PlayerClient]" << name << "started";
        refreshAll();
    });
    connect(watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString& name) {
        qInfo() << "[PlayerClient]" << name << "stopped";
        refreshAll();
    });
}

/* ------------------------------------------------------------------ */
/* Generic async call: send, and run onReply() later when the answer arrives */
/* ------------------------------------------------------------------ */
void PlayerClient::asyncCall(const QString& service, const QString& path, const QString& iface,
                             const QString& method, const QList<QVariant>& args,
                             ReplyHandler onReply)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    msg.setArguments(args);

    auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(msg, CALL_TIMEOUT_MS), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [method, onReply](QDBusPendingCallWatcher* w) {
                const QDBusMessage reply = w->reply();
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    qWarning() << "[PlayerClient]" << method << "failed:" << reply.errorMessage();
                } else if (onReply) {
                    onReply(reply);
                }
                w->deleteLater();
            });
}

void PlayerClient::callPlayer(const QString& method, const QList<QVariant>& args)
{
    if (!playerRunning_) {
        qWarning() << "[PlayerClient] video-player not running, ignore" << method;
        return;
    }
    asyncCall(VP_NAME, VP_PATH, VP_IFACE, method, args, [method](const QDBusMessage& reply) {
        if (!reply.arguments().isEmpty() && !reply.arguments().first().toBool())
            qWarning() << "[PlayerClient]" << method << "returned false";
    });
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */
void PlayerClient::play()                 { callPlayer("Play"); }
void PlayerClient::pause()                { callPlayer("Pause"); }
void PlayerClient::stop()                 { callPlayer("Stop"); }
void PlayerClient::next()                 { callPlayer("Next"); }
void PlayerClient::previous()             { callPlayer("Previous"); }
void PlayerClient::playIndex(int index)   { callPlayer("PlayIndex", {QVariant::fromValue<int>(index)}); }
void PlayerClient::seek(qint64 offsetMs)  { callPlayer("Seek", {QVariant::fromValue<qint64>(offsetMs)}); }

void PlayerClient::setVideoWindow(quint64 windowId)
{
    qInfo() << "[PlayerClient] SetVideoWindow" << Qt::hex << windowId;
    callPlayer("SetVideoWindow", {QVariant::fromValue<quint64>(windowId)});  // D-Bus type 't'
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */
void PlayerClient::refreshAll()
{
    QDBusConnectionInterface* dbus = bus_.interface();
    mmRunning_     = dbus && dbus->isServiceRegistered(MM_NAME).value();
    playerRunning_ = dbus && dbus->isServiceRegistered(VP_NAME).value();

    emit mediaManagerAvailable(mmRunning_);
    emit playerAvailable(playerRunning_);

    if (mmRunning_) {
        requestMediaState();
        requestPlaylist();
    } else {
        emit mediaStateChanged("NO_MEDIA");
        emit playlistChanged({});
    }
    pollStatus();
}

void PlayerClient::requestMediaState()
{
    asyncCall(MM_NAME, MM_PATH, MM_IFACE, "GetState", {}, [this](const QDBusMessage& reply) {
        emit mediaStateChanged(reply.arguments().value(0).toString());
    });
}

void PlayerClient::requestPlaylist()
{
    asyncCall(MM_NAME, MM_PATH, MM_IFACE, "GetPlaylist", {}, [this](const QDBusMessage& reply) {
        QVector<Track> tracks;

        // Reply type a(iss): array of struct(id, title, path)
        const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            Track t;
            arg.beginStructure();
            arg >> t.id >> t.title >> t.path;
            arg.endStructure();
            tracks.append(t);
        }
        arg.endArray();

        qInfo() << "[PlayerClient] Playlist:" << tracks.size() << "track(s)";
        emit playlistChanged(tracks);
    });
}

void PlayerClient::pollStatus()
{
    if (!playerRunning_) {
        emit statusChanged("STOPPED", -1, "");
        emit positionChanged(0, 0);
        return;
    }
    asyncCall(VP_NAME, VP_PATH, VP_IFACE, "GetStatus", {}, [this](const QDBusMessage& reply) {
        // (s state, i index, s title, x position_ms, x duration_ms)
        const QList<QVariant> a = reply.arguments();
        if (a.size() < 5) return;
        emit statusChanged(a[0].toString(), a[1].toInt(), a[2].toString());
        emit positionChanged(a[3].toLongLong(), a[4].toLongLong());
    });
}

/* ------------------------------------------------------------------ */
/* D-Bus signal handlers                                               */
/* ------------------------------------------------------------------ */
void PlayerClient::onMmStateChanged(const QString& state)
{
    qInfo() << "[PlayerClient] <-- StateChanged" << state;
    emit mediaStateChanged(state);
}

void PlayerClient::onMmPlaylistChanged(int count)
{
    qInfo() << "[PlayerClient] <-- PlaylistChanged" << count;
    if (count == 0)
        emit playlistChanged({});      // USB removed or no video file
    else
        requestPlaylist();
}

void PlayerClient::onPlayerStatusChanged(const QString& state, int index, const QString& title)
{
    emit statusChanged(state, index, title);
}
