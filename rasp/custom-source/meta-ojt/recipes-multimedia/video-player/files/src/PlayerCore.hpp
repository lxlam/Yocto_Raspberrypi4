#pragma once
#include "MediaBackend.hpp"
#include "MediaInfo.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
 
enum class PlayerState {
    STOPPED,
    PLAYING,
    PAUSED,
    ERROR
};
 
class PlayerCore {
public:
    explicit PlayerCore(std::unique_ptr<MediaBackend> backend);
    ~PlayerCore();
 
    bool setPlaylist(const std::vector<MediaInfo>& playlist);
    void clearPlaylist();
 
    bool play();
    bool playIndex(std::size_t index);   // load + play track at index
    bool pause();
    bool stop();
    bool next();
    bool previous();
    bool seekForward(int64_t milliseconds);
    bool seekBackward(int64_t milliseconds);
 
    PlayerState getState() const;
    std::string getLastError() const;
    int64_t getPosition() const;
    int64_t getDuration() const;
    std::size_t getCurrentIndex() const;
    MediaInfo getCurrentMedia() const;
 
private:
    bool loadCurrentMediaLocked();
    bool playLocked();
    void handleBackendEvent(BackendEvent event, const std::string& detail);
 
    std::unique_ptr<MediaBackend> backend_;
    std::vector<MediaInfo> playlist_;
    std::size_t currentIndex_{0};
    PlayerState state_{PlayerState::STOPPED};
    std::string lastError_;
    mutable std::recursive_mutex mutex_;
};