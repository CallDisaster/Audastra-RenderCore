// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace War3Map {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct OriginSpan {
    std::filesystem::path source;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::string fingerprint;
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string subsystem;
    std::string code;
    std::string message;
    std::optional<OriginSpan> origin;
};

template <typename T>
struct Result {
    std::optional<T> value;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        if (!value.has_value()) {
            return false;
        }
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Error) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static Result<T> Success(T result) {
        Result<T> output;
        output.value.emplace(std::move(result));
        return output;
    }

    [[nodiscard]] static Result<T> Failure(
        std::string subsystem,
        std::string code,
        std::string message) {
        Result<T> output;
        output.diagnostics.push_back(Diagnostic{
            DiagnosticSeverity::Error,
            std::move(subsystem),
            std::move(code),
            std::move(message),
            std::nullopt,
        });
        return output;
    }
};

} // namespace War3Map
