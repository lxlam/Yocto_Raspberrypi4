#include "GStreamerBackend.hpp"
#include <iostream>
#include <utility>
 
GStreamerBackend::GStreamerBackend() {
    gst_init(nullptr, nullptr);
 
    player_ = gst_element_factory_make("playbin", "player");
    if (!player_) {
        std::cerr << "[GStreamerBackend] Failed to create playbin\n";
        return;
    }
 
    bus_ = gst_element_get_bus(player_);
    if (!bus_) {
        std::cerr << "[GStreamerBackend] Failed to get GstBus\n";
        gst_object_unref(player_);
        player_ = nullptr;
        return;
    }
 
    busRunning_.store(true);
    busThread_ = std::thread(&GStreamerBackend::busLoop, this);
}
 
GStreamerBackend::~GStreamerBackend() {
    busRunning_.store(false);
 
    if (player_) {
        gst_element_set_state(player_, GST_STATE_NULL);
    }
 
    if (busThread_.joinable()) {
        busThread_.join();
    }
 
    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }
 
    if (player_) {
        gst_object_unref(player_);
        player_ = nullptr;
    }
}
 
void GStreamerBackend::setEventCallback(BackendEventCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = std::move(callback);
}
 
void GStreamerBackend::notify(BackendEvent event, const std::string& detail) {
    BackendEventCallback callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback = callback_;
    }
 
    if (callback) {
        callback(event, detail);
    }
}
 
void GStreamerBackend::busLoop() {
    const GstMessageType filter = static_cast<GstMessageType>(
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
 
    while (busRunning_.load()) {
        GstMessage* message = gst_bus_timed_pop_filtered(
            bus_, 100 * GST_MSECOND, filter);
 
        if (!message) {
            continue;
        }
 
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            std::cout << "[GStreamerBackend] EOS received\n";
            notify(BackendEvent::EOS, "End of stream");
            break;
 
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debugInfo = nullptr;
            gst_message_parse_error(message, &error, &debugInfo);
 
            std::string detail = error ? error->message : "Unknown GStreamer error";
            if (debugInfo) {
                detail += " | Debug: ";
                detail += debugInfo;
            }
 
            std::cerr << "[GStreamerBackend] ERROR: " << detail << "\n";
            notify(BackendEvent::ERROR, detail);
 
            if (error) g_error_free(error);
            if (debugInfo) g_free(debugInfo);
            break;
        }
 
        default:
            break;
        }
 
        gst_message_unref(message);
    }
}
 
bool GStreamerBackend::load(const std::string& path) {
    if (!player_) return false;
 
    gst_element_set_state(player_, GST_STATE_READY);
 
    GError* error = nullptr;
    gchar* uri = gst_filename_to_uri(path.c_str(), &error);
    if (!uri) {
        std::cerr << "[GStreamerBackend] URI error: "
                  << (error ? error->message : "unknown") << "\n";
        if (error) g_error_free(error);
        return false;
    }
 
    g_object_set(G_OBJECT(player_), "uri", uri, nullptr);
    g_free(uri);
 
    std::cout << "[GStreamerBackend] Loaded: " << path << "\n";
    return true;
}
 
bool GStreamerBackend::play() {
    return player_ &&
        gst_element_set_state(player_, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
}
 
bool GStreamerBackend::pause() {
    return player_ &&
        gst_element_set_state(player_, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE;
}
 
bool GStreamerBackend::stop() {
    return player_ &&
        gst_element_set_state(player_, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE;
}
 
bool GStreamerBackend::seek(int64_t positionMs) {
    if (!player_) return false;
    if (positionMs < 0) positionMs = 0;
 
    return gst_element_seek_simple(
        player_,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        static_cast<gint64>(positionMs) * GST_MSECOND);
}
 
int64_t GStreamerBackend::getPosition() const {
    if (!player_) return 0;
 
    gint64 value = 0;
    return gst_element_query_position(player_, GST_FORMAT_TIME, &value)
        ? value / GST_MSECOND : 0;
}
 
int64_t GStreamerBackend::getDuration() const {
    if (!player_) return 0;
 
    gint64 value = 0;
    return gst_element_query_duration(player_, GST_FORMAT_TIME, &value)
        ? value / GST_MSECOND : 0;
}