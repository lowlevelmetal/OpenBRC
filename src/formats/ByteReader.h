#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace brc::formats {

// Bounds-checked sequential reader over a byte span. Every read returns false
// (and leaves the output untouched) when the data runs out, so parsers can
// chain reads and fail cleanly on truncated input. OMT data is big-endian
// (the toolkit came from the Mac); `Endian::Little` serves the Win32 formats.
template <std::endian Endian>
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) : m_data(data) {}

    std::size_t position() const { return m_pos; }
    std::size_t remaining() const { return m_data.size() - m_pos; }
    bool atEnd() const { return m_pos >= m_data.size(); }
    bool seek(std::size_t pos) {
        if (pos > m_data.size())
            return false;
        m_pos = pos;
        return true;
    }
    bool skip(std::size_t n) { return seek(m_pos + n); }

    bool u8(std::uint8_t& out) {
        if (remaining() < 1)
            return false;
        out = std::to_integer<std::uint8_t>(m_data[m_pos++]);
        return true;
    }
    bool u16(std::uint16_t& out) {
        std::uint64_t v;
        if (!read(2, v))
            return false;
        out = static_cast<std::uint16_t>(v);
        return true;
    }
    bool i16(std::int16_t& out) {
        std::uint16_t v;
        if (!u16(v))
            return false;
        out = static_cast<std::int16_t>(v);
        return true;
    }
    bool u32(std::uint32_t& out) {
        std::uint64_t v;
        if (!read(4, v))
            return false;
        out = static_cast<std::uint32_t>(v);
        return true;
    }
    bool i32(std::int32_t& out) {
        std::uint32_t v;
        if (!u32(v))
            return false;
        out = static_cast<std::int32_t>(v);
        return true;
    }
    // Pascal string: u8 length, then the bytes.
    bool pascal(std::string& out) {
        std::uint8_t n;
        if (!u8(n) || remaining() < n) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(m_data.data() + m_pos), n);
        m_pos += n;
        return true;
    }
    bool bytes(std::span<std::byte> out) {
        if (remaining() < out.size())
            return false;
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = m_data[m_pos + i];
        m_pos += out.size();
        return true;
    }
    std::span<const std::byte> rest() const { return m_data.subspan(m_pos); }

private:
    bool read(int n, std::uint64_t& out) {
        if (remaining() < static_cast<std::size_t>(n))
            return false;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) {
            const auto b = std::to_integer<std::uint64_t>(m_data[m_pos + static_cast<std::size_t>(i)]);
            if constexpr (Endian == std::endian::big)
                v = (v << 8) | b;
            else
                v |= b << (8 * i);
        }
        m_pos += static_cast<std::size_t>(n);
        out = v;
        return true;
    }

    std::span<const std::byte> m_data;
    std::size_t m_pos = 0;
};

using BeReader = ByteReader<std::endian::big>;
using LeReader = ByteReader<std::endian::little>;

} // namespace brc::formats
