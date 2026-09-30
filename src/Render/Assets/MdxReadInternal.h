// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace War3Map::Render::MdxReadInternal {
// 结构错误统一抛出，由 ParseMdx 转成诊断；不发布部分模型。
struct MdxError : std::runtime_error {
    std::string code;
    MdxError(std::string errorCode, const std::string& message) : std::runtime_error(message), code(std::move(errorCode)) {}
};

[[noreturn]] inline void Fail(const char* code, const std::string& message) { throw MdxError(code, message); }

// 有界小端读取器：只在 [begin, end) 内读取。
class Reader {
public:
    Reader(std::span<const std::byte> bytes, std::size_t begin, std::size_t end) : bytes_(bytes), position_(begin), end_(end) {}
    [[nodiscard]] std::size_t Position() const noexcept { return position_; }
    [[nodiscard]] std::size_t Remaining() const noexcept { return end_ - position_; }
    [[nodiscard]] Reader Until(std::size_t end, const char* what) const {
        if (end < position_ || end > end_) Fail("MDX_SIZE_INVALID", std::string(what) + " 越过所在块边界。");
        return Reader(bytes_, position_, end);
    }
    void Need(std::size_t count, const char* what) const {
        if (count > Remaining()) Fail("MDX_TRUNCATED", std::string(what) + " 超出所在块范围。");
    }
    std::uint32_t U32(const char* what) {
        Need(4, what);
        std::uint32_t value = 0;
        std::memcpy(&value, bytes_.data() + position_, 4);
        position_ += 4;
        return value;
    }
    std::int32_t I32(const char* what) { return static_cast<std::int32_t>(U32(what)); }
    float F32(const char* what) {
        const auto raw = U32(what);
        float value = 0;
        std::memcpy(&value, &raw, 4);
        if (!std::isfinite(value)) Fail("MDX_NUMBER_INVALID", std::string(what) + " 不是有限浮点数。");
        return value;
    }
    std::uint16_t U16(const char* what) {
        Need(2, what);
        std::uint16_t value = 0;
        std::memcpy(&value, bytes_.data() + position_, 2);
        position_ += 2;
        return value;
    }
    std::uint8_t U8(const char* what) {
        Need(1, what);
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }
    std::string Tag(const char* what) {
        Need(4, what);
        std::string tag(reinterpret_cast<const char*>(bytes_.data() + position_), 4);
        position_ += 4;
        return tag;
    }
    [[nodiscard]] std::string PeekTag() const {
        if (Remaining() < 4) return {};
        return std::string(reinterpret_cast<const char*>(bytes_.data() + position_), 4);
    }
    std::string FixedString(std::size_t length, const char* what) {
        Need(length, what);
        const auto* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
        const auto* terminator = static_cast<const char*>(std::memchr(begin, 0, length));
        std::string value(begin, terminator != nullptr ? static_cast<std::size_t>(terminator - begin) : length);
        position_ += length;
        return value;
    }
    void Skip(std::size_t count, const char* what) { Need(count, what); position_ += count; }
    void ExpectTag(const char* tag, const char* what) {
        if (Tag(what) != tag) Fail("MDX_TAG_MISMATCH", std::string(what) + " 缺少 " + tag + " 标记。");
    }
    // 读取计数并确认后续 count × width 字节存在，防止用伪造计数触发巨量分配。
    std::uint32_t Count(std::size_t width, std::size_t maximum, const char* what) {
        const auto count = U32(what);
        if (count > maximum) Fail("MDX_LIMIT", std::string(what) + " 数量超过上限。");
        if (width != 0 && static_cast<std::uint64_t>(count) * width > Remaining())
            Fail("MDX_TRUNCATED", std::string(what) + " 数据超出所在块范围。");
        return count;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_;
    std::size_t end_;
};

} // namespace War3Map::Render::MdxReadInternal
