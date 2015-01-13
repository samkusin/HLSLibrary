/**
 *  @file       mpegts.hpp
 *  @brief      Parses programs and elementary streams from a MPEG Transport
 *              stream.
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_MPEG2TS_HPP
#define CINEK_AVLIB_MPEG2TS_HPP

#include "avlib.hpp"

#include <vector>

#if CINEK_AVLIB_IOSTREAMS
#include <istream>
#include <ostream>
#endif

namespace cinekav { namespace mpegts {

    constexpr int kDefaultPacketSize = 188;

    constexpr uint16_t kPID_PAT             = 0x0000;
    constexpr uint16_t kPID_Null            = 0x1fff;

    constexpr uint8_t kPAT_Program_Assoc_Table  = 0x00;
    constexpr uint8_t kPAT_Program_Map_Table    = 0x02;
    
    class Program;
    
    
    class ElementaryStream
    {
    public:
        enum Type
        {
            kNull               = 0x00,
            kAudio_AAC          = 0x0f,
            kVideo_H264         = 0x1b
        };
        
        static const uint32_t kDefaultBufferSize = 128*1024;
        
        ElementaryStream();
        ElementaryStream(Type type, uint16_t index);
        ~ElementaryStream();
        ElementaryStream(ElementaryStream&& other);
        ElementaryStream& operator=(ElementaryStream&& other);
        
        Type type() const { return _type; }
        uint16_t index() const { return _index; }
    
        int32_t parseHeader(Buffer& buffer, bool start);
        bool appendFrame(Buffer& buffer, uint32_t len);
        
    #if CINEK_AVLIB_IOSTREAMS
        std::basic_ostream<char>& write(std::basic_ostream<char>& ostr) const;
    #endif
    
        const ElementaryStream* prevStream() const {
            return _prev;
        }
    
    private:
        uint64_t pullTimecodeFromBuffer(Buffer& buffer);
        void updatePts(uint64_t pts);
        void updatePtsDts(uint64_t pts, uint64_t dts);
        
    private:
        friend class Program;
        
        Type _type;
        uint16_t _index;
        uint64_t _dts;
        uint64_t _lastPts;
        uint64_t _firstPts;
        uint64_t _firstDts;
        uint64_t _frameLength;
        uint64_t _frameCount;
        
        Buffer _header;
        uint16_t _headerFlags;
        uint8_t  _streamId;
        
        struct BufferNode
        {
            Buffer buffer;
            BufferNode* next;
        };
        BufferNode *_head;
        BufferNode *_tail;
        ElementaryStream *_prev;
    };
    
    class Program
    {
    public:
        Program();
        Program(uint16_t id);
        Program(Program&& other);
        ~Program();
        Program& operator=(Program&& other);
        
        uint16_t id() const { return _id; }
        
        ElementaryStream* appendStream(ElementaryStream::Type type,
                                       uint16_t index);
        ElementaryStream* findStream(uint16_t index);
        
        const ElementaryStream* firstStream() const {
            return _tail;
        }
        
    private:
        uint16_t _id;
        ElementaryStream *_tail;
    };


    class Demuxer
    {
    public:
        /// Result Codes
        enum Result
        {
            kComplete,
            kTruncated,
            kInvalidPacket,
            kContinue,
            kIOError,
            kOutOfMemory,
            kUnsupportedScramble,   ///< Unsupported feature : scrambling
            kUnsupportedTable,      ///< PSI table type is not supported
            kUnsupported,           ///< Feature within the TS is unsupported
            kInternalError          ///< Unknown (internal) error
        };

        Demuxer();
        ~Demuxer();

    #if CINEK_AVLIB_IOSTREAMS
        Result read(std::basic_istream<char>& istr);
    #endif
    
        int numPrograms() const { return _programs.size(); }
        const Program& program(int index) const {
            return _programs[index];
        }
    
        void reset();

    private:
        //  buffer state
        Buffer _buffer;

        //  sorted list of buffers (sorted by PID)
        struct BufferNode;

        BufferNode* _headBuffer;
        std::vector<Program, std_allocator<Program>> _programs;

        //  tracks the current state of parsing
        int _syncCnt;
        int _skipCnt;
        
    private:
        Result parsePacket();
        Result parsePayloadPSI(BufferNode& bufferNode, bool start);
        Result parseSectionPAT(BufferNode& bufferNode);
        Result parseSectionPMT(BufferNode& bufferNode, uint16_t programId);
        Result parsePayloadPES(BufferNode& bufferNode, bool start);
        
        BufferNode* createOrFindBuffer(uint16_t pid);
    };

}   /* namespace mpegts */ } /* namespace ckavlib */

#endif
