#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sema {

struct frame {
    std::uint32_t declared_length{};
    std::size_t prefix_length{};
    std::vector<std::uint8_t> body;

    [[nodiscard]] std::size_t wire_length() const noexcept {
        return prefix_length + body.size();
    }
};

class frame_decoder {
public:
    static constexpr std::size_t default_maximum_body_length = 16U * 1024U * 1024U;

    explicit frame_decoder(std::size_t maximum_body_length = default_maximum_body_length);

    void append(std::span<const std::uint8_t> data);
    [[nodiscard]] bool try_read(frame& output);
    [[nodiscard]] std::size_t buffered_byte_count() const noexcept;

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t start_{};
    std::size_t maximum_body_length_{};
};

[[nodiscard]] std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> body);

} // namespace sema
