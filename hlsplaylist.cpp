/**
 *  @file       hlsplaylist.cpp
 *  @brief      Parsers and containers for HLS playlists.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "hlsplaylist.hpp"

namespace cinekav {


HLSPlaylist::HLSPlaylist() :
    _seqNo(0),
    _targetDuration(0.f),
    _version(1)
{
}

HLSPlaylist::HLSPlaylist(const std::string& uri, const Memory& memory) :
    _uri(uri),
    _seqNo(0),
    _targetDuration(0.f),
    _version(1),
    _segments(memory)
{
}

HLSPlaylist::HLSPlaylist(HLSPlaylist&& other) :
    _uri(std::move(other._uri)),
    _seqNo(other._seqNo),
    _targetDuration(other._targetDuration),
    _version(other._version),
    _segments(std::move(other._segments))
{
    other._seqNo = 0;
    other._targetDuration = 0.f;
    other._version = 1;
}

HLSPlaylist& HLSPlaylist::operator=(HLSPlaylist&& other)
{
    _uri = std::move(other._uri);
    _seqNo = other._seqNo;
    _targetDuration = other._targetDuration;
    _segments = std::move(other._segments);
    _version = other._version;
    other._seqNo = 0;
    other._targetDuration = 0.f;
    other._version = 1;
    return *this;
}

void HLSPlaylist::addSegment(Segment&& segment)
{
    _segments.emplace_back(std::move(segment));
}

auto HLSPlaylist::segmentAt(size_t index) -> Segment*
{
    return const_cast<Segment*>(static_cast<const HLSPlaylist*>(this)->segmentAt(index));
}

auto HLSPlaylist::segmentAt(size_t index) const -> const Segment*
{
    if (index >= _segments.size())
        return nullptr;
    return &_segments[index];
}

////////////////////////////////////////////////////////////////////////////////

HLSPlaylistParser::HLSPlaylistParser() :
    _state(kInit),
    _info()
{
}

bool HLSPlaylistParser::parse(HLSPlaylist& playlist, const std::string& line)
{
    //  parse the trimmed version
    auto fpos = line.find_first_not_of(" \r\n\t");
    auto lpos = line.find_last_not_of(" \r\n\t");
    if (fpos == std::string::npos || lpos == std::string::npos)
        return true;
    std::string trimmed = line.substr(fpos, (lpos - fpos)+1);

    switch (_state)
    {
    case kInit:
        if (trimmed == "#EXTM3U")
        {
            _state = kInputLine;
        }
        break;
    case kInputLine:
        {
            if (trimmed[0]=='#')
            {
                auto paramSize = trimmed.find_first_of(':');
                if (paramSize != std::string::npos)
                {
                    auto valuePos = paramSize+1;
                    auto valueSize = valuePos;
                    valueSize = (trimmed.size() > valuePos) ? (trimmed.size() - valuePos)+1 :
                                std::string::npos;
                    if (valueSize > 0)
                    {
                        if (trimmed.compare(0, paramSize, "#EXT-X-VERSION")==0)
                        {
                            if (playlist._version == 1)
                            {
                                playlist._version =
                                    std::stoi(trimmed.substr(valuePos, valueSize));
                            }
                            else
                            {
                                //  warn?
                            }
                        }
                        else if (trimmed.compare(0, paramSize, "#EXT-X-TARGETDURATION")==0)
                        {
                            playlist._targetDuration =
                                std::stof(trimmed.substr(valuePos, valueSize));
                        }
                        else if (trimmed.compare(0, paramSize, "#EXT-X-MEDIA-SEQUENCE")==0)
                        {
                            playlist._seqNo =
                                std::stoi(trimmed.substr(valuePos, valueSize));
                        }
                        else if (trimmed.compare(0, paramSize, "#EXTINF")==0)
                        {
                            auto delim = trimmed.find_first_of(',', valuePos);
                            if (delim == std::string::npos)
                            {
                                //  error! standard requires #EXTINF:<length>,"
                            }
                            else
                            {
                                _info.duration =
                                    std::stof(trimmed.substr(valuePos, delim - valuePos));

                                //  determine if our uri is on the same line
                                //  or the next line.
                                if (delim+1 >= trimmed.size())
                                {
                                    _state = kPlaylistLine;
                                }
                                else
                                {
                                    _info.uri = trimmed.substr(delim+1);
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    case kPlaylistLine:
        {
            _info.uri = trimmed;
            playlist.addSegment(std::move(_info));
            _state = kInputLine;
        }
        break;
    }

    return true;
}

HLSMasterPlaylist::HLSMasterPlaylist(const Memory& memory) :
    _memory(memory),
    _playlists(_memory)
{
}

auto HLSMasterPlaylist::addStream
(
    const PlaylistInfo& info,
    const std::string& uri
) -> StreamInfo*
{
    //  sort?  criterion?
    _playlists.emplace_back();
    _playlists.back().info = info;
    _playlists.back().playlist = HLSPlaylist(uri, _memory);
    return &_playlists.back();
}

HLSMasterPlaylistParser::HLSMasterPlaylistParser() :
    _state(kInit),
    _version(1)
{
}

bool HLSMasterPlaylistParser::parse(HLSMasterPlaylist& playlist,
                                    const std::string& line)
{
    //  parse the trimmed version
    auto fpos = line.find_first_not_of(" \r\n\t");
    auto lpos = line.find_last_not_of(" \r\n\t");
    if (fpos == std::string::npos || lpos == std::string::npos)
        return true;
    std::string trimmed = line.substr(fpos, (lpos - fpos)+1);

    switch (_state)
    {
    case kInit:
        if (trimmed == "#EXTM3U")
        {
            _state = kInputLine;
        }
        break;
    case kInputLine:
        {
            if (trimmed[0]=='#')
            {
                auto paramSize = trimmed.find_first_of(':');
                if (paramSize != std::string::npos)
                {
                    auto valuePos = paramSize+1;
                    auto valueSize = valuePos;
                    valueSize = (trimmed.size() > valuePos) ? (trimmed.size() - valuePos)+1 :
                                std::string::npos;
                    if (valueSize > 0)
                    {
                        if (trimmed.compare(0, paramSize, "#EXT-X-VERSION")==0)
                        {
                            if (_version == 1)
                            {
                                _version = std::stoi(trimmed.substr(valuePos, valueSize));
                            }
                            else
                            {
                                //  warn?
                            }
                        }
                        else if (trimmed.compare(0, paramSize, "#EXT-X-STREAM-INF")==0)
                        {
                            //  parse the info line, note that many times our
                            //  valuePos will be std::string::npos+1, so
                            //  we add a >0 check to rule out valuePos rolling
                            //  over to 0.  valuePos would always start the loop
                            //  > 0, so this 'hackish' method should work.
                            while (valuePos < trimmed.size() && valuePos > 0)
                            {
                                auto delimPos = trimmed.find_first_of('=', valuePos);
                                std::string field = trimmed.substr(valuePos, delimPos - valuePos);
                                valuePos = delimPos+1;
                                delimPos = trimmed.find_first_of("\",", valuePos);
                                if (trimmed[delimPos]=='"')
                                {
                                    ++delimPos;
                                    //  retrieve quoted string
                                    delimPos = trimmed.find_first_of('"', delimPos);
                                    ++delimPos;
                                }
                                std::string value = trimmed.substr(valuePos, delimPos - valuePos);
                                valuePos = delimPos+1;

                                if (field == "BANDWIDTH")
                                {
                                    _info.bandwidth = std::stoi(value);
                                }
                                else if (field == "RESOLUTION")
                                {
                                    auto xPos = value.find_first_of('x');
                                    if (xPos != std::string::npos)
                                    {
                                        _info.frameWidth = std::stoi(value.substr(0, xPos));
                                        _info.frameHeight = std::stoi(value.substr(xPos+1));
                                    }
                                    else
                                    {
                                        //  warn?
                                    }
                                }
                                else if (field == "CODECS")
                                {
                                    parseCodecs(value);
                                }
                            }
                            //  next line will contain the uri for the playlist
                            _state = kPlaylistLine;
                        }
                    }
                }
            }
        }
        break;
    case kPlaylistLine:
        {
            playlist.addStream(_info, trimmed);
            _state = kInputLine;
        }
        break;
    }

    return true;
}

void HLSMasterPlaylistParser::parseCodecs(const std::string& line)
{
    //  todo as needed
    /*
    std::string::size_type lPos = 0;
    while (lPos < line.length())
    {
        auto rPos = line.find_first_of(',', lPos);
        if (rPos == std::string::npos)
            rPos = line.length();
        std::string codec = line.substr(lPos, rPos - lPos);
        //  parse codec line into our uint32_t

        lPos = rPos+1;
    }
    */
}



} /* namespace cinekav */
