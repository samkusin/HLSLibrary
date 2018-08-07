/**
 *  @file       hlstream.cpp
 *  @brief      Parses programs and elementary streams from a MPEG Transport
 *              stream.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "hlstream.hpp"
#include <string>

namespace cinekav {

/**
 *  The HLStream handles playback of a HTTP Live Stream
 *  
 *  Upon stream construction:
 *  - Set State to ROOTLIST
 *   - LOAD_LIST(URL) and parse to PLAYLIST 
 *  - Set State to MEDIALIST
 *   - Select media playlist from PLAYLIST - use mid-range bandwidth stream for 
 *     now
 *   - LOAD_LIST(root URL + media PL) and parse to MEDIALIST
 *  - Set State to PLAYBACK with current MEDIALIST at start
 *   - PLAY_LIST(Media Playlist ID)
 *
 *  Procedure - LOAD_LIST from URL
 *  - Invoke Open Callback to open the resource at the requested URL.
 *  - Invoke Size Callback to obtain the amount to retrieve the resource size
 *  - Invoke Read Callback to read the raw target hls list buffer
 *
 *  Procedure - PLAY_LIST from ID
 *
 */
HLStream::HLStream
(
    const StreamInputCallbacks& inputCbs,
    Buffer&& videoBuffer,
    Buffer&& audioBuffer,
    const char* url,
    const Memory& memory
) :
    _memory(memory),
    _inputCbs(inputCbs),
    _state(kOpenRootList),
    _inputRequestHandle(0),
    _inputResourceHandle(0),
    _masterPlaylist(memory),
    _toParsePlaylist(_masterPlaylist.end()),
    _toPlayPlaylist(_masterPlaylist.end()),
    _rootUrl(url),
    _playlistSegmentIndex(-1),
    _videoBuffer(std::move(videoBuffer)),
    _audioBuffer(std::move(audioBuffer)),
    _demuxer([this](cinekav::ElementaryStream::Type type,
                    uint16_t programId) -> cinekav::ElementaryStream*
            {
                return createES(type, programId);
            },
            [this](uint16_t programId, uint16_t index) -> cinekav::ElementaryStream*
            {
                return getES(programId, index);
            },
            [this](uint16_t programId, uint16_t index)
            {
                finalizeES(programId, index);
            },
            [this](uint16_t programId,
                   uint16_t index,
                   uint32_t len)  -> cinekav::ElementaryStream*
            {
                return handleOverflowES(programId, index, len);
            },
            _memory),
    _audioESIndex(0x01),
    _videoESIndex(0x80),
    _bufferCount(2),        // todo, make this a setting
    _audioStreams(_memory),
    _videoStreams(_memory)
{
    _inputRequestHandle = _inputCbs.openCb(url);
    
    //  if the url ends with a filename, strip it out
    auto endpos = _rootUrl.find_last_of('/');
    if (endpos != std::string::npos)
    {
        auto ext = _rootUrl.find_first_of('.', endpos);
        if (ext != std::string::npos)
        {
            _rootUrl = _rootUrl.substr(0, endpos+1);
        }
    }

    _audioStreams.reserve(_bufferCount);
    _videoStreams.reserve(_bufferCount);

    for (int i = 0; i < _bufferCount; ++i)
    {
        _audioStreams.emplace_back();
        _videoStreams.emplace_back();
    }

    resetStreams();
}

HLStream::~HLStream()
{
    if (_inputResourceHandle)
    {
        _inputCbs.closeCb(_inputResourceHandle);
    }
}

void HLStream::update()
{
    switch(_state)
    {
    case kOpenRootList:
    case kOpenMediaList:
    case kOpenSegment:
        {
            //  attempt to read the root playlist.  when read, proceed.
            auto status = _inputCbs.resultCb(_inputRequestHandle, &_inputResourceHandle);
            if (status == StreamInputCallbacks::Result::kComplete)
            {
                size_t fileSize = _inputCbs.sizeCb(_inputResourceHandle);
                if (fileSize != 0)
                {
                    _inputBuffer = std::move(Buffer(fileSize, _memory));
                    uint8_t* buf = _inputBuffer.obtain(fileSize);
                    if (buf)
                    {
                        _inputRequestHandle = _inputCbs.readCb(
                            _inputResourceHandle,
                            buf,
                            fileSize);
                        if (_state == kOpenRootList)
                            _state = kReadRootList;
                        else if (_state == kOpenMediaList)
                            _state = kReadMediaList;
                        else if (_state == kOpenSegment)
                            _state = kReadSegment;
                        else
                            _state = kInternalError;
                    }
                    else
                    {
                        _state = kMemoryError;
                    }
                }
                else
                {
                    _state = kNoStreamError;
                }
            }
            else if (status == StreamInputCallbacks::Result::kError ||
                     status == StreamInputCallbacks::Result::kInvalid)
            {
                _state = kNoStreamError;
            }
            if (_state == kNoStreamError)
            {
                if (_state == kOpenMediaList)
                {
                    _toParsePlaylist->info.available = false;
                }
            }
        }
        break;
    case kReadRootList:
        {
            //  parse the retrieved playlist, and select the appropriate
            //  media playlist based on a the first stream found (for now.)
            //  should at some point select a mid-range bandwidth and based
            //  on playback stats, reselect an determined bandwidth media
            //  playlist.
            //
            uintptr_t cnt;
            auto status = _inputCbs.resultCb(_inputRequestHandle, &cnt);
            if (status == StreamInputCallbacks::Result::kComplete)
            {
                HLSMasterPlaylistParser parser;
                StringBuffer sb(std::move(_inputBuffer));
                while (!sb.end())
                {
                    std::string line;
                    line.reserve(80);
                    sb.getline(line);
                    parser.parse(_masterPlaylist, line);
                }
                
                
                //  now open each stream in the master playlist by
                //  switch to the kOpenMediaList state.
                _toParsePlaylist = _masterPlaylist.begin();
                if (_toParsePlaylist != _masterPlaylist.end())
                {
                    std::string url;
                    if (!_toParsePlaylist->playlist.uri().compare(0, 5, "http:") ||
                        !_toParsePlaylist->playlist.uri().compare(0, 6, "https:"))
                    {
                        url = _toParsePlaylist->playlist.uri();
                    }
                    else
                    {
                        url = _rootUrl + _toParsePlaylist->playlist.uri();
                    }
                    _inputRequestHandle = _inputCbs.openCb(url.c_str());
                    _state = kOpenMediaList;
                }
                else
                {
                    _state = kNoStreamError;
                }
            }
            else if (status == StreamInputCallbacks::Result::kError ||
                     status == StreamInputCallbacks::Result::kInvalid)
            {
                _state = kNoStreamError;
            }
        }
        break;
    case kReadMediaList:
        {
            //  parse the retrieved playlist, and select the appropriate
            //  media playlist based on a the first stream found (for now.)
            //  should at some point select a mid-range bandwidth and based
            //  on playback stats, reselect an determined bandwidth media
            //  playlist.
            //
            uintptr_t cnt;
            auto status = _inputCbs.resultCb(_inputRequestHandle, &cnt);
            if (status == StreamInputCallbacks::Result::kComplete)
            {
                HLSPlaylistParser parser;
                StringBuffer sb(std::move(_inputBuffer));
                while (!sb.end())
                {
                    std::string line;
                    line.reserve(80);
                    sb.getline(line);
                    parser.parse(_toParsePlaylist->playlist, line);
                }

                _toParsePlaylist->info.available = true;
                
                //  now open each stream in the master playlist by
                //  switch to the kOpenMediaList state.
                ++_toParsePlaylist;
                if (_toParsePlaylist != _masterPlaylist.end())
                {
                    std::string url = _rootUrl + _toParsePlaylist->playlist.uri();
                    _inputRequestHandle = _inputCbs.openCb(url.c_str());
                    _state = kOpenMediaList;
                }
                else
                {
                    //  select a stream to play - for now, the 1st until the
                    //  library is more fleshed out
                    //
                    _toPlayPlaylist = _masterPlaylist.begin();
                    resetStreams();
                    _state = kDownloadSegment;
                }
            }
            else if (status == StreamInputCallbacks::Result::kError ||
                     status == StreamInputCallbacks::Result::kInvalid)
            {
                _state = kNoStreamError;
            }
            if (_state == kNoStreamError)
            {
                _toParsePlaylist->info.available = false;
            }
        }
        break;
    case kDownloadSegment:
        {
            //  HLStream's job is to fill the video and audio buffers with data
            //  from a Transport Stream's AV Elementary Streams
            //
            //  Starting from the first segment and continuing to the last
            //
            //  Streaming Method:
            //      Download current segment media file
            //      Demux media file to our Elementary Streams
            //      
            //      
            //  Elementary streams derive their Buffers from the application
            //  supplied video and audio buffers.
            //
            //  The demuxer will request ElementaryStream objects on a need
            //  basis.   Unfortunately we do not know the amount of memory
            //  needed for video and audio separately per transport stream.
            //  So our HLStream objects needs to track which sections of the
            //  master video and audio buffers are used per ElementaryStream
            //
            //  For this iteration, we'll attempt double buffering video and
            //  audio.
            //  
      
            auto& playlist =  (*_toPlayPlaylist).playlist;
            if (_playlistSegmentIndex < playlist.segmentCount())
            {
                if (_videoPos.hasWriteSpace() && _audioPos.hasWriteSpace())
                {
                    auto& segment = *playlist.segmentAt(_playlistSegmentIndex);
                    std::string url = _rootUrl + segment.uri;
                    _inputRequestHandle = _inputCbs.openCb(url.c_str());
                    _state = kOpenSegment;
                }
            }
        }
        break;
    case kReadSegment:
        {
            //  demux the read-in segment
            uintptr_t cnt;
            auto status = _inputCbs.resultCb(_inputRequestHandle, &cnt);
            if (status == StreamInputCallbacks::Result::kComplete)
            {
                //  prepare to read the next segment
                auto result = _demuxer.read(_inputBuffer);
                if (result == cinekav::mpegts::Demuxer::kComplete)
                {
                    ++_playlistSegmentIndex;
                    _state = kDownloadSegment;
                }
                else
                {
                    _state = kInStreamError;
                }
            }
            else if (status == StreamInputCallbacks::Result::kError ||
                     status == StreamInputCallbacks::Result::kInvalid)
            {
                //  retry.. report error?
                _state = kDownloadSegment;
            }
        }
        break;
    case kInStreamError:
    case kNoStreamError:
    case kMemoryError:
    default:
        break;
    }
}

//  obtain encoded data from our current read buffer.  
int HLStream::pullEncodedData(ESAccessUnit* vau, ESAccessUnit* aau)
{
    int res = 0;

    //  video
    if (_videoPos.hasReadSpace())
    {
        ElementaryStream& vstream = _videoStreams[_videoPos.readFromIdx];

        if (_videoPos.readAUIdx < vstream.accessUnitCount())
        {
            *vau = *vstream.accessUnit(_videoPos.readAUIdx);
            res |= 0x01;
            ++_videoPos.readAUIdx;
        }
        if (_videoPos.readAUIdx >= vstream.accessUnitCount())
        {
            if (_videoPos.advanceRead())
                _videoPos.readAUIdx = 0;
        }
    }

    //  audio
    if (_audioPos.hasReadSpace())
    {
        ElementaryStream& astream = _audioStreams[_audioPos.readFromIdx];

        if (_audioPos.readAUIdx < astream.accessUnitCount())
        {
            *aau = *astream.accessUnit(_audioPos.readAUIdx);
            res |= 0x02;
            ++_audioPos.readAUIdx;
        }
        if (_audioPos.readAUIdx >= astream.accessUnitCount())
        {
            if (_audioPos.advanceRead())
                _audioPos.readAUIdx = 0;
        }
    }
    return res;
}


//  Manage Elementary Streams using our double buffering technique when creating
//  or managing overflow (if this impl manages overflow)
//
cinekav::ElementaryStream* HLStream::createES
    (
        cinekav::ElementaryStream::Type type,
        uint16_t programId
    )
{
    //  create a buffer for the ES that spans the available space of our master
    //  buffer.  This may be the entire buffer, or a specific window, depending
    //  on how much of the buffer is being used by another ES.   Having a large
    //  enough master buffer to support at least two ES buffers *per* type is
    //  important, since its necessary to double buffer the incoming stream.
    cinekav::ElementaryStream* stream = nullptr;

    //  todo, a lot of repeated code here.   may need to consolidate this 
    //  some into a "stream object"
    switch (type)
    {
    case cinekav::ElementaryStream::kVideo_H264:        // video
        { 
            if (_videoESIndex == 0)
                _videoESIndex = 1;
            uint8_t esIndex = _videoESIndex++;


            int thisIdx = _videoPos.writeToIdx;
            const int kBufferSize = _videoBuffer.available()/2;
            Buffer streamBuffer = _videoBuffer.createSubBuffer(
                thisIdx * kBufferSize,
                kBufferSize);
            ElementaryStream estream(std::move(streamBuffer), type, programId,
                                        esIndex);
             
            _videoStreams[thisIdx] = std::move(estream);
            stream = &_videoStreams[thisIdx];
        }
        break;
    case cinekav::ElementaryStream::kAudio_AAC:         // audio
        {
            if (_audioESIndex == 0)
                _audioESIndex = 0x80;
            uint8_t esIndex = _audioESIndex++;


            int thisIdx = _audioPos.writeToIdx;
            const int kBufferSize = _audioBuffer.available()/2;
            Buffer streamBuffer = _audioBuffer.createSubBuffer(
                thisIdx * kBufferSize,
                kBufferSize);
            ElementaryStream estream(std::move(streamBuffer), type, programId,
                                        esIndex);
             
            _audioStreams[thisIdx] = std::move(estream);
            stream = &_audioStreams[thisIdx];
        }
        break;
    default:
        //  unsupported - returns null
        break;
    };

    return stream;
}

cinekav::ElementaryStream* HLStream::getES
    (
        uint16_t programId,
        uint16_t index
    )
{
    if (index > 0 && index < 0x80)
    {
        for (auto it = _videoStreams.begin(); it != _videoStreams.end(); ++it)
        {
            if ((*it).index() == index)
                return &(*it);
        }
    }
    else if (index >= 0x80 && index <= 0xff)
    {
        for (auto it = _audioStreams.begin(); it != _audioStreams.end(); ++it)
        {
            if ((*it).index() == index)
                return &(*it);
        }
    }
    return nullptr;
}
    
void HLStream::finalizeES(uint16_t programId, uint16_t index)
{
    //  advance our stream positions now that this stream's data has been
    //  full demuxed
    auto es = getES(programId, index);
    if (es)
    {
        if (es->index() < 0x80)
        {
            _videoPos.advanceWrite();
        }
        else
        {
            _audioPos.advanceWrite();
        }
    }
}

cinekav::ElementaryStream* HLStream::handleOverflowES(uint16_t programId,
                                    uint16_t index,
                                    uint32_t len)
{
    return nullptr;
}

void HLStream::StreamPosition::reset(int cnt)
{
    readFromIdx = 0;
    readAUIdx = 0;
    bufferCnt = cnt;

    writeToIdx = 0;
    writeDoneIdx = -1;
}

bool HLStream::StreamPosition::hasWriteSpace() const
{
    return (((writeToIdx+1)%bufferCnt) != readFromIdx) || writeDoneIdx != writeToIdx;
}

bool HLStream::StreamPosition::hasReadSpace() const
{
    return readFromIdx != writeToIdx;
}

bool HLStream::StreamPosition::advanceRead() 
{
    if (readFromIdx == writeToIdx)
        return false;

    //  advance our write-to-index if we've freed up the next buffer
    if (writeDoneIdx == writeToIdx && ((writeToIdx+1) % bufferCnt) == readFromIdx)
    {
        writeToIdx = readFromIdx;
    }

    readFromIdx = (readFromIdx+1) % bufferCnt;
    return true;
}

bool HLStream::StreamPosition::advanceWrite()
{
    writeDoneIdx = writeToIdx;

    int next = (writeToIdx+1) % bufferCnt;    

    if (next == readFromIdx)
        return false;

    writeToIdx = next;
    return true;
}

void HLStream::resetStreams()
{
    _audioPos.reset(_bufferCount);
    _videoPos.reset(_bufferCount);

    _audioESIndex = 0;
    _videoESIndex = 0;
    
    for (int i = 0; i < _bufferCount; ++i)
    {
        _audioStreams[i] = std::move(ElementaryStream());
        _videoStreams[i] = std::move(ElementaryStream());
    }

   
    _playlistSegmentIndex = 0;
}

void HLStream::startStreams()
{
}

void HLStream::stopStreams()
{
}

} /* namespace cinekav */