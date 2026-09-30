// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/Render/MdxModel.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace War3Map::Render::Scan {
struct Limits {
    std::uint32_t modelMs = 5000, totalSeconds = 300, memoryMiB = 512;
    std::uint32_t maxModels = 10000, fileMiB = 32, maxSamples = 4096;
    std::uint32_t maxNodes = 4096, maxVertices = 262144;
};
struct Options {
    Limits limits;
    std::filesystem::path executable, gameRoot, list, tsv;
    std::string mode, model;
};
struct Record {
    std::string path, source, parse = "NOT_RUN", compile = "NOT_RUN", state = "pending";
    std::uint64_t samples = 0, planned = 0, nodes = 0, groups = 0, alphas = 0;
    std::uint64_t nodeMismatch = 0, groupMismatch = 0, alphaMismatch = 0, diagnosticMismatch = 0, mismatchSamples = 0;
    std::uint64_t matchedRejections = 0, legacyUsable = 0, elapsedMs = 0;
    std::uint64_t nonKeyUnavailable = 0;
    std::string firstSample, digest, detail;
    std::uint64_t colors = 0, colorMismatch = 0, numericSamples = 0;
    std::map<std::string,std::uint64_t> rejectionCodes; // 核心实际拒绝的样本数；差分仍独立报告
    [[nodiscard]] std::string PrimaryRejection() const {
        std::string code; std::uint64_t count = 0;
        for (const auto& [key,value] : rejectionCodes) if (value > count) { code = key; count = value; }
        return code; // 最大计数；同数按字典序，确定性分账，不声称唯一根因
    }
    [[nodiscard]] bool AllSamplesRejected() const {
        return compile == "OK" && (state == "complete" || state == "particle_only_unimplemented") && samples != 0 &&
            samples == planned && numericSamples == 0 && matchedRejections == samples && mismatchSamples == 0;
    }
    std::uint64_t particleOnly = 0; // 仅编译成功的无GEOS且有PREM/PRE2模型；不是绘制成功。
    [[nodiscard]] bool FallbackPositive() const { return parse == "OK" && compile != "OK" && compile != "NOT_RUN" && legacyUsable != 0; }
};
struct Interrupted { const char* code; };
using Checkpoint = std::function<void()>;
using Clock = std::chrono::steady_clock;
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path FromUtf8(const std::string& text);
std::string Cell(std::string_view value);
std::string Header();
std::string Row(const Record& record);
Record ReadRecord(const std::filesystem::path& path);
void WriteRecord(const std::filesystem::path& path, const Record& record);
std::vector<std::string> ReadLines(const std::filesystem::path& path, std::uint64_t maxBytes, std::uint32_t maxLines);
void Enumerate(const Options& options); // 也在受内存/时间约束的独立子进程中执行。
std::vector<std::byte> ReadModel(const Options& options, std::string& source);
void Compare(const MdxModel& model, Record& record, const Limits& limits, const Checkpoint& checkpoint);
// 只限制/结束本工具创建的直接子进程；不安装服务、不访问游戏进程。
std::string RunChild(const Options& options, const std::vector<std::string>& args, std::uint32_t timeoutMs);
std::vector<std::string> LimitArguments(const Limits& limits);
int Run(const Options& options);
}
