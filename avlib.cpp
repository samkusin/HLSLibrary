/**
 *  @file       avlib.hpp
 *  @brief      Common admin methods for the AV Library
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#include "avlib.hpp"

#include <cstdlib>

namespace cinekav {

static void* DefaultMalloc(void*, int, size_t sz)
{
    return malloc(sz);
}

static void DefaultFree(void*, int, void* ptr)
{
    free(ptr);
}

static AllocFn gAllocFn = &DefaultMalloc;
static FreeFn gFreeFn = &DefaultFree;
static void* gMemoryContext = nullptr;

void* Memory::allocate(size_t sz)
{
    return gAllocFn(gMemoryContext, _region, sz);
}

void Memory::free(void* ptr)
{
    gFreeFn(gMemoryContext, _region, ptr);
}

void initialize(AllocFn allocFn, FreeFn freeFn, void* context)
{
    gAllocFn = allocFn;
    gFreeFn = freeFn;
    gMemoryContext = context;
}

////////////////////////////////////////////////////////////////////////////////

Buffer::Buffer(const Memory& memory) :
    _memory(memory),
    _buffer(nullptr),
    _head(nullptr),
    _tail(nullptr),
    _limit(nullptr),
    _overflow(false),
    _owned(false)
{
}

Buffer::Buffer(int sz, const Memory& memory) :
    _memory(memory),
    _buffer(nullptr),
    _head(nullptr),
    _tail(nullptr),
    _limit(nullptr),
    _overflow(false),
    _owned(true)
{
    if (_owned)
    {
        _buffer = reinterpret_cast<uint8_t*>(_memory.allocate(sz));
        if (_buffer)
        {
            _limit = _buffer + sz;
        }
        _head = _buffer;
        _tail = _head;
    }
}

Buffer::Buffer(uint8_t* buffer, int sz) :
    _buffer(buffer),
    _head(_buffer),
    _tail(_buffer + sz),
    _limit(_tail),
    _overflow(false),
    _owned(false)
{
}

Buffer::Buffer(uint8_t* buffer, int sz, int limit) :
    _buffer(buffer),
    _head(_buffer),
    _tail(_buffer + sz),
    _limit(_buffer + (sz > limit ? sz : limit)),
    _overflow(false),
    _owned(false)
{
}

Buffer::~Buffer()
{
    if (_owned)
    {
        _memory.free(_buffer);
        _buffer = nullptr;
    }
}

Buffer::Buffer(Buffer&& other) :
    _buffer(other._buffer),
    _head(other._head),
    _tail(other._tail),
    _limit(other._limit),
    _overflow(other._overflow),
    _owned(other._owned)
{
    other._buffer = nullptr;
    other._head = nullptr;
    other._tail = nullptr;
    other._limit = nullptr;
    other._overflow = false;
    other._owned = false;
}

Buffer& Buffer::operator=(Buffer&& other)
{
    if (_owned && _buffer)
    {
        _memory.free(_buffer);
    }
    _buffer = other._buffer;
    _head = other._head;
    _tail = other._tail;
    _limit = other._limit;
    _overflow = other._overflow;
    _owned = other._owned;

    other._buffer = nullptr;
    other._head = nullptr;
    other._tail = nullptr;
    other._limit = nullptr;
    other._overflow = false;
    other._owned = false;

    return *this;
}

void Buffer::reset()
{
    _head = _tail = _buffer;
}


int Buffer::pushBytes(const uint8_t* bytes, int cnt)
{
    if (available() < cnt)
        cnt = available();

    memcpy(_tail, bytes, cnt);
    _tail += cnt;
    return cnt;
}

#if CINEK_AVLIB_IOSTREAMS
int Buffer::pushBytesFromStream(std::basic_istream<char>& istr, int cnt)
{
    if (available() < cnt)
        cnt = available();

    istr.read((char*)_tail, cnt);
    if (istr.eof())
    {
        cnt = istr.gcount();
    }
    else if (istr.fail())
    {
        return -1;
    }
    _tail += cnt;
    return cnt;
}
#endif

Buffer& Buffer::pullBytesFrom(Buffer& source, int cnt, int* pulled)
{
    //  clip cnt to the intersection of this buffer and the target's.
    if (cnt > source.size())
        cnt = source.size();
    if (available() < cnt)
        cnt = available();

    if (cnt > 0)
    {
        memcpy(_tail, source._head, cnt);
        _tail += cnt;
        source._head += cnt;
    }
    if (pulled)
    {
        *pulled = cnt;
    }
    return *this;
}

//  Generates a subuffer from the available space of the owning buffer.
//  This is a buffer composed of [tail+offset, tail+offset+sz]
//  If either the buffer offset or size result in a buffer falling outside
//  this buffer's memory region, the returned buffer will reflect the
//  difference
Buffer Buffer::createSubBuffer(int offset, int sz)
{
    uint8_t* buffer = _tail + offset;
    if (buffer > _limit)
        return Buffer(_limit, 0);
    if (buffer + sz > _limit)
        sz = _limit - buffer;

    return Buffer(buffer, 0, sz);
}

Buffer Buffer::createSubBufferFromUsed()
{
    return Buffer(_head, _tail - _head);
}

StringBuffer::StringBuffer()
{
}

StringBuffer::StringBuffer(int sz, const Memory& memory) :
    _buffer(sz, memory)
{
}

StringBuffer::StringBuffer(Buffer&& buffer) :
    _buffer(std::move(buffer))
{
}


//  skips null characters if delim != 0.
//  else terminates on delim or end of buffer.
StringBuffer& StringBuffer::getline(std::string& str, char delim)
{
    str.clear();
    while (!_buffer.empty())
    {
        char ch = (char)_buffer.pullByte();
        if (ch == delim)
            break;
        str.push_back(ch);
    }

    return *this;
}

bool StringBuffer::end() const
{
    return _buffer.empty();
}



} /* namespace ckavlib */