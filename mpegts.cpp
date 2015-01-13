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

namespace cinekav { namespace mpegts {

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

ElementaryStream::ElementaryStream() :
    _type(kNull),
    _index(0),
    _dts(0),
    _lastPts(0),
    _firstPts(0),
    _firstDts(0),
    _frameLength(0),
    _frameCount(0),
    _headerFlags(0),
    _streamId(0),
    _head(nullptr),
    _tail(nullptr),
    _prev(nullptr)
{
}

ElementaryStream::ElementaryStream(Type type, uint16_t index) :
    _type(type),
    _index(index),
    _dts(0),
    _lastPts(0),
    _firstPts(0),
    _firstDts(0),
    _frameLength(0),
    _frameCount(0),
    _headerFlags(0),
    _streamId(0),
    _head(nullptr),
    _tail(nullptr),
    _prev(nullptr)
{
}

ElementaryStream::~ElementaryStream()
{
    while (_head)
    {
        auto next = _head->next;
        Memory::destroy(_head);
        _head = next;
    }
    _tail = nullptr;
}

ElementaryStream::ElementaryStream(ElementaryStream&& other) :
    _type(other._type),
    _index(other._index),
    _dts(other._dts),
    _lastPts(other._lastPts),
    _firstPts(other._firstPts),
    _firstDts(other._firstDts),
    _frameLength(other._frameLength),
    _frameCount(other._frameCount),
    _header(std::move(other._header)),
    _headerFlags(other._headerFlags),
    _streamId(other._streamId),
    _head(other._head),
    _tail(other._tail),
    _prev(other._prev)
{
    other._type = kNull;
    other._index = 0;
    other._head = other._tail = nullptr;
    other._prev = nullptr;
    other._lastPts = other._firstDts = other._dts = other._firstPts = 0;
    other._frameLength = 0;
    other._frameCount = 0;
    other._headerFlags = 0;
    other._streamId = 0;
}

ElementaryStream& ElementaryStream::operator=(ElementaryStream&& other)
{
    _type = other._type;
    _index = other._index;
    _head = other._head;
    _tail = other._tail;
    _prev = other._prev;
    _dts = other._dts;
    _firstDts = other._firstDts;
    _firstPts = other._firstPts;
    _lastPts = other._lastPts;
    _frameLength = other._frameLength;
    _frameCount = other._frameCount;
    
    _header = std::move(other._header);
    _headerFlags = other._headerFlags;
    _streamId = other._streamId;
    
    other._type = kNull;
    other._index = 0;
    other._head = other._tail = nullptr;
    other._prev = nullptr;
    other._lastPts = other._firstDts = other._dts = other._firstPts = 0;
    other._frameLength = 0;
    other._frameCount = 0;
    other._headerFlags = 0;
    other._streamId = 0;
    
    return *this;
}

int32_t ElementaryStream::parseHeader(Buffer& buffer, bool start)
{
    if (start)
    {
        //  parse the PES header
        //  note, the optional pes header is not available for stream IDs
        //  0xbe and 0xbf (and possibly more??)
        //  0xbe = Padding stream
        //  0xbf = Private stream 2
        //  http://dvd.sourceforge.net/dvdinfo/pes-hdr.html
        uint32_t startCode = buffer.pullUInt32();
        if ((startCode & 0xffffff00) != 0x00000100)
            return -1;
        _streamId = (uint8_t)(startCode & 0x000000ff);
        buffer.skip(2);         // PES Packet Length (needed?)
        if (_streamId != 0xbe && _streamId != 0xbf)
        {
            //  parse the optional header
            _headerFlags = buffer.pullUInt16();
            
            if ((_headerFlags & 0xc000) != 0x8000)
                return -1;
            if ((_headerFlags & 0x3000) != 0x0000)
                return -1;
            
            uint32_t hdrLen = buffer.pullByte();
            if (hdrLen > 0)
            {
                _header = std::move(Buffer(hdrLen));
            }
        }
    }
    
    uint32_t hdrLen = _header.writeAvailable();
    if (hdrLen)
    {
        if (hdrLen > buffer.available())
            hdrLen = buffer.available();
        buffer.pullBytesInto(_header, hdrLen, nullptr);
        hdrLen = _header.writeAvailable();
    
        //  header to read
        if (hdrLen == 0)
        {
            //  header to parse
            
            if ((_headerFlags & 0x00c0) == 0x0080)
            {
                // parse pts
                updatePts(pullTimecodeFromBuffer(_header));
                
            }
            else if ((_headerFlags & 0x00c0) == 0x00c0)
            {
                // parse pts, dts
                updatePtsDts
                (
                    pullTimecodeFromBuffer(_header),
                    pullTimecodeFromBuffer(_header)
                );
            }
        }
    }
    return hdrLen;
}

bool ElementaryStream::appendFrame(Buffer& buffer, uint32_t len)
{
    auto node = _tail;
    uint32_t xtra = 0;
    if (!node)
    {
        xtra = len;
        len = 0;
    }
    else if (len > node->buffer.writeAvailable())
    {
        xtra = len - node->buffer.writeAvailable();
        len = node->buffer.writeAvailable();
    }
    //  len will be zero if there is no tail buffer.
    if (len > 0)
    {
        buffer.pullBytesInto(_tail->buffer, len, nullptr);
    }
    if (xtra > 0)
    {
        node = _tail;
        _tail = Memory::create<BufferNode>();
        _tail->buffer = std::move(Buffer(kDefaultBufferSize));
        _tail->next = nullptr;
        if (node)
        {
            node->next = _tail;
        }
        buffer.pullBytesInto(_tail->buffer, xtra, nullptr);
    }
    if (!_head)
    {
        _head = _tail;
    }
    return true;
}

uint64_t ElementaryStream::pullTimecodeFromBuffer(Buffer& buffer)
{
    uint64_t tc = buffer.pullByte() << 29;
    tc |= (buffer.pullByte() << 22);
    tc |= ((buffer.pullByte() & 0xfe) << 14);
    tc |= (buffer.pullByte()) << 7;
    tc |= ((buffer.pullByte() & 0xfe) >> 1);
    return tc;
}

void ElementaryStream::updatePts(uint64_t pts)
{
    if (_dts > 0 && pts > _dts)
    {
        _frameLength = pts - _dts;
    }
    _dts = pts;
    if (pts > _lastPts)
        _lastPts = pts;
    if (!_firstPts)
        _firstPts = pts;
}

void ElementaryStream::updatePtsDts(uint64_t pts, uint64_t dts)
{
    if (_dts > 0 && dts > _dts)
    {
        _frameLength = dts - _dts;
    }
    _dts = dts;
    if (pts > _lastPts)
        _lastPts = pts;
    if (!_firstDts)
        _firstDts = dts;
}

#if CINEK_AVLIB_IOSTREAMS
std::basic_ostream<char>& ElementaryStream::write(std::basic_ostream<char>& ostr) const
{
    auto buffer = _head;
    while (buffer)
    {
        ostr.write((char*)buffer->buffer.head(), buffer->buffer.available());
        buffer = buffer->next;
    }
    return ostr;
}
#endif

////////////////////////////////////////////////////////////////////////////////

Program::Program() :
    _id(0xffff),
    _tail(nullptr)
{
}

Program::Program(uint16_t id) :
    _id(id),
    _tail(nullptr)
{
}

Program::Program(Program&& other) :
    _id(other._id),
    _tail(other._tail)
{
    other._id = 0xffff;
    other._tail = nullptr;
}

Program::~Program()
{
    while (_tail)
    {
        auto prev = _tail->_prev;
        Memory::destroy(_tail);
        _tail = prev;
    }
}

Program& Program::operator=(Program&& other)
{
    _id = other._id;
    _tail = other._tail;
    other._id = 0xffff;
    other._tail = nullptr;
    return *this;
}

ElementaryStream* Program::appendStream
(
    ElementaryStream::Type type,
    uint16_t index
)
{
    auto prev = _tail;
    _tail = Memory::create<ElementaryStream>(type, index);
    _tail->_prev = prev;
    return _tail;
}

ElementaryStream* Program::findStream(uint16_t index)
{
    auto stream = _tail;
    while (stream)
    {
        if (stream->index() == index)
            return stream;
        stream = stream->_prev;
    }
    return nullptr;
}

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
            uint8_t tableId;
            bool hasSectionSyntax;
        }
        psi;
        struct
        {
            uint16_t progId;    // program id that owns the stream
            uint8_t index;      // stream index within a Program
        }
        es;
    };
};

Demuxer::Demuxer() :
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
    //  with streaming, we need to manage our own buffer instead of relying
    //  on an application supplied buffer
    _buffer = std::move(Buffer(kDefaultPacketSize));

    if (!_buffer)
        return kOutOfMemory;

    //  restart demuxer
    reset();

    Result result = kContinue;

    while (result == kContinue)
    {
        _buffer.reset();
        
        printf("Pos Start: %u\n", (uint32_t)istr.tellg());

        //  read a single minimum-sized ts packet
        int cnt = _buffer.pushBytesFromStream(istr, kDefaultPacketSize);
        if (cnt == 0)
        {
            result = kComplete;
        }
        else if (cnt < 0)
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
        
        printf("Pos End: %u\n", (uint32_t)istr.tellg());
    }

    _buffer = std::move(Buffer());

    return result;
}
#endif

void Demuxer::reset()
{
    _syncCnt = 0;
    _skipCnt = 0;
    _programs.clear();
    while (_headBuffer)
    {
        BufferNode* next = _headBuffer->next;
        Memory::destroy(_headBuffer);
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
    int continuityCounter = byte & 0x0f;

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
    int payloadSize = _buffer.available();
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
        pidBuffer.buffer = std::move(Buffer(sectionLength));

        if (!pidBuffer.buffer)
            return kOutOfMemory;
    }

    if (!pidBuffer.buffer)
        return kInternalError;

    if (payloadSize > pidBuffer.buffer.writeAvailable())
        payloadSize = pidBuffer.buffer.writeAvailable();
    int pulled = 0;
    _buffer.pullBytesInto(pidBuffer.buffer, payloadSize, &pulled);
    if (pulled != payloadSize)
        return kInternalError;

    if (pidBuffer.buffer.writeAvailable())
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
                int numPrograms = (buffer.available() - 4) / 4;
                _programs.reserve(numPrograms);
                for (int i = 0; i < numPrograms && parseResult == kContinue; ++i)
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
   
        assert(buffer.available() == 4);
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
    
    _programs.emplace_back(progNum);
    
    return kContinue;
}

Demuxer::Result Demuxer::parseSectionPMT
(
    BufferNode& bufferNode,
    uint16_t programId
)
{
    auto it = std::find_if(_programs.begin(), _programs.end(),
        [&programId](const Program& p) -> bool { return p.id() == programId; });
    if (it == _programs.end())
        return kInvalidPacket;
    
    Program& program = (*it);
    
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
    uint16_t esIndex = 0;
    while (buffer.available() > 4)  // 4bytes, account for trailing crc32
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
            streamBuffer->type = BufferNode::kPES;
            streamBuffer->es.progId = programId;
            streamBuffer->es.index = ++esIndex;
            ElementaryStream* stream = program.findStream(streamBuffer->es.index);
            if (!stream)
            {
                stream = program.appendStream(
                    (ElementaryStream::Type)streamType,
                    streamBuffer->es.index
                );
            }
            if (!stream)
                return kOutOfMemory;
        }
    }
    
    return buffer.available() == 4 ? kContinue :kInvalidPacket;
}

auto Demuxer::parsePayloadPES
(
    BufferNode& bufferNode,
    bool start
) -> Demuxer::Result
{
    auto it = std::find_if(_programs.begin(), _programs.end(),
        [&bufferNode](const Program& p) -> bool {
            return p.id() == bufferNode.es.progId;
        });
    if (it == _programs.end())
        return kInternalError;
    
    Program& program = (*it);
    ElementaryStream* stream = program.findStream(bufferNode.es.index);
    if (!stream)
        return kInternalError;
    
    int32_t result = stream->parseHeader(_buffer, start);
    if (result < 0)
        return kInvalidPacket;
    if (result > 0)
        return kContinue;
    
    stream->appendFrame(_buffer, _buffer.available());
    
    return kContinue;
}

auto Demuxer::createOrFindBuffer(uint16_t pid) -> Demuxer::BufferNode*
{
    //  parse the payload based on the PID (packetId)
    BufferNode* pidNode = nullptr;
    if (!_headBuffer || pid < _headBuffer->pid)
    {
        _headBuffer = Memory::create<BufferNode>(pid, _headBuffer);
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
            node->next = Memory::create<BufferNode>(pid, next);
            node = node->next;
        }
        pidNode = node;
    }
    return pidNode;
}

}   /* namespace mpegts */ } /* namespace ckavlib */
