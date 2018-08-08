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
#include "elemstream.hpp"

#include <functional>

#if CINEK_AVLIB_IOSTREAMS
#include <istream>
#endif

namespace cinekav {
    class ElementaryStream;
}

namespace cinekav { namespace mpegts {

    constexpr int kDefaultPacketSize = 188;

    constexpr uint16_t kPID_PAT             = 0x0000;
    constexpr uint16_t kPID_Null            = 0x1fff;

    constexpr uint8_t kPAT_Program_Assoc_Table  = 0x00;
    constexpr uint8_t kPAT_Program_Map_Table    = 0x02;
    
    
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
            kStreamOverflow,        ///< Output ES overflow detected
            kUnsupportedTable,      ///< PSI table type is not supported
            kUnsupported,           ///< Feature within the TS is unsupported
            kInternalError          ///< Unknown (internal) error
        };

        using CreateStreamFn = 
            std::function<cinekav::ElementaryStream*(cinekav::ElementaryStream::Type,
                                            uint16_t programId)>;
        using GetStreamFn = 
            std::function<cinekav::ElementaryStream*(uint16_t programId,
                                            uint16_t index)>;
        using FinalizeStreamFn =
            std::function<void(uint16_t programId, uint16_t index)>;

        using OverflowStreamFn = 
            std::function<cinekav::ElementaryStream*(uint16_t programId,
                                            uint16_t index,
                                            uint32_t len)>;

        Demuxer(const CreateStreamFn& createStreamFn,
                const GetStreamFn& getStreamFn, 
                const FinalizeStreamFn& finalStreamFn,
                const OverflowStreamFn& overflowStreamFn,
                const Memory& memory =Memory());
        ~Demuxer();

    #if CINEK_AVLIB_IOSTREAMS
        Result read(std::basic_istream<char>& istr);
    #endif
        Result read(Buffer& buffer);
    
        void reset();

    private:
        Result readInternal(const std::function<int(Buffer&)>& inFn);
        //  buffer state
        Memory _memory;
        Buffer _buffer;

        CreateStreamFn _createStreamFn;
        GetStreamFn _getStreamFn;
        FinalizeStreamFn _finalStreamFn;
        OverflowStreamFn _overflowStreamFn;

        //  sorted list of buffers (sorted by PID)
        struct BufferNode;

        BufferNode* _headBuffer;
        
        //  tracks the current state of parsing
        int _syncCnt;
        int _skipCnt;
        
    private:
        Result readInternal();
        void finalizeStreams();

        Result parsePacket();
        Result parsePayloadPSI(BufferNode& bufferNode, bool start);
        Result parseSectionPAT(BufferNode& bufferNode);
        Result parseSectionPMT(BufferNode& bufferNode, uint16_t programId);
        Result parsePayloadPES(BufferNode& bufferNode, bool start);

        uint64_t pullTimecodeFromBuffer(Buffer& buffer);
        
        BufferNode* createOrFindBuffer(uint16_t pid);
    };

}   /* namespace mpegts */ } /* namespace ckavlib */

#endif
