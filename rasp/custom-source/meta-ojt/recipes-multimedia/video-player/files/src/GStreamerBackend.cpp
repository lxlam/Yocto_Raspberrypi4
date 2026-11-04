#include "GStreamerBackend.hpp"
#include <gst/video/videooverlay.h>
#include <chrono>
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

void GStreamerBackend::notify(BackendEvent event, const std::string& detail,
                              std::uint64_t generation) {
    BackendEventCallback callback;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback = callback_;
    }

    if (callback) {
        callback(event, detail, generation);
    }
}

std::uint64_t GStreamerBackend::generation() const {
    std::lock_guard<std::mutex> lock(genMutex_);
    return generation_;
}

/*
 * NULL state (instead of READY):
 *   - closes the file -> nothing keeps the USB busy
 *   - GstPipeline "flushes" its bus at NULL -> old EOS/ERROR messages are dropped
 * Then generation_++ marks any message already taken by busLoop() as old.
 */
void GStreamerBackend::goToNullState() {
    gst_element_set_state(player_, GST_STATE_NULL);
    std::lock_guard<std::mutex> lock(genMutex_);
    ++generation_;
}

void GStreamerBackend::busLoop() {
    const GstMessageType filter = static_cast<GstMessageType>(
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

    while (busRunning_.load()) {
        GstMessage* message = nullptr;
        std::uint64_t generation = 0;
        {
            // pop + read generation together, so goToNullState() cannot run in between.
            // NON-blocking pop: the lock is held only for a few microseconds.
            std::lock_guard<std::mutex> lock(genMutex_);
            message = gst_bus_pop_filtered(bus_, filter);
            generation = generation_;
        }

        if (!message) {
            // wait OUTSIDE the lock, otherwise load()/stop() could wait a long time for it
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            std::cout << "[GStreamerBackend] EOS received\n";
            notify(BackendEvent::EOS, "End of stream", generation);
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
            notify(BackendEvent::ERROR, detail, generation);

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

/*
 * Choose the video sink once the X server is running (first load()).
 * OJT_VIDEO_SINK (set in video-player.sh) can force one, e.g. "glimagesink".
 * A sink is accepted if it can go to READY (= it could open the display).
 */
void GStreamerBackend::chooseVideoSink() {
    if (videoSinkChosen_) return;

    const char* candidates[] = {
        g_getenv("OJT_VIDEO_SINK"), "xvimagesink", "glimagesink", "ximagesink"
    };

    for (const char* name : candidates) {
        if (!name || !*name) continue;

        GstElement* sink = gst_element_factory_make(name, nullptr);
        if (!sink) continue;
        gst_object_ref_sink(sink);     // we own one normal reference

        if (gst_element_set_state(sink, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS) {
            gst_element_set_state(sink, GST_STATE_NULL);
            // Let the GUI get keyboard/mouse events, not the video window
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "handle-events")) {
                g_object_set(G_OBJECT(sink), "handle-events", FALSE, nullptr);
            }
            g_object_set(G_OBJECT(player_), "video-sink", sink, nullptr);  // playbin takes its own ref
            gst_object_unref(sink);
            videoSinkChosen_ = true;
            std::cout << "[GStreamerBackend] Video sink: " << name << "\n";
            return;
        }

        gst_element_set_state(sink, GST_STATE_NULL);
        gst_object_unref(sink);
    }

    std::cerr << "[GStreamerBackend] No X11 video sink usable yet (X server not running?), "
                 "using playbin default\n";
}

void GStreamerBackend::setVideoWindow(std::uintptr_t handle) {
    const bool sameWindow = (handle == windowHandle_);
    windowHandle_ = handle;
    if (player_) {
        // playbin forwards the window id to its video sink
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(player_),
                                            static_cast<guintptr>(handle));
        // The GUI sends the SAME window id again after the video box changed size
        // (e.g. full screen). handle-events is FALSE, so the sink does not see the
        // new size by itself: expose() makes it re-read the size and redraw.
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(player_));
    }
    if (!sameWindow)
        std::cout << "[GStreamerBackend] Video window = 0x" << std::hex << handle << std::dec << "\n";
}

bool GStreamerBackend::load(const std::string& path) {
    if (!player_) return false;

    goToNullState();
    chooseVideoSink();
    if (windowHandle_ != 0) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(player_),
                                            static_cast<guintptr>(windowHandle_));
    }

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
    if (!player_) return false;
    goToNullState();
    return true;
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
