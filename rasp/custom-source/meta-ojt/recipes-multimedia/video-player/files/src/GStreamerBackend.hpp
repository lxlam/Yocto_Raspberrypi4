#pragma once
#include "MediaBackend.hpp"
#include <gst/gst.h>
#include <atomic>
#include <mutex>
#include <thread>

class GStreamerBackend final : public MediaBackend {
public:
    GStreamerBackend();
    ~GStreamerBackend() override;

    bool load(const std::string& path) override;
    bool play() override;
    bool pause() override;
    bool stop() override;
    bool seek(int64_t positionMs) override;
    int64_t getPosition() const override;
    int64_t getDuration() const override;
    void setEventCallback(BackendEventCallback callback) override;
    void setVideoWindow(std::uintptr_t handle) override;
    std::uint64_t generation() const override;

private:
    void busLoop();
    void notify(BackendEvent event, const std::string& detail, std::uint64_t generation);
    void goToNullState();          // stop pipeline, close file, drop old bus messages
    void chooseVideoSink();        // pick a sink that works with the X server

    GstElement* player_{nullptr};
    GstBus* bus_{nullptr};
    std::atomic<bool> busRunning_{false};
    std::thread busThread_;
    mutable std::mutex callbackMutex_;
    BackendEventCallback callback_;

    std::uintptr_t windowHandle_{0};
    bool videoSinkChosen_{false};

    mutable std::mutex genMutex_;  // protects generation_ + "pop message" together
    std::uint64_t generation_{0};
};
