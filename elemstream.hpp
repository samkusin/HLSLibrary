/**
 *  @file       elemstream.hpp
 *  @brief      A container for an elementary stream from a file container
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_ELEMENTARY_STREAM_HPP
#define CINEK_AVLIB_ELEMENTARY_STREAM_HPP

#include "avlib.hpp"

#if CINEK_AVLIB_IOSTREAMS
#include <ostream>
#endif

namespace cinekav {

    struct ESAccessUnit
    {
        const uint8_t* data;
        size_t dataSize;
        uint64_t pts;
        uint64_t dts;
    };
    
    class ElementaryStream
    {
    public:
        enum Type
        {
            kNull               = 0x00,
            kAudio_AAC          = 0x0f,
            kVideo_H264         = 0x1b
        };
        
        ElementaryStream();
        ElementaryStream(Buffer&& buffer, Type type, uint16_t progId, uint8_t index,
                         const Memory& memory=Memory());
        ElementaryStream(ElementaryStream&& other);
        ElementaryStream& operator=(ElementaryStream&& other);

        ~ElementaryStream();
        
        operator bool() const { return _type != kNull; }

        Type type() const { return _type; }
        uint16_t programId() const { return _progId; }
        uint8_t index() const { return _index; }

        const Buffer& buffer() const { return _buffer; }
    
        uint32_t appendPayload(Buffer& source, uint32_t len, bool pesStart);
        
    #if CINEK_AVLIB_IOSTREAMS
        std::basic_ostream<char>& write(std::basic_ostream<char>& ostr) const;
    #endif
    
        void updateStreamId(uint8_t streamId) { _streamId = streamId; }
        void updatePts(uint64_t pts);
        void updatePtsDts(uint64_t pts, uint64_t dts);

        ESAccessUnit* accessUnit(size_t index);
        size_t accessUnitCount() const { return _ESAccessUnitCount; }
        
    private:
        void freeESAUBatches();

        Memory _memory;
        Buffer _buffer;
        Type _type;
        uint16_t _progId;
        uint8_t  _index;
        uint8_t  _streamId;
        uint64_t _dts;
        uint64_t _pts;

        //  todo - should be a parameter in the constructor
        //  this value allows for ~ 10 second long streams with 29.97 fps.
        //
        static const size_t kAccessUnitCount = 384;

        //  a growing, segmented vector (a primitive deque, etc.)
        struct ESAccessUnitBatch
        {
            ESAccessUnit* head;
            ESAccessUnit* tail;
            ESAccessUnit* limit;
            ESAccessUnitBatch* nextBatch;

            ESAccessUnitBatch() : 
                head(nullptr), tail(nullptr), limit(nullptr),
                nextBatch(nullptr) {}

        };
        ESAccessUnitBatch* _ESAUBatch;
        size_t _ESAccessUnitCount;

        //  access unit parsing state
        struct ESAccessUnitParserState
        {
            const uint8_t* head;
            const uint8_t* tail;
            const uint8_t* auStart;
            bool VCLcheck;
            ESAccessUnitParserState() :
                head(nullptr), tail(nullptr), auStart(nullptr),
                VCLcheck(false) {}
        };
        ESAccessUnitParserState _parser;

        void appendAccessUnit(const uint8_t* data, size_t size);
        void parseH264Stream();
    };

}

#endif
