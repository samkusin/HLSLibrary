/**
 *  @file       hlstream.hpp
 *  @brief      Ingests HLS playlists and transport streams, and outputs
 *              encoded elementary stream data.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_HLSTREAM_HPP
#define CINEK_AVLIB_HLSTREAM_HPP

#include "avstream.hpp"
#include "hlsplaylist.hpp"
#include "mpegts.hpp"

namespace cinekav {
    namespace mpegts {
        class ElementaryStream;
    }
}

namespace cinekav {

class HLStream : public Stream
{
public:
    HLStream(const StreamInputCallbacks& inputCbs,
             Buffer&& videoBuffer,
             Buffer&& audioBuffer,
             const char* url,
             const Memory& memory=Memory());
    virtual ~HLStream();

    virtual void update() override;

    //  Obtain encoded data from our current read buffer.  This method advances
    //  may advance the read pointer as needed
    int pullEncodedData(ESAccessUnit* vau, ESAccessUnit* aau);

private:
    cinekav::ElementaryStream* createES(cinekav::ElementaryStream::Type,
                               uint16_t programId);
    cinekav::ElementaryStream* getES(uint16_t programId, uint16_t index);

    void finalizeES(uint16_t programId, uint16_t index);

    cinekav::ElementaryStream* handleOverflowES(uint16_t programId,
                                       uint16_t index,
                                       size_t len);

private:
    Memory _memory;
    StreamInputCallbacks _inputCbs;

    enum
    {
        kOpenRootList,
        kReadRootList,
        kOpenMediaList,
        kReadMediaList,
        kDownloadSegment,
        kOpenSegment,
        kReadSegment,
        kNoStreamError,
        kInStreamError,
        kMemoryError,
        kInternalError
    }
    _state;
    uint32_t _inputRequestHandle;
    uintptr_t _inputResourceHandle;
    Buffer _inputBuffer;

    HLSMasterPlaylist _masterPlaylist;
    HLSMasterPlaylist::Playlists::iterator _toParsePlaylist;
    HLSMasterPlaylist::Playlists::const_iterator _toPlayPlaylist;
    std::string _rootUrl;

    size_t _playlistSegmentIndex;

    Buffer _videoBuffer;
    Buffer _audioBuffer;
    cinekav::mpegts::Demuxer _demuxer;
    uint8_t _audioESIndex;          // 0x1  - 0x7f
    uint8_t _videoESIndex;          // 0x80 - 0xff

    int _bufferCount;

    using EStreams = std::vector<cinekav::ElementaryStream,
                                 std_allocator<cinekav::ElementaryStream>>;
    EStreams _audioStreams;
    EStreams _videoStreams;

    //  identifies which stream buffer within the index array marks the current
    //  read and writeindices.
    struct StreamPosition
    {
        size_t readFromIdx;                // read from buffer index
        size_t readDoneIdx;
        size_t readAUIdx;                  // read access unit within buffer

        size_t writeToIdx;                 // write to buffer index
        size_t writeDoneIdx;

        size_t bufferCnt;

        void reset(size_t bufferCnt);

        bool hasWriteSpace() const;
        bool hasReadSpace() const;
        bool advanceWrite();
        bool advanceRead();
    };

    StreamPosition _audioPos;
    StreamPosition _videoPos;

    void resetStreams();
    void startStreams();
    void stopStreams();

};

} /* namespace cinekav */

#endif
