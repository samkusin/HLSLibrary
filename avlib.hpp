/**
 *  @file       avlib.hpp
 *  @brief      Common admin methods for the AV Library
 *
 *  @copyright  Copyright 2015 Samir Sinha.  All rights reserved.
 *  @license    This project is released under the ISC license.  See LICENSE
 *              for the full text.
 */

#ifndef CINEK_AVLIB_HPP
#define CINEK_AVLIB_HPP

#include "avdefs.hpp"

#if CINEK_AVLIB_IOSTREAMS
#include <streambuf>
#include <istream>
#endif
#if CINEK_AVLIB_EXCEPTIONS
#include <stdexcept>
#endif

namespace cinekav {

typedef void* (*AllocFn)(void* context, int region, size_t sz);
typedef void (*FreeFn)(void* context, int region, void* ptr);

void initialize(AllocFn allocFn, FreeFn freeFn, void* context);

struct Memory
{
    Memory() : _region(0) {}
    Memory(int region) : _region(region) {}

    void* allocate(size_t sz);
    void free(void* ptr);

    template<typename T, typename... Args>
    T* create(Args&&... args)
    {
        T* p = reinterpret_cast<T*>(allocate(sizeof(T)));
        ::new(p) T(std::forward<Args>(args)...);
        return p;
    }

    template<typename T>
    void destroy(T* ptr)
    {
        ptr->~T();
        free(ptr);
    }

private:
    friend bool operator==(const Memory& lha, const Memory& rha);
    friend bool operator!=(const Memory& lha, const Memory& rha);
    int _region;
};

inline bool operator==(const Memory& lha, const Memory& rha)
{
    return (lha._region == rha._region);
}
inline bool operator!=(const Memory& lha, const Memory& rha)
{
    return (lha._region != rha._region);
}


class StringBuffer;

class Buffer
{
public:
    Buffer(const Memory& memory=Memory());
    Buffer(size_t sz, const Memory& memory=Memory());
    Buffer(uint8_t* buffer, size_t sz);
    Buffer(uint8_t* buffer, size_t sz, size_t limit);
    ~Buffer();

    Buffer(const Buffer& other) = delete;
    Buffer& operator=(const Buffer& other) = delete;

    Buffer(Buffer&& other);
    Buffer& operator=(Buffer&& other);

    operator bool() const {
        return _buffer != nullptr;
    }

    bool empty() const { return _head == _tail; }
    bool overflow() const { return _overflow; }

    size_t pushBytes(const uint8_t* bytes, size_t sz);

#if CINEK_AVLIB_IOSTREAMS
    size_t pushBytesFromStream(std::basic_istream<char>& istr, size_t cnt);
#endif
    void reset();

    Buffer& pullBytesFrom(Buffer& target, size_t cnt, size_t* pulled);

    uint8_t pullByte() {
        _overflow = _overflow || (_head == _tail);
        if (_overflow)
            return 0;
        return *(_head++);
    }
    uint16_t pullUInt16() {
        uint16_t s = pullByte();
        s <<= 8;
        s |= pullByte();
        return s;
    }
    uint32_t pullUInt32() {
        uint32_t ui = pullUInt16();
        ui <<= 16;
        ui |= pullUInt16();
        return ui;
    }
    void skip(size_t cnt) {
        _head += cnt;
        _overflow = _overflow || (_head > _tail);
        if (_head > _tail)
            _head = _tail;
    }
    size_t  headAvailable() const {
        return _head - _buffer;
    }
    size_t size() const {
        return _tail - _head;
    }
    size_t available() const {
        return _limit - _tail;
    }
    size_t capacity() const {
        return _limit - _buffer;
    }
    const uint8_t* head() const {
        return _head;
    }
    const uint8_t* tail() const {
        return _tail;
    }

    uint8_t* obtain(size_t sz) {
        if (_tail+sz > _limit)
            return nullptr;
        uint8_t* tail = _tail;
        _tail += sz;
        return tail;
    }

    //  Generates a subuffer from the available space of the owning buffer.
    //  This is a buffer composed of [tail+offset, tail+offset+sz]
    //  If either the buffer offset or size result in a buffer falling outside
    //  this buffer's memory region, the returned buffer will reflect the
    //  difference
    Buffer createSubBuffer(size_t offset, size_t sz);
    //  creates a buffer mapped to the memory from head to tail of this buffer.
    Buffer createSubBufferFromUsed();
private:
    friend class StringBuffer;
    Memory _memory;
    uint8_t* _buffer;
    uint8_t* _head;
    uint8_t* _tail;
    uint8_t* _limit;
    bool _overflow;
    bool _owned;
};

class StringBuffer
{
public:
    StringBuffer();
    StringBuffer(size_t sz, const Memory& memory=Memory());
    StringBuffer(Buffer&& buffer);

    //  skips null characters if delim != 0.
    //  else terminates on delim or end of buffer.
    StringBuffer& getline(std::string& str, char delim='\n');

    bool end() const;

private:
    Buffer _buffer;
};


/**
 * @class std_allocator
 * @brief A std::allocator compliant allocator for STL containers.
 */
template <typename T, class Allocator=Memory>
struct std_allocator
{
    /** @cond */
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T value_type;

    template <class U> struct rebind {
        typedef std_allocator<U, Allocator> other;
    };

    std_allocator() {}
    std_allocator(const Allocator& allocator): _allocator(allocator) {}
    std_allocator(const std_allocator& source): _allocator(source._allocator) {}
    template <class U> std_allocator(const std_allocator<U, Allocator>& source): _allocator(source._allocator) {}

    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }

    pointer allocate(size_type s, const void* = 0) {
        if (s == 0)
            return nullptr;
        pointer temp = static_cast<pointer>(_allocator.allocate(s*sizeof(T)));
    #if CINEK_AVLIB_EXCEPTIONS
        if (temp == nullptr)
            throw std::bad_alloc();
    #endif
        return temp;
    }

    void deallocate(pointer p, size_type) {
    	_allocator.free((void* )p);
    }
    size_type max_size() const {
        return std::numeric_limits<size_t>::max() / sizeof(T);
    }
    template<class U, class... Args> void construct(U* p, Args&&... args) {
        ::new((void *)p) U(std::forward<Args>(args)...);
    }
    void destroy(pointer p) {
        p->~T();
    }

    Allocator _allocator;
    /** @endcond */
};

template<typename T, class Allocator>
inline bool operator==(const std_allocator<T, Allocator>& lha,
                        const std_allocator<T, Allocator>& rha)
{
    return lha._allocator == rha._allocator;
}
template<typename T, class Allocator>
inline bool operator!=(const std_allocator<T, Allocator>& lha,
                        const std_allocator<T, Allocator>& rha)
{
    return lha._allocator != rha._allocator;
}

}   /* namespace cinekav */

#endif
