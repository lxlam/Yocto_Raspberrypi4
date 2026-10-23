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
 
private:
    void busLoop();
    void notify(BackendEvent event, const std::string& detail);
 
    GstElement* player_{nullptr};
    GstBus* bus_{nullptr};
    std::atomic<bool> busRunning_{false};
    std::thread busThread_;
    mutable std::mutex callbackMutex_;
    BackendEventCallback callback_;
};