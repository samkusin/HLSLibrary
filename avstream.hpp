/**
 *  @file       avstream.hpp
 *  @brief      Parses programs and elementary streams from a MPEG Transport
 *              stream.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_STREAM_HPP
#define CINEK_AVLIB_STREAM_HPP

#include "avlib.hpp"

#include <functional>

namespace cinekav {
/*
    VideoStream concept

    Description:
    Provides a read-only interface for streaming video data from an
    external source.

    Detail:

    The VideoStream offers a container-independent interface for reading video
    data.  Input comes from an external source (a Stream?) and parsed by the
    VideoStream.   Video players use the VideoStream to obtain encoded data on a
    per unit basis.  It is the video player's responsibility to decode these
    data units into frames for display.

    Inputs:
      Data from a named stream.  The VideoStream expects an input data stream to
      provide the following MANDATORY actions:
        - Opening a Stream
        - Reading Binary Data from a Stream
        - Closing a Stream
        - Total Amount of data to be accessed from the Stream (file size)

      A data stream can provide the following OPTIONAL actions depending on what
      input format the VideoStream implementation expects (i.e. file based and
      HTTP pseudostreaming allow a form of seek, while HLS will just read data
      sequentially from a playlist file or a transport stream.)
        - Seek to Absolute Position in Stream (seek)

    Outputs:
      Outputs are in the form requests made from a video playing application to
      the VideoStream.
        - Video Data encoded
        - Audio Data encoded

    Controls:
      Applications can manipulate a VideoStream with the following controls:
        - Open
        - Close
*/

class StreamInputCallbacks
{
public:
    enum class Result
    {
        kInvalid,
        kPending,
        kComplete,
        kError
    };

    //  A request handle of 0 is considered invalid (null)
    //  A file handle of 0 is considered invalid (null)

    using OpenCb = std::function<uint32_t(const char* url)>;
    using CloseCb = std::function<void(uintptr_t hnd)>;
    using ReadCb = std::function<uint32_t(uintptr_t hnd, uint8_t* p, size_t cnt)>;
    using SizeCb = std::function<size_t(uintptr_t hnd)>;
    using ResultCb = std::function<Result(uint32_t poll, uintptr_t* result)>;

    OpenCb openCb;
    SizeCb sizeCb;
    CloseCb closeCb;
    ReadCb readCb;
    ResultCb resultCb;
};

class Stream
{
public:
    virtual ~Stream() {}

    virtual void update() = 0;
};

} /* namespace cinekav */

#endif