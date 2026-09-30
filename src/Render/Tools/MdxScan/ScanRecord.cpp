// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "ScanTypes.h"
#include "ScanDetail.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace War3Map::Render::Scan {
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::filesystem::path FromUtf8(const std::string& text) {
    std::u8string utf8; utf8.reserve(text.size());
    for (const auto c : text) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    return std::filesystem::path(utf8);
}
std::string Cell(std::string_view value) {
    std::string result;
    for (const auto c : value) {
        if (c == '\t') result += "\\t";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    return result;
}
namespace {
std::string Uncell(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const auto c = value[++i]; result += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
        } else result += value[i];
    }
    return result;
}
std::string RejectCounts(const std::map<std::string,std::uint64_t>& counts) {
    std::string result;
    for (const auto& [code,count] : counts) { if (!result.empty()) result += ';'; result += code + "=" + std::to_string(count); }
    return result;
}
std::map<std::string,std::uint64_t> ParseCounts(std::string_view text) {
    std::map<std::string,std::uint64_t> out;
    while (!text.empty()) {
        const auto end = text.find(';'); const auto item = text.substr(0,end); const auto equal = item.find('=');
        const auto code = item.substr(0,equal);
        if (equal == std::string_view::npos || code.empty() || code.size() > 128 || out.size() >= 128 ||
            !std::all_of(code.begin(),code.end(),[](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
            throw std::runtime_error("SCAN_REPORT_INVALID");
        const auto number = item.substr(equal+1); std::uint64_t count = 0;
        const auto parsed = std::from_chars(number.data(),number.data()+number.size(),count);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data()+number.size() || count == 0 ||
            !out.emplace(std::string(code),count).second) throw std::runtime_error("SCAN_REPORT_INVALID");
        if (end == std::string_view::npos) break;
        text.remove_prefix(end+1);
        if (text.empty()) throw std::runtime_error("SCAN_REPORT_INVALID");
    }
    return out;
}

}
std::string Header() {
    return "path\tsource\tparse_code\tcompile_code\tstate\tsamples\tplanned_samples\tnodes_compared\tgroups_compared\talpha_compared"
        "\tnode_mismatch\tgroup_mismatch\talpha_mismatch\tdiagnostic_mismatch\tmismatch_samples\tmatched_rejections"
        "\tlegacy_usable\tfallback_positive\tnonkey_unavailable\telapsed_ms\tfirst_sample\tcontent_digest\tparticle_only\tdetail\tcolors_compared\tcolor_mismatch\tnumeric_samples\tsample_rejection_codes\tprimary_rejection_code";
}
std::string Row(const Record& r) {
    std::ostringstream out;
    out << Cell(r.path) << '\t' << Cell(r.source) << '\t' << Cell(r.parse) << '\t' << Cell(r.compile) << '\t' << Cell(r.state)
        << '\t' << r.samples << '\t' << r.planned << '\t' << r.nodes << '\t' << r.groups << '\t' << r.alphas
        << '\t' << r.nodeMismatch << '\t' << r.groupMismatch << '\t' << r.alphaMismatch << '\t' << r.diagnosticMismatch
        << '\t' << r.mismatchSamples << '\t' << r.matchedRejections << '\t' << r.legacyUsable << '\t' << r.FallbackPositive()
        << '\t' << r.nonKeyUnavailable << '\t' << r.elapsedMs << '\t' << Cell(r.firstSample) << '\t' << Cell(r.digest) << '\t' << r.particleOnly << '\t' << Cell(r.detail)
        << '\t' << r.colors << '\t' << r.colorMismatch << '\t' << r.numericSamples
        << '\t' << RejectCounts(r.rejectionCodes) << '\t' << r.PrimaryRejection();
    return out.str();
}
void WriteRecord(const std::filesystem::path& path, const Record& record) {
    std::ofstream out(path,std::ios::binary | std::ios::trunc);
    out << Row(record) << '\n'; out.flush();
    if (!out) throw std::runtime_error("SCAN_REPORT_WRITE_FAILED");
}
std::vector<std::string> ReadLines(const std::filesystem::path& path, std::uint64_t maxBytes, std::uint32_t maxLines) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("SCAN_LIST_OPEN_FAILED");
    std::vector<std::string> lines; std::string line; std::uint64_t bytes = 0;
    // 逐字节限制，不能等 getline 分配完一个恶意巨行后才检查。
    for (char c; file.get(c); ) {
        if (++bytes > maxBytes || line.size() >= std::min<std::uint64_t>(maxBytes,65536)) throw std::runtime_error("SCAN_LIST_LIMIT");
        if (c != '\n') { line += c; continue; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            if (lines.size() >= maxLines) throw std::runtime_error("SCAN_LIST_LIMIT");
            lines.push_back(std::move(line)); line.clear();
        }
    }
    if (file.bad()) throw std::runtime_error("SCAN_LIST_READ_FAILED");
    if (!line.empty()) {
        if (line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            if (lines.size() >= maxLines) throw std::runtime_error("SCAN_LIST_LIMIT");
            lines.push_back(std::move(line));
        }
    }
    return lines;
}
Record ReadRecord(const std::filesystem::path& path) {
    const auto lines = ReadLines(path,65536,1);
    if (lines.size() != 1) throw std::runtime_error("SCAN_REPORT_INVALID");
    std::vector<std::string> cells; std::istringstream line(lines[0]);
    for (std::string cell; std::getline(line,cell,'\t'); ) cells.push_back(cell);
    // 末列为空时 getline 不返回它。
    if (!lines[0].empty() && lines[0].back() == '\t') cells.emplace_back();
    if (cells.size() != 29) throw std::runtime_error("SCAN_REPORT_INVALID");
    Record r; r.path = Uncell(cells[0]); r.source = Uncell(cells[1]); r.parse = Uncell(cells[2]);
    r.compile = Uncell(cells[3]); r.state = Uncell(cells[4]);
    const auto number = [&](std::size_t i) {
        std::uint64_t v = 0; const auto& s = cells[i];
        const auto result = std::from_chars(s.data(),s.data()+s.size(),v);
        if (result.ec != std::errc{} || result.ptr != s.data()+s.size()) throw std::runtime_error("SCAN_REPORT_INVALID");
        return v;
    };
    r.samples = number(5); r.planned = number(6); r.nodes = number(7); r.groups = number(8); r.alphas = number(9);
    r.nodeMismatch = number(10); r.groupMismatch = number(11); r.alphaMismatch = number(12);
    r.diagnosticMismatch = number(13); r.mismatchSamples = number(14); r.matchedRejections = number(15);
    r.legacyUsable = number(16); r.nonKeyUnavailable = number(18); r.elapsedMs = number(19); r.firstSample = Uncell(cells[20]); r.digest = Uncell(cells[21]);
    r.particleOnly = number(22); r.detail = Uncell(cells[23]);
    r.colors = number(24); r.colorMismatch = number(25); r.numericSamples = number(26);
    r.rejectionCodes = ParseCounts(cells[27]);
    std::uint64_t rejects = 0;
    for (const auto& [code,count] : r.rejectionCodes) {
        (void)code;
        if (count > r.samples - rejects) throw std::runtime_error("SCAN_REPORT_INVALID");
        rejects += count;
    }
    if (cells[28] != r.PrimaryRejection() || rejects < r.matchedRejections || rejects > r.samples - std::min(r.samples,r.numericSamples))
        throw std::runtime_error("SCAN_REPORT_INVALID");
    if (r.numericSamples > r.samples || r.matchedRejections > r.samples - r.numericSamples)
        throw std::runtime_error("SCAN_REPORT_INVALID");
    if (r.particleOnly > 1 || (r.particleOnly != 0 && (r.parse != "OK" || r.compile != "OK"))
        || r.detail.size() > MaxDetailBytes || number(17) != static_cast<std::uint64_t>(r.FallbackPositive()))
        throw std::runtime_error("SCAN_REPORT_INVALID");
    return r;
}
}
