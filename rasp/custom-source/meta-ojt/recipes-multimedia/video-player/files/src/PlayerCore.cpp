#include "PlayerCore.hpp"
#include <iostream>
#include <utility>
 
PlayerCore::PlayerCore(std::unique_ptr<MediaBackend> backend)
    : backend_(std::move(backend)) {
    if (backend_) {
        backend_->setEventCallback(
            [this](BackendEvent event, const std::string& detail) {
                handleBackendEvent(event, detail);
            });
    }
}
 
PlayerCore::~PlayerCore() {
    if (backend_) {
        backend_->setEventCallback(nullptr);
        backend_->stop();
    }
}
 
bool PlayerCore::setPlaylist(const std::vector<MediaInfo>& playlist) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!backend_) return false;
 
    backend_->stop();
    playlist_ = playlist;
    currentIndex_ = 0;
    state_ = PlayerState::STOPPED;
    lastError_.clear();
 
    if (playlist_.empty()) {
        std::cout << "[PlayerCore] Empty playlist\n";
        return true;
    }
 
    return loadCurrentMediaLocked();
}
 
void PlayerCore::clearPlaylist() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (backend_) backend_->stop();
    playlist_.clear();
    currentIndex_ = 0;
    state_ = PlayerState::STOPPED;
    lastError_.clear();
}
 
bool PlayerCore::loadCurrentMediaLocked() {
    if (!backend_ || playlist_.empty() || currentIndex_ >= playlist_.size()) {
        return false;
    }
 
    if (!backend_->load(playlist_[currentIndex_].path)) {
        state_ = PlayerState::ERROR;
        lastError_ = "Failed to load: " + playlist_[currentIndex_].path;
        return false;
    }
 
    return true;
}
 
bool PlayerCore::playLocked() {
    if (!backend_ || playlist_.empty()) return false;
 
    if (!backend_->play()) {
        state_ = PlayerState::ERROR;
        lastError_ = "Failed to change backend to PLAYING";
        return false;
    }
 
    state_ = PlayerState::PLAYING;
    lastError_.clear();
    return true;
}
 
bool PlayerCore::play() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return playLocked();
}
 
bool PlayerCore::playIndex(std::size_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index >= playlist_.size()) return false;

    currentIndex_ = index;
    if (!loadCurrentMediaLocked()) return false;
    return playLocked();
}

bool PlayerCore::pause() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!backend_ || state_ != PlayerState::PLAYING) return false;
 
    if (!backend_->pause()) {
        state_ = PlayerState::ERROR;
        lastError_ = "Failed to change backend to PAUSED";
        return false;
    }
 
    state_ = PlayerState::PAUSED;
    return true;
}
 
bool PlayerCore::stop() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!backend_) return false;
 
    if (!backend_->stop()) {
        state_ = PlayerState::ERROR;
        lastError_ = "Failed to stop backend";
        return false;
    }
 
    state_ = PlayerState::STOPPED;
    return true;
}
 
bool PlayerCore::next() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (playlist_.empty()) return false;
 
    const PlayerState oldState = state_;
    currentIndex_ = (currentIndex_ + 1) % playlist_.size();
 
    if (!loadCurrentMediaLocked()) return false;
 
    if (oldState == PlayerState::PLAYING) {
        return playLocked();
    }
 
    if (oldState == PlayerState::PAUSED) {
        if (!backend_->pause()) {
            state_ = PlayerState::ERROR;
            lastError_ = "Failed to keep next media paused";
            return false;
        }
        state_ = PlayerState::PAUSED;
    } else {
        state_ = PlayerState::STOPPED;
    }
 
    return true;
}
 
bool PlayerCore::previous() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (playlist_.empty()) return false;
 
    const PlayerState oldState = state_;
    currentIndex_ = currentIndex_ == 0
        ? playlist_.size() - 1 : currentIndex_ - 1;
 
    if (!loadCurrentMediaLocked()) return false;
 
    if (oldState == PlayerState::PLAYING) {
        return playLocked();
    }
 
    if (oldState == PlayerState::PAUSED) {
        if (!backend_->pause()) {
            state_ = PlayerState::ERROR;
            lastError_ = "Failed to keep previous media paused";
            return false;
        }
        state_ = PlayerState::PAUSED;
    } else {
        state_ = PlayerState::STOPPED;
    }
 
    return true;
}
 
bool PlayerCore::seekForward(int64_t milliseconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!backend_ || milliseconds < 0) return false;
 
    int64_t target = backend_->getPosition() + milliseconds;
    const int64_t duration = backend_->getDuration();
    if (duration > 0 && target > duration) target = duration;
    return backend_->seek(target);
}
 
bool PlayerCore::seekBackward(int64_t milliseconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!backend_ || milliseconds < 0) return false;
 
    int64_t target = backend_->getPosition() - milliseconds;
    if (target < 0) target = 0;
    return backend_->seek(target);
}
 
void PlayerCore::handleBackendEvent(
    BackendEvent event, const std::string& detail) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
 
    if (event == BackendEvent::EOS) {
        std::cout << "[PlayerCore] EOS: auto-next\n";
 
        if (playlist_.empty()) {
            state_ = PlayerState::STOPPED;
            return;
        }
 
        currentIndex_ = (currentIndex_ + 1) % playlist_.size();
        if (!loadCurrentMediaLocked() || !playLocked()) {
            state_ = PlayerState::ERROR;
            if (lastError_.empty()) lastError_ = "Failed to auto-play next media";
        }
        return;
    }
 
    if (event == BackendEvent::ERROR) {
        state_ = PlayerState::ERROR;
        lastError_ = detail;
        std::cerr << "[PlayerCore] Backend error: " << detail << "\n";
    }
}
 
PlayerState PlayerCore::getState() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return state_;
}
 
std::string PlayerCore::getLastError() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return lastError_;
}
 
int64_t PlayerCore::getPosition() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return backend_ ? backend_->getPosition() : 0;
}
 
int64_t PlayerCore::getDuration() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return backend_ ? backend_->getDuration() : 0;
}
 
std::size_t PlayerCore::getCurrentIndex() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return currentIndex_;
}
 
MediaInfo PlayerCore::getCurrentMedia() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (playlist_.empty() || currentIndex_ >= playlist_.size()) return {};
    return playlist_[currentIndex_];
}
 