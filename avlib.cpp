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

static void* DefaultMalloc(void*, size_t sz)
{
    return malloc(sz);
}

static void DefaultFree(void*, void* ptr)
{
    free(ptr);
}

static AllocFn gAllocFn = &DefaultMalloc;
static FreeFn gFreeFn = &DefaultFree;
static void* gMemoryContext = nullptr;

void* Memory::allocate(size_t sz)
{
    return gAllocFn(gMemoryContext, sz);
}

void Memory::free(void* ptr)
{
    gFreeFn(gMemoryContext, ptr);
}

void initialize(AllocFn allocFn, FreeFn freeFn, void* context)
{
    gAllocFn = allocFn;
    gFreeFn = freeFn;
    gMemoryContext = context;
}

////////////////////////////////////////////////////////////////////////////////

Buffer::Buffer() :
    _buffer(nullptr),
    _head(nullptr),
    _tail(nullptr),
    _limit(nullptr),
    _overflow(false),
    _owned(false)
{
}

Buffer::Buffer(int sz) :
    _buffer(nullptr),
    _head(nullptr),
    _tail(nullptr),
    _limit(nullptr),
    _overflow(false),
    _owned(true)
{
    if (_owned)
    {
        _buffer = reinterpret_cast<uint8_t*>(Memory::allocate(sz));
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

Buffer::~Buffer()
{
    if (_owned)
    {
        Memory::free(_buffer);
        _buffer = nullptr;
    }
}

Buffer::Buffer(Buffer&& other) :
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
        Memory::free(_buffer);
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

Buffer Buffer::mapBuffer(int head, int tail) const
{
    if (tail > _limit - _tail)
        tail = _limit - _tail;
    if (head > tail)
        head = tail;
    if (!_head)
    {
        head = tail = 0;
    }
    return Buffer(_head + head, tail - head);
}

int Buffer::pushBytes(const uint8_t* bytes, int cnt)
{
    if (writeAvailable() < cnt)
        cnt = writeAvailable();

    memcpy(_tail, bytes, cnt);
    _tail += cnt;
    return cnt;
}

int Buffer::pushBytesFromStream(std::basic_istream<char>& istr, int cnt)
{
    if (writeAvailable() < cnt)
        cnt = writeAvailable();

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

Buffer& Buffer::pullBytesInto(Buffer& target, int cnt, int* pulled)
{
    //  clip cnt to the intersection of this buffer and the target's.
    if (cnt > available())
        cnt = available();
    if (target.writeAvailable() < cnt)
        cnt = target.writeAvailable();

    if (cnt > 0)
    {
        memcpy(target._tail, _head, cnt);
        target._tail += cnt;
        _head += cnt;
    }
    if (pulled)
    {
        *pulled = cnt;
    }
    return *this;
}

} /* namespace ckavlib */