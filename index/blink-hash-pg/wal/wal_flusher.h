#ifndef BLINK_HASH_WAL_FLUSHER_H__
#define BLINK_HASH_WAL_FLUSHER_H__



#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include <thread>

namespace BLINK_HASH {
namespace WAL {

class RingBuffer;    

constexpr size_t   FLUSH_BATCH_MAX       = 256 * 1024;  /* 256 KB */
constexpr size_t   WAL_SEGMENT_SIZE      = 64ULL * 1024 * 1024;  /* 64 MB */
constexpr size_t   WRITE_ALIGNMENT       = 4096;  /* NVMe / O_DIRECT */

class Flusher {
public:

    Flusher(const std::string& wal_dir, RingBuffer& ring);
    ~Flusher();

  
    Flusher(const Flusher&) = delete;
    Flusher& operator=(const Flusher&) = delete;

  
    void start();


    void stop();

    /*
     * The most recent LSN that has been durably written to disk.
     * Workers compare their record's LSN against this to decide
     * whether a commit is durable.
     */
    uint64_t flushed_lsn() const {
        return flushed_lsn_.load(std::memory_order_acquire);
    }

private:

    void run();

    /* WAL segment file management  */
    void open_segment(uint64_t segment_id);
    void rotate_segment();

    /*  I/O backend */
    void write_batch(const void* data, size_t len);
    void sync_current_segment();

    /* io_uring state (Linux only) */
#ifdef __linux__
    void init_io_uring();
    void submit_uring_write(const void* data, size_t len, uint64_t offset);
    void wait_uring_completion();
    void* uring_;          
#endif

    /* ── state ── */
    std::string   wal_dir_;
    RingBuffer&   ring_;

    int           fd_;                   /* current segment fd       */
    uint64_t      segment_id_;           /* current segment number   */
    uint64_t      segment_offset_;       /* write offset in segment  */
    char*         write_buf_;            /* 4-KB aligned staging buf */

    std::thread           thread_;
    std::atomic<bool>     running_{false};
    alignas(64) std::atomic<uint64_t> flushed_lsn_{0};
};

} 
} 

#endif 
