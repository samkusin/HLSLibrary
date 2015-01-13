/**
 *  @file       main.cpp
 *  @brief      Sample application for the AV Library
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "mpegts.hpp"

#include <istream>
#include <fstream>
#include <string>

using namespace cinekav::mpegts;

int main(int argc, const char* argv[])
{
    Demuxer tsdemux;
    
    std::ifstream input("fileSequence0.ts", std::ios::binary);
    
    Demuxer::Result result = tsdemux.read(input);
    
    for (int i = 0; i < tsdemux.numPrograms(); ++i)
    {
        const Program& program = tsdemux.program(i);
        auto stream = program.firstStream();
        while (stream)
        {
            std::string outName = "stream";
            outName += std::to_string(stream->index());
            outName += ".out";
            std::ofstream output(outName, std::ios::binary);
            stream->write(output);
            stream = stream->prevStream();
        }
    }
    return 0;
}