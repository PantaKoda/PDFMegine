#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace pdfbookmark::text::detail {
namespace {

constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void block(std::array<std::uint32_t, 8>& h, const std::uint8_t* p) {
    std::uint32_t w[64]{};
    for (int i = 0; i < 16; ++i) {
        const int j = i * 4;
        w[i] = (static_cast<std::uint32_t>(p[j]) << 24) |
               (static_cast<std::uint32_t>(p[j + 1]) << 16) |
               (static_cast<std::uint32_t>(p[j + 2]) << 8) | p[j + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a = h[0], b = h[1], c = h[2], d = h[3];
    auto e = h[4], f = h[5], g = h[6], q = h[7];
    for (int i = 0; i < 64; ++i) {
        const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto t1 = q + s1 + choice + k[i] + w[i];
        const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        q = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += q;
}

}  // namespace

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size) {
    std::array<std::uint32_t, 8> h = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const std::size_t full = size / 64;
    for (std::size_t i = 0; i < full; ++i) block(h, data + i * 64);
    std::array<std::uint8_t, 128> tail{};
    const std::size_t rem = size % 64;
    for (std::size_t i = 0; i < rem; ++i) tail[i] = data[full * 64 + i];
    tail[rem] = 0x80;
    const std::size_t tail_size = rem < 56 ? 64 : 128;
    const auto bits = static_cast<std::uint64_t>(size) * 8;
    for (int i = 0; i < 8; ++i)
        tail[tail_size - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bits >> (i * 8));
    block(h, tail.data());
    if (tail_size == 128) block(h, tail.data() + 64);
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < h.size(); ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::array<std::uint8_t, 32> sha256_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open model resource for hashing");
    std::array<std::uint32_t, 8> h = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 65536> chunk{};
    std::array<std::uint8_t, 64> carry{};
    std::size_t carry_size = 0;
    std::uint64_t total = 0;
    while (stream) {
        stream.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        const auto got = static_cast<std::size_t>(stream.gcount());
        total += got;
        std::size_t pos = 0;
        if (carry_size != 0) {
            const auto n = std::min(got, 64 - carry_size);
            for (std::size_t i = 0; i < n; ++i) carry[carry_size + i] = chunk[i];
            carry_size += n;
            pos += n;
            if (carry_size == 64) {
                block(h, carry.data());
                carry_size = 0;
            }
        }
        while (pos + 64 <= got) {
            block(h, chunk.data() + pos);
            pos += 64;
        }
        while (pos < got) carry[carry_size++] = chunk[pos++];
    }
    if (!stream.eof()) throw std::runtime_error("Cannot read model resource for hashing");
    std::array<std::uint8_t, 128> tail{};
    for (std::size_t i = 0; i < carry_size; ++i) tail[i] = carry[i];
    tail[carry_size] = 0x80;
    const std::size_t tail_size = carry_size < 56 ? 64 : 128;
    const auto bits = total * 8;
    for (int i = 0; i < 8; ++i)
        tail[tail_size - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bits >> (i * 8));
    block(h, tail.data());
    if (tail_size == 128) block(h, tail.data() + 64);
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < h.size(); ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::string sha256_hex(const std::array<std::uint8_t, 32>& digest) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

}  // namespace pdfbookmark::text::detail
