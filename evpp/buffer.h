#pragma once

#include "evpp/inner_pre.h"
#include "evpp/slice.h"
#include "evpp/sockets.h"

#include <algorithm>

namespace evpp {
    class EVPP_EXPORT Buffer {
    public:
        static const size_t kCheapPrepend;
        static const size_t kInitialSize;

        explicit Buffer(size_t initial_size = kInitialSize)
            : capacity_(kCheapPrepend + initial_size),
            read_index_(kCheapPrepend),
            write_index_(kCheapPrepend) {
            buffer_ = new char[capacity_];
            assert(length() == 0);
            assert(WritableBytes() == initial_size);
            assert(PrependableBytes() == kCheapPrepend);
        }

        ~Buffer() {
            delete[] buffer_;
            buffer_ = NULL;
            capacity_ = 0;
        }

        void Swap(Buffer& rhs) {
            std::swap(buffer_, rhs.buffer_);
            std::swap(capacity_, rhs.capacity_);
            std::swap(read_index_, rhs.read_index_);
            std::swap(write_index_, rhs.write_index_);
        }

        // Skip advances the reading index of the buffer
        void Skip(size_t len) {
            if (len < length()) {
                read_index_ += len;
            } else {
                Reset();
            }
        }

        // Retrieve advances the reading index of the buffer
        // Retrieve it the same as Skip.
        void Retrieve(size_t len) {
            Skip(len);
        }

        // Truncate discards all but the first n unread bytes from the buffer
        // but continues to use the same allocated storage.
        // It does nothing if n is greater than the length of the buffer.
        void Truncate(size_t n) {
            if (n == 0) {
                read_index_ = kCheapPrepend;
                write_index_ = kCheapPrepend;
            } else if (write_index_ > read_index_ + n) {
                write_index_ = read_index_ + n;
            }
        }

        // Reset resets the buffer to be empty,
        // but it retains the underlying storage for use by future writes.
        // Reset is the same as Truncate(0).
        void Reset() {
            Truncate(0);
        }

        // Increase the capacity of the container to a value that's greater
        // or equal to len. If len is greater than the current capacity(),
        // new storage is allocated, otherwise the method does nothing.
        void Reserve(size_t len) {
            if (capacity_ >= len + kCheapPrepend) {
                return;
            }

            // TODO add the implementation logic here
            grow(len + kCheapPrepend);
        }


        // TODO XXX Little-Endian/Big-Endian problem.
#define bswap_64(x)                                     \
            ((((x) & 0xff00000000000000ull) >> 56)      \
            | (((x) & 0x00ff000000000000ull) >> 40)     \
            | (((x) & 0x0000ff0000000000ull) >> 24)     \
            | (((x) & 0x000000ff00000000ull) >> 8)      \
            | (((x) & 0x00000000ff000000ull) << 8)      \
            | (((x) & 0x0000000000ff0000ull) << 24)     \
            | (((x) & 0x000000000000ff00ull) << 40)     \
            | (((x) & 0x00000000000000ffull) << 56))

        // Write
    public:
        void Write(const void* /*restrict*/ d, size_t len) {
            ensureWritableBytes(len);
            memcpy(beginWrite(), d, len);
            assert(write_index_ + len <= capacity_);
            write_index_ += len;
        }

        void Append(const Slice& str) {
            Write(str.data(), str.size());
        }

        void Append(const char* /*restrict*/ d, size_t len) {
            Write(d, len);
        }

        void Append(const void* /*restrict*/ d, size_t len) {
            Write(d, len);
        }

        // Append int64_t/int32_t/int16_t with network endian
        void AppendInt64(int64_t x) {
            int64_t be = bswap_64(x);
            Write(&be, sizeof be);
        }

        void AppendInt32(int32_t x) {
            int32_t be32 = ::htonl(x);
            Write(&be32, sizeof be32);
        }

        void AppendInt16(int16_t x) {
            int16_t be16 = ::htons(x);
            Write(&be16, sizeof be16);
        }

        void AppendInt8(int8_t x) {
            Write(&x, sizeof x);
        }

        // Prepend int64_t/int32_t/int16_t with network endian
        void PrependInt64(int64_t x) {
            int64_t be = bswap_64(x);
            Prepend(&be, sizeof be);
        }

        void PrependInt32(int32_t x) {
            int32_t be32 = ::htonl(x);
            Prepend(&be32, sizeof be32);
        }

        void PrependInt16(int16_t x) {
            int16_t be16 = ::htons(x);
            Prepend(&be16, sizeof be16);
        }

        void PrependInt8(int8_t x) {
            Prepend(&x, sizeof x);
        }

        // Insert content, specified by the parameter, into the front of reading index
        void Prepend(const void* /*restrict*/ d, size_t len) {
            assert(len <= PrependableBytes());
            read_index_ -= len;
            const char* p = static_cast<const char*>(d);
            memcpy(begin() + read_index_, p, len);
        }

        void UnwriteBytes(size_t n) {
            assert(n <= length());
            write_index_ -= n;
        }


        //Read
    public:
        // Peek int64_t/int32_t/int16_t/int8_t with network endian
        int64_t ReadInt64() {
            int64_t result = PeekInt64();
            Skip(sizeof result);
            return result;
        }

        int32_t ReadInt32() {
            int32_t result = PeekInt32();
            Skip(sizeof result);
            return result;
        }

        int16_t ReadInt16() {
            int16_t result = PeekInt16();
            Skip(sizeof result);
            return result;
        }

        int8_t ReadInt8() {
            int8_t result = PeekInt8();
            Skip(sizeof result);
            return result;
        }

        Slice ToSlice() const {
            return Slice(data(), static_cast<int>(length()));
        }

        void Shrink(size_t reserve) {
            Buffer other(length() + reserve);
            other.Append(ToSlice());
            Swap(other);
        }

        /// Read data directly into buffer.
        ///
        /// It may implement with readv(2)
        /// @return result of read(2), @c errno is saved
        ssize_t ReadFromFD(int fd, int* savedErrno);

        // Next returns a slice containing the next n bytes from the buffer,
        // advancing the buffer as if the bytes had been returned by Read.
        // If there are fewer than n bytes in the buffer, Next returns the entire buffer.
        // The slice is only valid until the next call to a read or write method.
        Slice Next(size_t len) {
            if (len < length()) {
                Slice result(data(), len);
                read_index_ += len;
                return result;
            }
            return NextAll();
        }


        // NextAll returns a slice containing all the unread portion of the buffer,
        // advancing the buffer as if the bytes had been returned by Read.
        Slice NextAll() {
            Slice result(data(), length());
            Reset();
            return result;
        }

        std::string NextString(size_t len) {
            Slice s = Next(len);
            return std::string(s.data(), s.size());
        }

        std::string NextAllString() {
            Slice s = NextAll();
            return std::string(s.data(), s.size());
        }

        // ReadByte reads and returns the next byte from the buffer.
        // If no byte is available, it returns '\0'.
        char ReadByte() {
            assert(length() >= 1);
            if (length() == 0) {
                return '\0';
            }
            return buffer_[read_index_++];
        }

        // UnreadBytes unreads the last n bytes returned
        // by the most recent read operation.
        void UnreadBytes(size_t n) {
            assert(n < read_index_);
            read_index_ -= n;
        }

        // Peek
    public:
        // Peek int64_t/int32_t/int16_t/int8_t with network endian

        int64_t PeekInt64() const {
            assert(length() >= sizeof(int64_t));
            int64_t be64 = 0;
            ::memcpy(&be64, data(), sizeof be64);
            return bswap_64(be64);
        }

        int32_t PeekInt32() const {
            assert(length() >= sizeof(int32_t));
            int32_t be32 = 0;
            ::memcpy(&be32, data(), sizeof be32);
            return ::ntohl(be32);
        }

        int16_t PeekInt16() const {
            assert(length() >= sizeof(int16_t));
            int16_t be16 = 0;
            ::memcpy(&be16, data(), sizeof be16);
            return ::ntohs(be16);
        }

        int8_t PeekInt8() const {
            assert(length() >= sizeof(int8_t));
            int8_t x = *data();
            return x;
        }

    public:
        // data returns a pointer of length Buffer.length() holding the unread portion of the buffer.
        // The data is valid for use only until the next buffer modification (that is,
        // only until the next call to a method like Read, Write, Reset, or Truncate).
        // The data aliases the buffer content at least until the next buffer modification,
        // so immediate changes to the slice will affect the result of future reads.
        const char* data() const {
            return buffer_ + read_index_;
        }

        // length returns the number of bytes of the unread portion of the buffer
        size_t length() const {
            assert(write_index_ >= read_index_);
            return write_index_ - read_index_;
        }

        // size returns the number of bytes of the unread portion of the buffer.
        // It is the same as length().
        size_t size() const {
            return length();
        }

        // capacity returns the capacity of the buffer's underlying byte slice, that is, the
        // total space allocated for the buffer's data.
        size_t capacity() const {
            return capacity_;
        }


        size_t WritableBytes() const {
            assert(capacity_ >= write_index_);
            return capacity_ - write_index_;
        }

        size_t PrependableBytes() const {
            return read_index_;
        }

        // Helpers
    public:
        const char* FindCRLF() const {
            const char* crlf = std::search(data(), beginWrite(), kCRLF, kCRLF + 2);
            return crlf == beginWrite() ? NULL : crlf;
        }

        const char* FindCRLF(const char* start) const {
            assert(data() <= start);
            assert(start <= beginWrite());
            const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
            return crlf == beginWrite() ? NULL : crlf;
        }

        const char* FindEOL() const {
            const void* eol = memchr(data(), '\n', length());
            return static_cast<const char*>(eol);
        }

        const char* FindEOL(const char* start) const {
            assert(data() <= start);
            assert(start <= beginWrite());
            const void* eol = memchr(start, '\n', beginWrite() - start);
            return static_cast<const char*>(eol);
        }
    private:
        char* beginWrite() {
            return begin() + write_index_;
        }

        const char* beginWrite() const {
            return begin() + write_index_;
        }

        char* begin() {
            return buffer_;
        }

        const char* begin() const {
            return buffer_;
        }

        void ensureWritableBytes(size_t len) {
            if (WritableBytes() < len) {
                grow(len);
            }
            assert(WritableBytes() >= len);
        }

        void grow(size_t len) {
            if (WritableBytes() + PrependableBytes() < len + kCheapPrepend) {
                //grow the capacity
                size_t n = (capacity_ << 1) + len;
                size_t m = length();
                char* d = new char[n];
                memcpy(d + kCheapPrepend, begin() + read_index_, m);
                write_index_ = m + kCheapPrepend;
                read_index_ = kCheapPrepend;
                capacity_ = n;
                delete[] buffer_;
                buffer_ = d;
            } else {
                // move readable data to the front, make space inside buffer
                assert(kCheapPrepend < read_index_);
                size_t readable = length();
                memcpy(begin() + kCheapPrepend, begin() + read_index_, length());
                read_index_ = kCheapPrepend;
                write_index_ = read_index_ + readable;
                assert(readable == length());
                assert(WritableBytes() >= len);
            }
        }

    private:
        char* buffer_;
        size_t capacity_;
        size_t read_index_;
        size_t write_index_;

        static const char kCRLF[];
    };

}
