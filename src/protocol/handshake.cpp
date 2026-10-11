#include "sema/handshake.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace sema {
namespace {

constexpr std::array<std::uint8_t, 8> server_header{0x11, 0x36, 0x00, 0x00, 0x60, 0xEA, 0x00, 0x00};
constexpr std::array<std::uint8_t, 12> server_trailer{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF9, 0xFF, 0xFF, 0xFF};
constexpr std::array<std::uint8_t, 6> lobby_server_header{0x01, 0x39, 0x00, 0x00, 0x3C, 0x00};
constexpr std::array<std::uint8_t, 10> lobby_server_trailer{};

void check_status(const NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(std::string(operation) + " failed with NTSTATUS " + std::to_string(status));
    }
}

std::uint32_t read_leb128(const std::span<const std::uint8_t> source, std::size_t& bytes_read) {
    std::uint32_t value = 0;
    bytes_read = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (bytes_read >= source.size()) {
            throw std::runtime_error("handshake field length is truncated");
        }
        const auto current = source[bytes_read++];
        if (shift == 28 && (current & 0xF0U) != 0) {
            throw std::runtime_error("handshake field length overflows uint32");
        }
        value |= static_cast<std::uint32_t>(current & 0x7FU) << shift;
        if ((current & 0x80U) == 0) {
            return value;
        }
    }
    throw std::runtime_error("invalid handshake field length");
}

void write_leb128(std::uint32_t value, std::vector<std::uint8_t>& output) {
    do {
        auto current = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7;
        if (value != 0) current |= 0x80U;
        output.push_back(current);
    } while (value != 0);
}

std::size_t read_der_length(std::span<const std::uint8_t> der, std::size_t& offset) {
    if (offset >= der.size()) throw std::runtime_error("truncated DER length");
    const auto first = der[offset++];
    if ((first & 0x80U) == 0) return first;
    const auto count = static_cast<std::size_t>(first & 0x7FU);
    if (count == 0 || count > sizeof(std::size_t) || offset + count > der.size()) {
        throw std::runtime_error("invalid DER length");
    }
    std::size_t result = 0;
    for (std::size_t i = 0; i < count; ++i) result = (result << 8) | der[offset++];
    return result;
}

std::span<const std::uint8_t> read_der_integer(std::span<const std::uint8_t> der, std::size_t& offset) {
    if (offset >= der.size() || der[offset++] != 0x02) throw std::runtime_error("expected DER integer");
    const auto length = read_der_length(der, offset);
    if (offset + length > der.size()) throw std::runtime_error("truncated DER integer");
    const auto result = der.subspan(offset, length);
    offset += length;
    return result;
}

struct rsa_parts {
    std::span<const std::uint8_t> modulus;
    std::span<const std::uint8_t> exponent;
};

rsa_parts parse_rsa_key(std::span<const std::uint8_t> der);

client_hello parse_client_hello_impl(
    const std::span<const std::uint8_t> body,
    const std::uint8_t type,
    const std::uint8_t channel) {
    if (body.size() < 2 || body[0] != type || body[1] != channel) {
        throw std::runtime_error("frame is not the expected AION2 client hello");
    }
    std::size_t length_bytes = 0;
    const auto field_length = read_leb128(body.subspan(2), length_bytes);
    const auto field_offset = 2 + length_bytes;
    if (field_offset + field_length > body.size()) throw std::runtime_error("client RSA key is truncated");
    client_hello result{
        {body.begin() + static_cast<std::ptrdiff_t>(field_offset), body.begin() + static_cast<std::ptrdiff_t>(field_offset + field_length)},
        {body.begin() + static_cast<std::ptrdiff_t>(field_offset + field_length), body.end()}};
    (void)parse_rsa_key(result.public_key_der);
    return result;
}

rsa_parts parse_rsa_key(std::span<const std::uint8_t> der) {
    std::size_t offset = 0;
    if (der.empty() || der[offset++] != 0x30) throw std::runtime_error("RSA key is not a DER sequence");
    const auto sequence_length = read_der_length(der, offset);
    if (offset + sequence_length != der.size()) throw std::runtime_error("invalid DER RSA sequence length");
    auto modulus = read_der_integer(der, offset);
    const auto exponent = read_der_integer(der, offset);
    if (offset != der.size()) throw std::runtime_error("trailing bytes in DER RSA key");
    if (modulus.size() > 1 && modulus.front() == 0) modulus = modulus.subspan(1);
    if (modulus.empty() || exponent.empty()) throw std::runtime_error("empty RSA key component");
    return {modulus, exponent};
}

struct algorithm_deleter {
    void operator()(void* handle) const noexcept {
        if (handle) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
    }
};
struct key_deleter {
    void operator()(void* handle) const noexcept {
        if (handle) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(handle));
    }
};

} // namespace

client_hello parse_client_hello(const std::span<const std::uint8_t> body) {
    return parse_client_hello_impl(body, 0x10, 0x36);
}

client_hello parse_lobby_client_hello(const std::span<const std::uint8_t> body) {
    return parse_client_hello_impl(body, 0x00, 0x39);
}

server_hello parse_server_hello(const std::span<const std::uint8_t> body) {
    if (body.size() < 9 || body[0] != 0x11 || body[1] != 0x36) {
        throw std::runtime_error("frame is not an AION2 server hello");
    }
    constexpr std::size_t length_offset = 8;
    std::size_t length_bytes = 0;
    const auto field_length = read_leb128(body.subspan(length_offset), length_bytes);
    const auto field_offset = length_offset + length_bytes;
    if (field_offset + field_length > body.size()) throw std::runtime_error("server ciphertext is truncated");
    return {
        {body.begin(), body.begin() + static_cast<std::ptrdiff_t>(length_offset)},
        {body.begin() + static_cast<std::ptrdiff_t>(field_offset), body.begin() + static_cast<std::ptrdiff_t>(field_offset + field_length)},
        {body.begin() + static_cast<std::ptrdiff_t>(field_offset + field_length), body.end()}};
}

std::vector<std::uint8_t> create_server_hello(
    const client_hello& hello,
    const std::span<const std::uint8_t> session_data) {
    if (session_data.empty()) throw std::invalid_argument("session data cannot be empty");
    const auto parts = parse_rsa_key(hello.public_key_der);

    BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
    check_status(BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider");
    std::unique_ptr<void, algorithm_deleter> algorithm(raw_algorithm);

    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = static_cast<ULONG>(parts.modulus.size() * 8);
    header.cbPublicExp = static_cast<ULONG>(parts.exponent.size());
    header.cbModulus = static_cast<ULONG>(parts.modulus.size());
    std::vector<std::uint8_t> blob(sizeof(header) + parts.exponent.size() + parts.modulus.size());
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), parts.exponent.data(), parts.exponent.size());
    std::memcpy(blob.data() + sizeof(header) + parts.exponent.size(), parts.modulus.data(), parts.modulus.size());

    BCRYPT_KEY_HANDLE raw_key = nullptr;
    check_status(BCryptImportKeyPair(
        raw_algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &raw_key,
        blob.data(), static_cast<ULONG>(blob.size()), 0), "BCryptImportKeyPair");
    std::unique_ptr<void, key_deleter> key(raw_key);

    ULONG encrypted_size = 0;
    check_status(BCryptEncrypt(
        raw_key, const_cast<PUCHAR>(session_data.data()), static_cast<ULONG>(session_data.size()),
        nullptr, nullptr, 0, nullptr, 0, &encrypted_size, BCRYPT_PAD_PKCS1), "BCryptEncrypt(size)");
    std::vector<std::uint8_t> encrypted(encrypted_size);
    check_status(BCryptEncrypt(
        raw_key, const_cast<PUCHAR>(session_data.data()), static_cast<ULONG>(session_data.size()),
        nullptr, nullptr, 0, encrypted.data(), encrypted_size, &encrypted_size, BCRYPT_PAD_PKCS1), "BCryptEncrypt");
    encrypted.resize(encrypted_size);

    std::vector<std::uint8_t> result(server_header.begin(), server_header.end());
    write_leb128(static_cast<std::uint32_t>(encrypted.size()), result);
    result.insert(result.end(), encrypted.begin(), encrypted.end());
    result.insert(result.end(), server_trailer.begin(), server_trailer.end());
    return result;
}

std::vector<std::uint8_t> create_lobby_server_hello(
    const client_hello& hello,
    const std::span<const std::uint8_t> session_data) {
    const auto world_shape = create_server_hello(hello, session_data);
    const auto parsed = parse_server_hello(world_shape);
    std::vector<std::uint8_t> result(lobby_server_header.begin(), lobby_server_header.end());
    write_leb128(static_cast<std::uint32_t>(parsed.encrypted_session_data.size()), result);
    result.insert(result.end(), parsed.encrypted_session_data.begin(), parsed.encrypted_session_data.end());
    result.insert(result.end(), lobby_server_trailer.begin(), lobby_server_trailer.end());
    return result;
}

} // namespace sema
