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
#include <istream>
#endif
#if CINEK_AVLIB_EXCEPTIONS
#include <stdexcept>
#endif

namespace cinekav {

typedef void* (*AllocFn)(void* context, size_t sz);
typedef void (*FreeFn)(void* context, void* ptr);

void initialize(AllocFn allocFn, FreeFn freeFn, void* context);

struct Memory
{
    static void* allocate(size_t sz);
    static void free(void* ptr);
    
    template<typename T, typename... Args>
    static T* create(Args&&... args)
    {
        T* p = reinterpret_cast<T*>(allocate(sizeof(T)));
        ::new(p) T(std::forward<Args>(args)...);
        return p;
    }
    
    template<typename T>
    static void destroy(T* ptr)
    {
        ptr->~T();
        free(ptr);
    }
};

class Buffer
{
public:
    Buffer();
    Buffer(int sz);
    Buffer(uint8_t* buffer, int sz);
    ~Buffer();

    Buffer(const Buffer& other) = delete;
    Buffer& operator=(const Buffer& other) = delete;

    Buffer(Buffer&& other);
    Buffer& operator=(Buffer&& other);
    
    Buffer mapBuffer(int offset, int limit) const;

    operator bool() const {
        return _buffer != nullptr;
    }

    bool empty() const { return _head == _tail; }
    bool overflow() const { return _overflow; }

    int pushBytes(const uint8_t* bytes, int sz);
    int pushBytesFromStream(std::basic_istream<char>& istr, int cnt);

    void reset();

    Buffer& pullBytesInto(Buffer& target, int cnt, int* pulled);

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
    void skip(uint32_t cnt) {
        _head += cnt;
        _overflow = _overflow || (_head > _tail);
        if (_head > _tail)
            _head = _tail;
    }
    int used() const {
        return _head - _buffer;
    }
    int available() const {
        return _tail - _head;
    }
    int writeAvailable() const {
        return _limit - _tail;
    }
    int capacity() const {
        return _limit - _buffer;
    }
    
    const uint8_t* head() {
        return _head;
    }

private:
    uint8_t* _buffer;
    uint8_t* _head;
    uint8_t* _tail;
    uint8_t* _limit;
    bool _overflow;
    bool _owned;
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
    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }

    pointer allocate(size_type s, const void* = 0) {
        if (s == 0)
            return nullptr;
        pointer temp = static_cast<pointer>(Allocator::allocate(s*sizeof(T)));
    #if CINEK_AVLIB_EXCEPTIONS
        if (temp == nullptr)
            throw std::bad_alloc();
    #endif
        return temp;
    }
    
    void deallocate(pointer p, size_type) {
    	Allocator::free((void* )p);
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
    /** @endcond */
};

}   /* namespace cinekav */

#endif
