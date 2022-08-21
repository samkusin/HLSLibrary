/**
 *  @file       hlsplaylist.hpp
 *  @brief      Parsers and containers for HLS playlists.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_HLSPLAYLIST_HPP
#define CINEK_AVLIB_HLSPLAYLIST_HPP

#include "avstream.hpp"
#include <array>
#include <vector>
#include <string>

namespace cinekav {
    namespace mpegts {
        class ElementaryStream;
    }
}

namespace cinekav {

class HLSPlaylistParser;



class HLSPlaylist
{
public:
    struct Segment
    {
        std::string uri;
        float duration;
        Segment() = default;
        Segment(Segment&& other) :
            uri(std::move(other.uri)),
            duration(other.duration)
        {
            other.duration = 0.f;
        }
        Segment& operator=(Segment&& other)
        {
            uri = std::move(other.uri);
            duration = other.duration;
            other.duration = 0.f;
            return *this;
        }
    };

    HLSPlaylist();
    HLSPlaylist(const std::string& uri, const Memory& memory=Memory());
    HLSPlaylist(HLSPlaylist&& other);
    HLSPlaylist& operator=(HLSPlaylist&& other);

    void addSegment(Segment&& segment);
    size_t segmentCount() const { return _segments.size(); }
    Segment* segmentAt(size_t index);
    const Segment* segmentAt(size_t index) const;
    const std::string& uri() const { return _uri; }

private:
    friend class HLSPlaylistParser;
    std::string _uri;
    int _seqNo;
    float _targetDuration;
    int _version;
    std::vector<Segment, std_allocator<Segment>> _segments;
};


class HLSPlaylistParser
{
public:
    HLSPlaylistParser();

    bool parse(HLSPlaylist& playlist, const std::string& line);

private:
    enum { kInit, kInputLine, kPlaylistLine } _state;
    HLSPlaylist::Segment _info;
};

class HLSMasterPlaylistParser;

class HLSMasterPlaylist
{
public:
    struct PlaylistInfo
    {
        uint32_t frameWidth = 0;
        uint32_t frameHeight = 0;
        uint32_t bandwidth = 0;
        std::array<uint32_t, 4> codecs;         // more?
        bool available = false;
    };

    struct StreamInfo
    {
        PlaylistInfo info;
        HLSPlaylist playlist;
    };

    using Playlists = std::vector<StreamInfo, std_allocator<StreamInfo>>;

    HLSMasterPlaylist(const Memory& memory=Memory());

    StreamInfo* addStream(const PlaylistInfo& info, const std::string& uri);

    Playlists::const_iterator begin() const {
        return _playlists.begin();
    }
    Playlists::const_iterator end() const {
        return _playlists.end();
    }
    Playlists::iterator begin() {
        return _playlists.begin();
    }
    Playlists::iterator end() {
        return _playlists.end();
    }

private:
    friend class HLSMasterPlaylistParser;
    Memory _memory;

    Playlists _playlists;
};

class HLSMasterPlaylistParser
{
public:
    HLSMasterPlaylistParser();

    bool parse(HLSMasterPlaylist& playlist, const std::string& line);

private:
    void parseCodecs(const std::string& line);

    enum { kInit, kInputLine, kPlaylistLine } _state;
    HLSMasterPlaylist::PlaylistInfo _info;
    int _version;
};

} /* namespace cinekav */

#endif
