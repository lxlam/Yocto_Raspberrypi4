#pragma once
#include <cstdint>
#include <functional>
#include <string>

enum class BackendEvent {
    EOS,
    ERROR
};

/*
 * generation: a counter that the backend increases on every load()/stop().
 * An event carries the generation of the media that produced it, so the
 * receiver can ignore OLD events (e.g. an ERROR from a USB that was pulled out
 * and has already been replaced by a new playlist).
 */
using BackendEventCallback =
    std::function<void(BackendEvent event, const std::string& detail,
                       std::uint64_t generation)>;

class MediaBackend {
public:
    virtual ~MediaBackend() = default;

    virtual bool load(const std::string& path) = 0;
    virtual bool play() = 0;
    virtual bool pause() = 0;
    virtual bool stop() = 0;
    virtual bool seek(int64_t positionMs) = 0;
    virtual int64_t getPosition() const = 0;
    virtual int64_t getDuration() const = 0;
    virtual void setEventCallback(BackendEventCallback callback) = 0;

    /* Draw the video inside an existing window (X11 window id). 0 = own window. */
    virtual void setVideoWindow(std::uintptr_t handle) = 0;

    /* Current generation (see BackendEventCallback). */
    virtual std::uint64_t generation() const = 0;
};
