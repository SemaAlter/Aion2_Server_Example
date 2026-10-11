#include "sema/frame_codec.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sema {
namespace {

bool read_leb128(std::span<const std::uint8_t> source, std::uint32_t& value, std::size_t& bytes_read) {
    value = 0;
    bytes_read = 0;

    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (bytes_read >= source.size()) {
            return false;
        }

        const auto current = source[bytes_read++];
        if (shift == 28 && (current & 0xF0U) != 0) {
            throw std::runtime_error("AION2 frame length overflows a 32-bit unsigned integer");
        }

        value |= static_cast<std::uint32_t>(current & 0x7FU) << shift;
        if ((current & 0x80U) == 0) {
            return true;
        }
    }

    throw std::runtime_error("AION2 frame length has an invalid LEB128 prefix");
}

void write_leb128(std::uint32_t value, std::vector<std::uint8_t>& output) {
    do {
        auto current = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7;
        if (value != 0) {
            current |= 0x80U;
        }
        output.push_back(current);
    } while (value != 0);
}

} // namespace

frame_decoder::frame_decoder(const std::size_t maximum_body_length)
    : maximum_body_length_(maximum_body_length) {
    buffer_.reserve(4096);
}

void frame_decoder::append(const std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return;
    }

    if (start_ > 0 && start_ * 2 >= buffer_.size()) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(start_));
        start_ = 0;
    }
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

bool frame_decoder::try_read(frame& output) {
    const auto available = std::span<const std::uint8_t>(buffer_).subspan(start_);
    std::uint32_t declared_length = 0;
    std::size_t prefix_length = 0;
    if (!read_leb128(available, declared_length, prefix_length)) {
        return false;
    }
    if (declared_length < 4) {
        throw std::runtime_error("AION2 frame length is below the protocol minimum");
    }

    const auto body_length = static_cast<std::size_t>(declared_length - 4);
    if (body_length > maximum_body_length_) {
        throw std::runtime_error("AION2 frame body exceeds the configured limit");
    }
    if (available.size() < prefix_length + body_length) {
        return false;
    }

    output.declared_length = declared_length;
    output.prefix_length = prefix_length;
    output.body.assign(
        available.begin() + static_cast<std::ptrdiff_t>(prefix_length),
        available.begin() + static_cast<std::ptrdiff_t>(prefix_length + body_length));
    start_ += prefix_length + body_length;
    if (start_ == buffer_.size()) {
        buffer_.clear();
        start_ = 0;
    }
    return true;
}

std::size_t frame_decoder::buffered_byte_count() const noexcept {
    return buffer_.size() - start_;
}

std::vector<std::uint8_t> encode_frame(const std::span<const std::uint8_t> body) {
    if (body.size() > std::numeric_limits<std::uint32_t>::max() - 4ULL) {
        throw std::overflow_error("AION2 frame body is too large");
    }

    std::vector<std::uint8_t> result;
    result.reserve(body.size() + 5);
    write_leb128(static_cast<std::uint32_t>(body.size() + 4), result);
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

} // namespace sema
