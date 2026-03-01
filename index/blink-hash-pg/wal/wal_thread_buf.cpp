

#include "wal_thread_buf.h"
#include "wal_ring.h"
#include <cstring>
#include <cstdlib>      /* posix_memalign, free */
#include <cassert>

namespace BLINK_HASH {
namespace WAL {

/*  helpers*/

static char* alloc_aligned(size_t sz) {
    void* ptr = nullptr;
    int rc = posix_memalign(&ptr, 4096, sz);
    if (rc != 0) {
        /* posix_memalign failed — unrecoverable */
        std::abort();
    }
    return static_cast<char*>(ptr);
}

/* constructors / destructors*/

ThreadBuf::ThreadBuf()
    : buf_(alloc_aligned(THREAD_BUF_SIZE))
    , used_(0)
{}

ThreadBuf::~ThreadBuf() {
    std::free(buf_);
}

ThreadBuf::ThreadBuf(ThreadBuf&& o) noexcept
    : buf_(o.buf_), used_(o.used_)
{
    o.buf_  = nullptr;
    o.used_ = 0;
}

ThreadBuf& ThreadBuf::operator=(ThreadBuf&& o) noexcept {
    if (this != &o) {
        std::free(buf_);
        buf_   = o.buf_;
        used_  = o.used_;
        o.buf_  = nullptr;
        o.used_ = 0;
    }
    return *this;
}

/* append */

bool ThreadBuf::append(const void* data, size_t len) {
    /*
     * If the record is larger than the entire buffer capacity
     * there is no way to buffer it.  The caller should flush
     * the record directly to the ring.
     */
    if (len > THREAD_BUF_SIZE)
        return false;

    /*
     * If appending would exceed the buffer, return false.
     * The caller must flush() first and then retry.
     */
    if (used_ + len > THREAD_BUF_SIZE)
        return false;

    std::memcpy(buf_ + used_, data, len);
    used_ += len;
    return true;
}

/* flush / drain */

void ThreadBuf::flush(RingBuffer& ring) {
    if (used_ == 0)
        return;

    /*
     * Reserve a contiguous region in the ring buffer.
     * The reserve() call is a single atomic fetch_add — no lock.
     */
    uint64_t off = ring.reserve(used_);

    /* Copy the thread-local buffer into the ring. */
    /* We cannot just get a raw pointer to the ring buffer because
     * the data might wrap around. reserve() returns an absolute
     * byte offset — the ring buffer implementation handles the
     * modulo addressing internally.  However, our ring buffer
     * exposes the internal buffer via peek(), but for writes
     * we need to address it directly.
     *
     * Approach: the ring gives us an *absolute* offset.
     * We compute the physical offset (off & mask) and
     * handle the wrap-around case with two memcpys.
     */

    /* Because ThreadBuf needs to write into the ring's memory,
     * and the ring's internal buffer is private, we provide a
     * "write_at" method on RingBuffer.  See the ring buffer
     * implementation below — we add a public write_at() method. */

    ring.write_at(off, buf_, used_);
    ring.commit(off, used_);

    used_ = 0;
}

void ThreadBuf::drain(RingBuffer& ring) {
    flush(ring);
}

} 
} 