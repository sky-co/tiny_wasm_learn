#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

class ExecutableBuffer {
  public:
    ExecutableBuffer() = default;
    ~ExecutableBuffer() {
        Release();
    }

    ExecutableBuffer(const ExecutableBuffer &) = delete;
    ExecutableBuffer &operator=(const ExecutableBuffer &) = delete;

    ExecutableBuffer(ExecutableBuffer &&other) noexcept : data_(other.data_), size_(other.size_), used_(other.used_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.used_ = 0;
    }

    ExecutableBuffer &operator=(ExecutableBuffer &&other) noexcept {
        if (this != &other) {
            Release();
            data_ = other.data_;
            size_ = other.size_;
            used_ = other.used_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.used_ = 0;
        }
        return *this;
    }

    bool Allocate(size_t bytes) {
        Release();
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0 || bytes == 0U) {
            return false;
        }

        const size_t page = static_cast<size_t>(page_size);
        size_ = ((bytes + page - 1U) / page) * page;
        void *const mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            size_ = 0;
            return false;
        }

        data_ = static_cast<uint8_t *>(mem);
        used_ = 0;
        return true;
    }

    uint8_t *Emit(const std::vector<uint8_t> &machine) {
        const size_t aligned = (used_ + 15U) & ~15U;
        if (data_ == nullptr || machine.empty() || aligned + machine.size() > size_) {
            return nullptr;
        }

        uint8_t *const entry = data_ + aligned;
        std::memcpy(entry, machine.data(), machine.size());
        used_ = aligned + machine.size();
        return entry;
    }

    bool ProtectExecutable() {
        if (data_ == nullptr || mprotect(data_, size_, PROT_READ | PROT_EXEC) != 0) {
            return false;
        }
        char *const start = static_cast<char *>(static_cast<void *>(data_));
        __builtin___clear_cache(start, start + size_);
        return true;
    }

  private:
    void Release() {
        if (data_ != nullptr) {
            munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
            used_ = 0;
        }
    }

    uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t used_ = 0;
};
