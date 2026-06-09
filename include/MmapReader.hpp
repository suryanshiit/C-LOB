#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <string>

class MmapReader {
private:
    int m_fd{-1};
    size_t m_file_size{0};
    char* m_mapped_data{nullptr};

public:
    explicit MmapReader(const std::string& filepath) {
        // 1. Open the file directly via the OS
        m_fd = ::open(filepath.c_str(), O_RDONLY);
        if (m_fd < 0) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        // 2. Get the exact file size
        struct stat sb;
        if (::fstat(m_fd, &sb) < 0) {
            ::close(m_fd);
            throw std::runtime_error("Failed to get file stats.");
        }
        m_file_size = sb.st_size;

        // 3. Map the file to virtual memory
        m_mapped_data = static_cast<char*>(::mmap(
            nullptr, m_file_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, m_fd, 0
        ));

        if (m_mapped_data == MAP_FAILED) {
            ::close(m_fd);
            throw std::runtime_error("mmap failed.");
        }
    }

    ~MmapReader() {
        if (m_mapped_data && m_mapped_data != MAP_FAILED) {
            ::munmap(m_mapped_data, m_file_size);
        }
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    [[nodiscard]] const char* data() const { return m_mapped_data; }
    [[nodiscard]] size_t size() const { return m_file_size; }
};