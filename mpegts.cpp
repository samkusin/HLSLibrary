/**
 *  @file       mpegts.cpp
 *  @brief      Parses programs and elementary streams from a MPEG Transport
 *              stream.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "mpegts.hpp"

#include <cstdlib>
#include <algorithm>
#include <cassert>

namespace cinekav { namespace mpegts {

//  0x0f        = AAC
//  0x1b        = H.264 (AVC1)
//
static uint8_t kSupportedStreamFormats[16][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

////////////////////////////////////////////////////////////////////////////////

struct Demuxer::BufferNode
{
    BufferNode(uint16_t pid_, BufferNode* next_) :
        pid(pid_), next(next_), type(kNull) {}
    Buffer buffer;
    uint16_t pid;
    BufferNode* next;
    enum { kNull, kPSI, kPES } type;

    union
    {
        struct
        {
            uint16_t progId;
            uint8_t tableId;
            bool hasSectionSyntax;
        }
        psi;
        struct
        {
            uint16_t progId;    // program id that owns the stream
            uint16_t hdrFlags;  // PES packet header flags.
            uint8_t index;      // stream index within a Program
        }
        es;
    };
};

Demuxer::Demuxer(const CreateStreamFn& createStreamFn,
                 const GetStreamFn& getStreamFn,
                 const FinalizeStreamFn& finalStreamFn,
                 const OverflowStreamFn& overflowStreamFn,
                 const Memory& memory) :
    _memory(memory),
    _createStreamFn(createStreamFn),
    _getStreamFn(getStreamFn),
    _finalStreamFn(finalStreamFn),
    _overflowStreamFn(overflowStreamFn),
    _headBuffer(nullptr)
{
    reset();
}

Demuxer::~Demuxer()
{
    reset();
}

#if CINEK_AVLIB_IOSTREAMS
auto Demuxer::read(std::basic_istream<char> &istr) -> Result
{
    return readInternal(
        [&istr](Buffer& target) -> size_t {
            return target.pushBytesFromStream(istr, kDefaultPacketSize);
        });
}
#endif

auto Demuxer::read(Buffer& in) -> Result
{
    return readInternal(
        [&in](Buffer& target) -> size_t {
            size_t cnt = 0;
            target.pullBytesFrom(in, kDefaultPacketSize, &cnt);
            return cnt;
        });
}

auto Demuxer::readInternal(const std::function<size_t(Buffer&)>& inFn) -> Result
{
 //  with streaming, we need to manage our own buffer instead of relying
    //  on an application supplied buffer
    if (_buffer.capacity() < kDefaultPacketSize)
    {
        _buffer = Buffer(kDefaultPacketSize, _memory);
    }
    else
    {
        _buffer.reset();
    }

    if (!_buffer)
        return kOutOfMemory;

    //  restart demuxer
    reset();

    Result result = kContinue;

    while (result == kContinue)
    {
        _buffer.reset();

        //  read a single minimum-sized ts packet
        auto cnt = inFn(_buffer);

        if (cnt == 0)
        {
            result = kComplete;
        }
        else if (cnt == SIZE_MAX)
        {
            result = kIOError;
        }
        else if (cnt < kDefaultPacketSize)
        {
            result = kTruncated;
        }
        else
        {
            result = parsePacket();
        }
    }

    if (result == kComplete)
    {
        finalizeStreams();
    }

    return result;
}

void Demuxer::finalizeStreams()
{
    auto bufferNode = _headBuffer;
    while (bufferNode)
    {
        if (bufferNode->type == BufferNode::kPES)
        {
            _finalStreamFn(bufferNode->es.progId, bufferNode->es.index);
        }
        bufferNode = bufferNode->next;
    }
}

void Demuxer::reset()
{
    _syncCnt = 0;
    _skipCnt = 0;
    while (_headBuffer)
    {
        BufferNode* next = _headBuffer->next;
        _memory.destroy(_headBuffer);
        _headBuffer = next;
    }
}

auto Demuxer::parsePacket() -> Result
{
    uint8_t byte;
    uint16_t word;

    //  TS sync check
    byte = _buffer.pullByte();
    if (byte != 0x47)
        return kInvalidPacket;

    ++_syncCnt;

    //  parse the remaining 3 bytes from the header
    word = _buffer.pullUInt16();

    uint16_t pid = word & 0x1fff;
    bool payloadUnitStart = word & 0x4000;
    bool transportError = word & 0x8000;
    //  todo: priority?

    if (transportError)
    {
        ++_skipCnt;
        return kContinue;
    }

    byte = _buffer.pullByte();

    bool adaptationFieldExists = byte & 0x20;
    bool hasPayload = byte & 0x10;
    //int continuityCounter = byte & 0x0f;

    if (pid == kPID_Null || !hasPayload)
    {
        return kContinue;
    }

    //  parse the adaptation field - todo
    if (adaptationFieldExists)
    {
        byte = _buffer.pullByte();
        _buffer.skip(byte);
        if (_buffer.overflow())
            return kInvalidPacket;
    }

    BufferNode* pidNode = createOrFindBuffer(pid);
    if (!pidNode)
        return kOutOfMemory;

    if (pidNode->pid == kPID_PAT || pidNode->type == BufferNode::kPSI)
    {
        return parsePayloadPSI(*pidNode, payloadUnitStart);
    }
    else if (pidNode->type == BufferNode::kPES)
    {
        return parsePayloadPES(*pidNode, payloadUnitStart);
    }

    return kContinue;
}

auto Demuxer::parsePayloadPSI(BufferNode& pidBuffer, bool start) -> Result
{
    auto payloadSize = _buffer.size();
    if (start)
    {
        //  the pointer field used to offset the start of our table data, or 0.
        uint8_t byte = _buffer.pullByte();
        _buffer.skip(byte);
        if (_buffer.overflow())
            return kInvalidPacket;

        //  parse the table header
        uint8_t tableId = _buffer.pullByte();
        uint16_t sectionHeader = _buffer.pullUInt16();
        if ((sectionHeader & 0x3000)!=0x3000)
            return kInvalidPacket;

        //  obtain the table's syntax section for PAT and PMT sections.
        bool hasSyntaxSection = sectionHeader & 0x8000;
        uint16_t sectionLength = sectionHeader & 0x03ff;

        pidBuffer.type = BufferNode::kPSI;
        pidBuffer.psi.tableId = tableId;
        pidBuffer.psi.hasSectionSyntax = hasSyntaxSection;
        pidBuffer.buffer = Buffer(sectionLength, _memory);

        if (!pidBuffer.buffer)
            return kOutOfMemory;
    }

    if (!pidBuffer.buffer)
        return kInternalError;

    if (payloadSize > pidBuffer.buffer.available())
        payloadSize = pidBuffer.buffer.available();
    size_t pulled = 0;
    pidBuffer.buffer.pullBytesFrom(_buffer, payloadSize, &pulled);
    if (pulled != payloadSize)
        return kInternalError;

    if (pidBuffer.buffer.available())
        return kContinue;   // expecting more data

    if (pidBuffer.psi.hasSectionSyntax)
    {
        // iterate through all table entries
        Buffer& buffer = pidBuffer.buffer;
        uint16_t programId = buffer.pullUInt16();
        uint8_t byte = buffer.pullByte();
        if ((byte & 0xc0)!=0xc0)
            return kInvalidPacket;
        if ((byte & 0x01)!=0x01)
            return kUnsupportedTable;
        //uint8_t sectionStart = buffer.pullByte();
        //uint8_t sectionEnd = buffer.pullByte();
        buffer.skip(2);

        Result parseResult = kContinue;

        switch (pidBuffer.psi.tableId)
        {
        case kPAT_Program_Assoc_Table:
            {
                //  4 byte PAT entry
                size_t numPrograms = (buffer.size() - 4) / 4;
                for (size_t i = 0; i < numPrograms && parseResult == kContinue; ++i)
                {
                    parseResult = parseSectionPAT(pidBuffer);
                }
            }
            break;
        case kPAT_Program_Map_Table:
            {
                parseResult = parseSectionPMT(pidBuffer, programId);
            }
            break;
        default:
            parseResult = kUnsupportedTable;
            break;
        }

        assert(buffer.size() == 4);
        //uint32_t crc32 = buffer.pullUInt32();
        buffer.skip(4); // todo: CRC check?
    }
    else
    {
        return kUnsupportedTable;
    }

    return kContinue;
}

Demuxer::Result Demuxer::parseSectionPAT
(
    BufferNode& bufferNode
)
{
    //  register programs
    uint16_t progNum = bufferNode.buffer.pullUInt16();
    uint16_t progPid = bufferNode.buffer.pullUInt16();
    if ((progPid & 0xe000) != 0xe000)
        return kInvalidPacket;

    //  create a PSI buffer for our PMT
    progPid &= 0x1fff;
    BufferNode* pmtBuffer = createOrFindBuffer(progPid);
    if (!pmtBuffer)
        return kOutOfMemory;

    pmtBuffer->type = BufferNode::kPSI;
    pmtBuffer->psi.progId = progNum;
    pmtBuffer->psi.tableId = 0;
    pmtBuffer->psi.hasSectionSyntax = false;

    return kContinue;
}

Demuxer::Result Demuxer::parseSectionPMT
(
    BufferNode& bufferNode,
    uint16_t programId
)
{
    Buffer& buffer = bufferNode.buffer;
    //  register programs
    uint16_t pidPCR = buffer.pullUInt16();
    uint16_t progInfoLength = buffer.pullUInt16();
    if ((pidPCR & 0xe000) != 0xe000)
        return kInvalidPacket;
    if ((progInfoLength & 0xf000) != 0xf000)
        return kInvalidPacket;
    progInfoLength &= (0x03ff);

    //  todo: program descriptor parsing?
    buffer.skip(progInfoLength);

    //  parse elementary stream info
    while (buffer.size() > 4)  // 4bytes, account for trailing crc32
    {
        uint8_t streamType = buffer.pullByte();
        uint16_t pidStream = buffer.pullUInt16();
        if ((pidStream & 0xe000)!=0xe000)
            return kInvalidPacket;

        pidStream &= 0x1fff;

        //  todo: ES descriptor bytes - skip for now.
        uint16_t esDescLen = buffer.pullUInt16() & 0x03ff;
        buffer.skip(esDescLen);

        uint8_t validStreamType =
            kSupportedStreamFormats[(streamType & 0xf0)>>4][(streamType & 0x0f)];
        if (validStreamType)
        {
            BufferNode* streamBuffer = createOrFindBuffer(pidStream);
            if (!streamBuffer)
                return kInvalidPacket;
            if (streamBuffer->type == BufferNode::kNull)
            {
                streamBuffer->type = BufferNode::kPES;
                streamBuffer->es.progId = programId;
                streamBuffer->es.hdrFlags = 0;
                streamBuffer->es.index = 0;
            }
            ElementaryStream* stream = _getStreamFn(streamBuffer->es.progId,
                streamBuffer->es.index);
            if (!stream)
            {
                stream = _createStreamFn((ElementaryStream::Type)streamType,
                    streamBuffer->es.progId);
            }
            if (!stream)
                return kOutOfMemory;
            streamBuffer->es.index = stream->index();
        }
    }

    return buffer.size() == 4 ? kContinue :kInvalidPacket;
}

auto Demuxer::parsePayloadPES
(
    BufferNode& bufferNode,
    bool start
) -> Demuxer::Result
{
    ElementaryStream* stream = _getStreamFn(bufferNode.es.progId, bufferNode.es.index);
    if (!stream)
        return kContinue;

    auto& header = bufferNode.buffer;
    bool frameBegin = start;

    if (start)
    {
        //  parse the PES header
        //  note, the optional pes header is not available for stream IDs
        //  0xbe and 0xbf (and possibly more??)
        //  0xbe = Padding stream
        //  0xbf = Private stream 2
        //  http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
        uint32_t startCode = _buffer.pullUInt32();
        if ((startCode & 0xffffff00) != 0x00000100)
            return kInvalidPacket;
        uint8_t streamId = (uint8_t)(startCode & 0x000000ff);
        stream->updateStreamId(streamId);
        _buffer.skip(2);         // PES Packet Length (needed?)
        if (streamId != 0xbe && streamId != 0xbf)
        {
            //  parse the optional header
            uint16_t headerFlags = _buffer.pullUInt16();

            if ((headerFlags & 0xc000) != 0x8000)
                return kInvalidPacket;
            if ((headerFlags & 0x3000) != 0x0000)
                return kInvalidPacket;

            bufferNode.es.hdrFlags = headerFlags;

            uint32_t hdrLen = _buffer.pullByte();
            if (hdrLen > 0)
            {
                if (header.capacity() < hdrLen)
                {
                    header = Buffer(hdrLen, _memory);
                }
                else
                {
                    header.reset();
                }
            }
        }
    }

    auto hdrLen = header.available();
    if (hdrLen)
    {
        frameBegin = true;
        if (hdrLen > _buffer.size())
            hdrLen = _buffer.size();
        header.pullBytesFrom(_buffer, hdrLen, nullptr);
        hdrLen = header.available();

        //  header completely read from our input buffer?
        if (hdrLen == 0)
        {
            //  header to parse

            if ((bufferNode.es.hdrFlags & 0x00c0) == 0x0080)
            {
                // parse pts
                stream->updatePts(pullTimecodeFromBuffer(header));

            }
            else if ((bufferNode.es.hdrFlags & 0x00c0) == 0x00c0)
            {
                // parse pts, dts
                stream->updatePtsDts
                (
                    pullTimecodeFromBuffer(header),
                    pullTimecodeFromBuffer(header)
                );
            }
        }
        else
        {
            return kContinue;
        }
    }

    auto overflow = stream->appendPayload(_buffer, _buffer.size(), frameBegin);
    if (overflow)
    {
        //  allow the caller to give us a valid stream to read back into in the
        //  case of an overflow.  failing that, then report an overflow error.
        stream = _overflowStreamFn(bufferNode.es.progId, bufferNode.es.index,
                                   overflow);
        if (stream)
        {
            overflow = stream->appendPayload(_buffer, _buffer.size(), frameBegin);
        }
        if (overflow || !stream)
            return kStreamOverflow;
    }

    return kContinue;
}

auto Demuxer::createOrFindBuffer(uint16_t pid) -> Demuxer::BufferNode*
{
    //  parse the payload based on the PID (packetId)
    BufferNode* pidNode = nullptr;
    if (!_headBuffer || pid < _headBuffer->pid)
    {
        _headBuffer = _memory.create<BufferNode>(pid, _headBuffer);
        pidNode = _headBuffer;
    }
    else
    {
        auto node = _headBuffer;
        auto next = node->next;
        while (next)
        {
            if (pid < next->pid)
                break;
            node = next;
            next = node->next;
        }

        if (pid != node->pid)
        {
            node->next = _memory.create<BufferNode>(pid, next);
            node = node->next;
        }
        pidNode = node;
    }
    return pidNode;
}

uint64_t Demuxer::pullTimecodeFromBuffer(Buffer& buffer)
{
    uint64_t tc = buffer.pullByte() << 29;
    tc |= (buffer.pullByte() << 22);
    tc |= ((buffer.pullByte() & 0xfe) << 14);
    tc |= (buffer.pullByte()) << 7;
    tc |= ((buffer.pullByte() & 0xfe) >> 1);
    return tc;
}

}   /* namespace mpegts */ } /* namespace ckavlib */
