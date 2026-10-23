#include "GStreamerBackend.hpp"
#include "PlayerCore.hpp"

#include <iostream>
#include <memory>
#include <vector>

static const char* stateName(PlayerState state)
{
    switch (state)
    {
    case PlayerState::STOPPED:
        return "STOPPED";

    case PlayerState::PLAYING:
        return "PLAYING";

    case PlayerState::PAUSED:
        return "PAUSED";

    case PlayerState::ERROR:
        return "ERROR";
    }

    return "UNKNOWN";
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr
            << "Usage: player-test "
            << "<media-file> [media-file ...]\n";

        return 1;
    }


    std::vector<MediaInfo> playlist;

    for (int i = 1; i < argc; ++i)
    {
        playlist.push_back({
            argv[i],
            argv[i]
        });
    }


    PlayerCore player(
        std::make_unique<GStreamerBackend>()
    );


    if (!player.setPlaylist(playlist))
    {
        std::cerr
            << "Failed to set playlist\n";

        return 1;
    }


    if (!player.play())
    {
        std::cerr
            << "Failed to start playback: "
            << player.getLastError()
            << "\n";

        return 1;
    }

    std::cout
        << "\n"
        << "Player test started\n"
        << "EOS auto-next is enabled\n"
        << "\n"
        << "Commands:\n"
        << "  p : play/resume\n"
        << "  a : pause\n"
        << "  s : stop\n"
        << "  n : next\n"
        << "  b : previous\n"
        << "  f : forward 10 seconds\n"
        << "  r : backward 10 seconds\n"
        << "  i : player information\n"
        << "  c : clear playlist\n"
        << "  q : quit\n"
        << "\n";


    for (char command;
         std::cout << "> " && std::cin >> command;)
    {
        switch (command)
        {
        case 'p':
        {
            if (!player.play())
            {
                std::cerr
                    << "Play failed: "
                    << player.getLastError()
                    << "\n";
            }

            break;
        }

        case 'a':
        {
            if (!player.pause())
            {
                std::cerr
                    << "Pause failed: "
<< player.getLastError()
                    << "\n";
            }

            break;
        }

        case 's':
        {
            if (!player.stop())
            {
                std::cerr
                    << "Stop failed: "
                    << player.getLastError()
                    << "\n";
            }

            break;
        }

        case 'n':
        {
            if (!player.next())
            {
                std::cerr
                    << "Next failed: "
                    << player.getLastError()
                    << "\n";
            }

            break;
        }

        case 'b':
        {
            if (!player.previous())
            {
                std::cerr
                    << "Previous failed: "
                    << player.getLastError()
                    << "\n";
            }

            break;
        }

        case 'f':
        {
            constexpr int64_t SEEK_STEP_MS = 10000;

            if (!player.seekForward(SEEK_STEP_MS))
            {
                std::cerr
                    << "Seek forward failed\n";
            }

            break;
        }

        case 'r':
        {
            constexpr int64_t SEEK_STEP_MS = 10000;

            if (!player.seekBackward(SEEK_STEP_MS))
            {
                std::cerr
                    << "Seek backward failed\n";
            }

            break;
        }

        case 'i':
        {
            const MediaInfo media =
                player.getCurrentMedia();

            std::cout
                << "\n"
                << "----- Player Info -----\n"
                << "State    : "
                << stateName(player.getState())
                << "\n"
                << "Index    : "
                << player.getCurrentIndex()
                << "\n"
                << "Media    : "
                << (
                    media.path.empty()
                        ? "<none>"
                        : media.path
                )
                << "\n"
                << "Position : "
                << player.getPosition() / 1000
                << " sec\n"
                << "Duration : "
                << player.getDuration() / 1000
                << " sec\n"
                << "Error    : "
                << (
                    player.getLastError().empty()
                        ? "<none>"
                        : player.getLastError()
                )
                << "\n"
                << "-----------------------\n";

            break;
        }

        case 'c':
        {


            player.clearPlaylist();

            std::cout
                << "Playlist cleared\n";

            break;
        }

        case 'q':
        {
            player.stop();

            std::cout
<< "Player test stopped\n";

            return 0;
        }

        default:
        {
            std::cout
                << "Unknown command: "
                << command
                << "\n";

            break;
        }
        }
    }

    return 0;
}
