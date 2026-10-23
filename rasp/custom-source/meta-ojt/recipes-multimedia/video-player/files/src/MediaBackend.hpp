#pragma once
#include <cstdint>
#include <functional>
#include <string>
 
enum class BackendEvent {
    EOS,
    ERROR
};
 
using BackendEventCallback =
    std::function<void(BackendEvent event, const std::string& detail)>;
 
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
};