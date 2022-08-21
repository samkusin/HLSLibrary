/**
 *  @file       elemstream.cpp
 *  @brief      A container for an elementary stream from a file container
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "elemstream.hpp"
#include <cassert>

namespace cinekav {

ElementaryStream::ElementaryStream() :
    _type(kNull),
    _progId(0),
    _index(0),
    _streamId(0),
    _dts(0),
    _pts(0),
    _ESAUBatch(nullptr),
    _ESAccessUnitCount(0)
{
}

ElementaryStream::~ElementaryStream()
{
    freeESAUBatches();
}

ElementaryStream::ElementaryStream(Buffer&& buffer, Type type, uint16_t progId,
                                   uint8_t index,
                                   const Memory& memory) :
    _memory(memory),
    _buffer(std::move(buffer)),
    _type(type),
    _progId(progId),
    _index(index),
    _streamId(0),
    _dts(0),
    _pts(0),
    _ESAUBatch(nullptr),
    _ESAccessUnitCount(0)
{
}

ElementaryStream::ElementaryStream(ElementaryStream&& other) :
    _memory(std::move(other._memory)),
    _buffer(std::move(other._buffer)),
    _type(other._type),
    _progId(other._progId),
    _index(other._index),
    _streamId(other._streamId),
    _dts(other._dts),
    _pts(other._pts),
    _ESAUBatch(other._ESAUBatch),
    _ESAccessUnitCount(other._ESAccessUnitCount),
    _parser(other._parser)
{
    other._type = kNull;
    other._progId = 0;
    other._index = 0;
    other._streamId = 0;
    other._dts = 0;
    other._pts = 0;
    other._ESAUBatch = nullptr;
    other._ESAccessUnitCount = 0;
    other._parser = ESAccessUnitParserState();
}

ElementaryStream& ElementaryStream::operator=(ElementaryStream&& other)
{
    freeESAUBatches();

    _memory = std::move(other._memory);
    _buffer = std::move(other._buffer);
    _type = other._type;
    _progId = other._progId;
    _index = other._index;
    _streamId = other._streamId;
    _pts = other._pts;
    _dts = other._dts;
    _ESAUBatch = other._ESAUBatch;
    _ESAccessUnitCount = other._ESAccessUnitCount;
    _parser = other._parser;

    other._type = kNull;
    other._progId = 0;
    other._streamId = 0;
    other._index = 0;
    other._dts = 0;
    other._pts = 0;
    other._ESAUBatch = nullptr;
    other._ESAccessUnitCount = 0;
    other._parser = ESAccessUnitParserState();

    return *this;
}

void ElementaryStream::freeESAUBatches()
{
    while (_ESAUBatch)
    {
        ESAccessUnitBatch* next = _ESAUBatch->nextBatch;
        _memory.free(_ESAUBatch->head);
        _memory.destroy(_ESAUBatch);
        _ESAUBatch = next;
    }
    _ESAccessUnitCount = 0;
}

size_t ElementaryStream::appendPayload(Buffer& source, size_t len, bool pesStart)
{
    if (len > _buffer.available())
    {
        return len - _buffer.available();
    }

    //  call out the start of our parsing region (the portion of the buffer
    //  to search for access units
    if (!_parser.head)
    {
        _parser.head = _buffer.head();
        _parser.tail = _parser.head;
        _parser.auStart = nullptr;
        _parser.VCLcheck = false;
    }

    //  len will be zero if there is no tail buffer.
    if (len == 0)
        return len;

    size_t pulled = 0;
    _buffer.pullBytesFrom(source, len, &pulled);
    len -= pulled;
    assert(len == 0);   // first length check should've prevented this

    //  the current end of buffer marker (limit of parsing.)
    _parser.tail = _buffer.tail();

    //  Parse from our current head to tail for access units.  We parse during
    //  buffer generation so we can assign the current dts/pts markers to
    //  frames.
    if (_type == kVideo_H264)
    {
        parseH264Stream();
    }

    return len;
}

void ElementaryStream::updatePts(uint64_t pts)
{
    _pts = pts;
    _dts = pts;
}

void ElementaryStream::updatePtsDts(uint64_t pts, uint64_t dts)
{
    _dts = dts;
    _pts = pts;
}

void ElementaryStream::appendAccessUnit(const uint8_t* data, size_t size)
{
    ESAccessUnitBatch* auBatch = _ESAUBatch ? _ESAUBatch : nullptr;
    if (!auBatch)
    {
        auBatch = _memory.create<ESAccessUnitBatch>();
    }
    if (auBatch)
    {
        if (!auBatch->head)
        {
            auBatch->head = reinterpret_cast<ESAccessUnit*>(
                _memory.allocate(sizeof(ESAccessUnit)*kAccessUnitCount)
                );
            auBatch->tail = auBatch->head;
            auBatch->limit = auBatch->tail + kAccessUnitCount;
        }
        else if (auBatch->tail == auBatch->limit)
        {
            //  new batch
            auBatch->nextBatch = _memory.create<ESAccessUnitBatch>();
            auBatch = auBatch->nextBatch;
        }
    }
    if (!auBatch)
    {
        return;
    }
    _ESAUBatch = auBatch;
    _ESAUBatch->tail->data = data;
    _ESAUBatch->tail->dataSize = size;
    _ESAUBatch->tail->dts = _dts;
    _ESAUBatch->tail->pts = _pts;
    ++_ESAUBatch->tail;
    ++_ESAccessUnitCount;
}

ESAccessUnit* ElementaryStream::accessUnit(size_t index)
{
    ESAccessUnitBatch* batch = _ESAUBatch;
    size_t minIndex = 0;
    while (batch)
    {
        size_t maxIndex = minIndex +  (batch->tail - batch->head);
        if (index >= minIndex && index < maxIndex)
        {
            return &batch->head[index - minIndex];
        }
        minIndex = maxIndex;
        batch = batch->nextBatch;
    }
    return nullptr;
}

#if CINEK_AVLIB_IOSTREAMS
std::basic_ostream<char>& ElementaryStream::write(std::basic_ostream<char>& ostr) const
{
    ostr.write((const char*)_buffer.head(), _buffer.size());
    return ostr;
}
#endif

void ElementaryStream::parseH264Stream()
{
    while (_parser.head+4 < _parser.tail)
    {
        //  detect start of NAL unit
        const uint8_t* hdr = _parser.head;
        bool ACUfinish = false;

        if (!hdr[0] && !hdr[1] && hdr[2] == 0x01)
        {
            //  0x000001 found, marking the start of a NAL unit
            //  next byte contains nal unit type (5 bits lsb)
            uint8_t NALType = hdr[3] & 0x1f;

            //  approximation of Fig 7-1 from the ITU-T H.264 spec (2012)
            //  A video frame contains non-VCL NAL units first, followed by
            //  VCL units.
            if (NALType > 0x00 && NALType < 0x0a)   // EndOfSeq
            {
                if (_parser.VCLcheck)
                {
                    if (NALType < 0x06)             // VCL
                    {
                        _parser.VCLcheck = false;
                    }
                }
                else
                {
                    if (NALType >= 0x06)            // Non-VCL
                    {
                        _parser.VCLcheck = true;
                        if (!_parser.auStart)
                        {
                            _parser.auStart = _parser.head;
                        }
                        else
                        {
                            ACUfinish = true;
                        }
                    }
                    else
                    {
                        if (hdr[4] & 0x80)
                        {
                            if (!_parser.auStart)
                            {
                                _parser.auStart = _parser.head;
                            }
                            else
                            {
                                ACUfinish = true;
                            }
                        }
                    }
                }
            }

            if (ACUfinish)
            {
                appendAccessUnit(_parser.auStart, _parser.head - _parser.auStart);
                _parser.auStart = nullptr;
                _parser.VCLcheck = false;
            }

            _parser.head += 4;
        }
        else
        {
            //  either outside or inside a NAL unit
            ++_parser.head;
        }
    }
}


}
