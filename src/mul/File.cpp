#include "uo/mul.h"

#include <cstdio>
#include <sys/stat.h>

namespace uo::mul {

File::File() : f_(nullptr), size_(0) {}
File::~File() { Close(); }

bool File::Open(const char* path) {
    Close();
    f_ = std::fopen(path, "rb");
    if (!f_) return false;

    struct _stat32 st{};
    if (::_stat32(path, &st) == 0) {
        size_ = static_cast<u64>(st.st_size);
    } else {
        size_ = 0;
    }
    return true;
}

void File::Close() {
    if (f_) {
        std::fclose(f_);
        f_ = nullptr;
    }
    size_ = 0;
}

bool File::Read(void* dst, usize n) {
    if (!f_) return false;
    return std::fread(dst, 1, n, f_) == n;
}

bool File::Seek(i64 offset, int whence) {
    if (!f_) return false;
    return std::fseek(f_, static_cast<long>(offset), whence) == 0;
}

u64 File::Tell() const {
    if (!f_) return 0;
    return static_cast<u64>(std::ftell(f_));
}

}
