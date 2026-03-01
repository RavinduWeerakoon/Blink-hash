
#include "wal_ring.h"

#include <cstring>
#include <cstdlib>
#include <cassert>
#include <new>

#ifdef __linux__
#include <sys/mman.h>
#elif defined(__APPLE__)
#include <sys/mman.h>
#endif

namespace BLINK_HASH {
namespace WAL {


static bool is_power_of_two(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

/* Block size for commit tracking. */
static constexpr size_t COMMIT_BLOCK = 4096;

/* constructor / destructor */

RingBuffer::RingBuffer(size_t capacity)
    : buf_(nullptr)
    , capacity_(capacity)
    , mask_(capacity - 1)
    , committed_(nullptr)
{
    assert(is_power_of_two(capacity) && "capacity must be power-of-two");
    assert(capacity >= 2 * COMMIT_BLOCK && "need at least 2 blocks");

    /*
     * Allocate ring memory.  We use mmap so we can later add (to get the cpu level page controls)
     * MAP_HUGETLB on Linux.  On macOS mmap is fine (no hugepages).
     */
    void* p = ::mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        std::abort();
    buf_ = static_cast<char*>(p);


    size_t num_slots = capacity / COMMIT_BLOCK;
    committed_ = new std::atomic<uint8_t>[num_slots];
    for (size_t i = 0; i < num_slots; ++i)
        committed_[i].store(0, std::memory_order_relaxed);
}

RingBuffer::~RingBuffer() {
    if (buf_)
        ::munmap(buf_, capacity_);
    delete[] committed_;
}

/* Producer: reserve*/

uint64_t RingBuffer::reserve(size_t size) {
 
    uint64_t ticket;
    for (;;) {
        ticket = write_head_.fetch_add(size, std::memory_order_acq_rel);
        uint64_t rh = read_head_.load(std::memory_order_acquire);
        if (ticket + size - rh <= capacity_)
            break;                 

     
        write_head_.fetch_sub(size, std::memory_order_acq_rel);
        /* Yield to let the flusher make progress */
#if defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
#endif
    }
    return ticket;
}

/* Producer: commit  */

void RingBuffer::commit(uint64_t offset, size_t size) {
    /*
     * We use store(1, release) so that the preceding memcpy
     * (from ThreadBuf::flush) is visible to the flusher.
     */
    uint64_t first_block = offset / COMMIT_BLOCK;
    uint64_t last_block  = (offset + size - 1) / COMMIT_BLOCK;
    size_t   num_slots   = capacity_ / COMMIT_BLOCK;

    for (uint64_t b = first_block; b <= last_block; ++b) {
        size_t idx = b % num_slots;
        committed_[idx].store(1, std::memory_order_release);
    }

    /*
     * Advance commit_head_ past all consecutively committed blocks.
     * Only one CAS winner per slot contention is low because
     * workers typically write to disjoint blocks.
     */
    uint64_t expected = first_block * COMMIT_BLOCK;
    while (expected == commit_head_.load(std::memory_order_acquire)) {
        size_t idx = (expected / COMMIT_BLOCK) % num_slots;
        if (committed_[idx].load(std::memory_order_acquire) == 0)
            break;

        uint64_t next = expected + COMMIT_BLOCK;
        if (commit_head_.compare_exchange_weak(expected, next,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            committed_[idx].store(0, std::memory_order_release);
            expected = next;
        } else {
            break;  
        }
    }
}

/* Consumer: peek */

size_t RingBuffer::peek(const void** out_ptr) const {
    /*
     * the gap between read_head_ and commit_head_.  The data is guaranteed to be committed
     * (all producers done with their memcpys).
     */
    uint64_t rh = read_head_.load(std::memory_order_acquire);
    uint64_t ch = commit_head_.load(std::memory_order_acquire);

    if (rh >= ch) {
        *out_ptr = nullptr;
        return 0;
    }

    size_t phys_off = static_cast<size_t>(rh & mask_);
    size_t avail    = static_cast<size_t>(ch - rh);

    size_t contig = capacity_ - phys_off;
    if (avail > contig)
        avail = contig;

    *out_ptr = buf_ + phys_off;
    return avail;
}

/*  Consumer: advance  */

void RingBuffer::advance(size_t size) {
    read_head_.fetch_add(size, std::memory_order_release);
}

/* write_at (for ThreadBuf flush)  */

void RingBuffer::write_at(uint64_t abs_offset, const void* data, size_t len) {
    /*
     * Write `len` bytes starting at absolute offset `abs_offset`.
     * Handles wrap-around at the physical end of the buffer.
     */
    size_t phys = static_cast<size_t>(abs_offset & mask_);
    size_t first_part = capacity_ - phys;

    if (len <= first_part) {
        std::memcpy(buf_ + phys, data, len);
    } else {
        std::memcpy(buf_ + phys, data, first_part);
        std::memcpy(buf_, static_cast<const char*>(data) + first_part,
                    len - first_part);
    }
}

} 
} 