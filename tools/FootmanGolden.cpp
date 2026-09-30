// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view ExpectedModelSha256 =
    "7ee255776a6e757bd89766354df763201cd0519c488585b7c30151d5e313fc13";
constexpr std::uint64_t MaxFixtureBytes = 16ull * 1024 * 1024;

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path verify;
    std::string modelSha256;
};

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Options ParseOptions(std::span<const std::string> arguments) {
    Require(arguments.size() == 7, "用法：--input <本机MDX> --model-sha256 <宿主已核验SHA> --output <新夹具> 或 --verify <已有夹具>");
    Options options;
    for (std::size_t i = 1; i < arguments.size(); i += 2) {
        const auto& flag = arguments[i];
        const auto& value = arguments[i + 1];
        Require(!value.empty(), "参数值不能为空。");
        if (flag == "--input") {
            Require(options.input.empty(), "--input 不得重复。");
            options.input = std::filesystem::path(std::u8string(value.begin(), value.end()));
        } else if (flag == "--model-sha256") {
            Require(options.modelSha256.empty(), "--model-sha256 不得重复。");
            options.modelSha256 = value;
        } else if (flag == "--output") {
            Require(options.output.empty(), "--output 不得重复。");
            options.output = std::filesystem::path(std::u8string(value.begin(), value.end()));
        } else if (flag == "--verify") {
            Require(options.verify.empty(), "--verify 不得重复。");
            options.verify = std::filesystem::path(std::u8string(value.begin(), value.end()));
        } else {
            throw std::runtime_error("未知参数。");
        }
    }
    Require(!options.input.empty(), "必须指定本机MDX文件。");
    Require(options.modelSha256 == ExpectedModelSha256, "SHA声明不是W1已验证的Footman身份；宿主须先实际计算输入文件SHA。");
    Require(options.output.empty() != options.verify.empty(), "--output 与 --verify 必须且只能选择一个。");
    return options;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path, std::uint64_t maximum) {
    const auto size = std::filesystem::file_size(path);
    Require(size != 0 && size <= maximum, "输入为空或超过读取预算。");
    std::ifstream stream(path, std::ios::binary);
    Require(stream.is_open(), "无法打开输入文件。");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(stream.gcount() == static_cast<std::streamsize>(bytes.size()), "输入读取不完整。");
    char extra = 0;
    Require(!stream.read(&extra, 1), "读取期间输入大小改变。");
    Require(stream.eof(), "输入读取失败。");
    return bytes;
}

// 只做IEEE float32位模式的文本编码；不改变浮点求值顺序或数值。
void WriteFloatBits(std::ostream& output, float value) {
    Require(std::isfinite(value), "求值结果包含非有限浮点数。");
    constexpr std::string_view digits = "0123456789abcdef";
    const auto bits = std::bit_cast<std::uint32_t>(value);
    output << "0x";
    for (std::uint32_t shift = 28; ; shift -= 4) {
        output << digits[(bits >> shift) & 0xfu];
        if (shift == 0) {
            break;
        }
    }
}

template <class T>
void PrintDiagnostics(std::string_view stage, const War3Map::Result<T>& result) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << stage << ": " << diagnostic.code << ": " << diagnostic.message << '\n';
    }
}

std::string BuildFixture(std::span<const std::byte> bytes) {
    const auto parsed = War3Map::Render::ParseMdx(bytes);
    PrintDiagnostics("parse", parsed);
    Require(parsed.Succeeded(), "共享MDX解析器拒绝输入；未产生夹具。");
    const auto& model = *parsed.value;
    Require(model.sequences.size() > 6 && model.sequences[0].Usable() && model.sequences[6].Usable(),
        "固定动作序号不存在或不可用。");
    Require(model.sequences[0].name.starts_with("Stand") && model.sequences[6].name.starts_with("Walk"),
        "固定序号不是W1的Stand/Walk动作；不得猜动作或替换动作序号。");

    const War3RenderCore::AssetIdentity identity{1, 1};
    const auto compiled = War3RenderCore::CompileMdxModel(model, identity,
        {"units\\human\\footman\\footman.mdx", "host-verified-file", "sha256:" + std::string(ExpectedModelSha256)});
    PrintDiagnostics("compile", compiled);
    Require(compiled.Succeeded(), "共享编译器拒绝输入；未产生夹具。");
    const std::array assets{*compiled.value};
    auto prepared = War3RenderCore::PoseBatchEvaluator::Create(assets, 1);
    PrintDiagnostics("prepare", prepared);
    Require(prepared.Succeeded(), "共享姿态求值器准备失败；未产生夹具。");
    auto& evaluator = *prepared.value;
    const auto& asset = assets[0];

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "# SPDX-License-Identifier: GPL-3.0-only\n"
        << "# Copyright (C) 2026 CallDisaster\n"
        << "war3-render-core-footman-golden 1\n"
        << "source_model_sha256 " << ExpectedModelSha256 << '\n'
        << "asset_content_digest " << asset.Source().contentDigest << '\n'
        << "source_bytes " << bytes.size() << '\n'
        << "mdx_version " << model.version << '\n'
        << "node_count " << asset.NodeCount() << '\n'
        << "sequence_count " << asset.SequenceCount() << '\n'
        << "geoset_count " << asset.GeosetCount() << '\n'
        << "group_count " << asset.GroupCount() << '\n'
        << "quality PreviewApproximate\n"
        << "matrix_layout ColumnMajor3x4\n"
        << "coordinate_space ModelLocal\n"
        << "world_transform_applied 0\n"
        << "float_encoding ieee754-binary32-uint32-hex\n";

    const auto layouts = asset.Geosets();
    Require(layouts.size() == model.geosets.size(), "共享资产与解析模型的geoset数量不符。");
    for (std::size_t geosetIndex = 0; geosetIndex < model.geosets.size(); ++geosetIndex) {
        const auto& geoset = model.geosets[geosetIndex];
        const auto& layout = layouts[geosetIndex];
        Require(layout.groups.count == geoset.matrixGroupSizes.size(), "共享资产组数与geoset组成员不符。");
        output << "geoset " << geosetIndex << " groups " << layout.groups.offset << ' ' << layout.groups.count
            << " vertices " << layout.vertices.count << '\n';
        std::size_t memberOffset = 0;
        for (std::size_t groupIndex = 0; groupIndex < geoset.matrixGroupSizes.size(); ++groupIndex) {
            const auto count = geoset.matrixGroupSizes[groupIndex];
            Require(memberOffset <= geoset.matrixIndices.size() && count <= geoset.matrixIndices.size() - memberOffset,
                "geoset组成员范围不完整。");
            output << "members " << geosetIndex << ' ' << groupIndex << ' ' << count;
            for (std::uint32_t member = 0; member < count; ++member) {
                output << ' ' << geoset.matrixIndices[memberOffset + member];
            }
            memberOffset += count;
            output << '\n';
        }
        Require(memberOffset == geoset.matrixIndices.size(), "geoset组成员存在未映射尾部。");
    }

    struct Sample {
        std::string_view name;
        std::uint32_t sequence;
        std::uint64_t elapsedMs;
    };
    constexpr std::array samples{Sample{"stand500", 0, 500}, Sample{"walk250", 6, 250}, Sample{"walk283", 6, 283}};
    for (const auto& sample : samples) {
        War3RenderCore::PoseRequest request;
        request.asset = identity;
        request.instance = {1, 1};
        request.sequenceIndex = sample.sequence;
        request.sequenceElapsedMs = sample.elapsedMs;
        request.globalElapsedMs = 0;
        const auto outcome = evaluator.Evaluate(std::span(&request, 1));
        Require(outcome.error == War3RenderCore::Code::Ok && outcome.instanceCount == 1 && outcome.failedInstances == 0,
            "固定样本求值失败；未产生夹具。");
        const auto receipts = evaluator.Receipts();
        Require(receipts.size() == 1 && receipts[0].error == War3RenderCore::Code::Ok,
            "固定样本没有有效姿态回执。");
        const auto groups = evaluator.Groups(0);
        const auto alphas = evaluator.Alphas(0);
        const auto colors = evaluator.Colors(0);
        Require(groups.size() == asset.GroupCount() && alphas.size() == asset.GeosetCount() && colors.size() == alphas.size(),
            "固定样本矩阵、alpha或RGB数量不完整。");
        output << "sample " << sample.name << " sequence " << sample.sequence << " elapsed_ms " << sample.elapsedMs
            << " global_ms 0 sequence_frame " << receipts[0].sequenceFrame << '\n';
        for (std::size_t group = 0; group < groups.size(); ++group) {
            output << "matrix " << group;
            for (const auto value : groups[group].values) {
                output << ' ';
                WriteFloatBits(output, value);
            }
            output << '\n';
        }
        for (std::size_t geoset = 0; geoset < alphas.size(); ++geoset) {
            output << "alpha_rgb " << geoset << ' ';
            WriteFloatBits(output, alphas[geoset]);
            for (const auto value : colors[geoset]) {
                output << ' ';
                WriteFloatBits(output, value);
            }
            output << '\n';
        }
        output << "end_sample\n";
    }
    output << "end_golden\n";
    const auto text = output.str();
    Require(text.size() <= MaxFixtureBytes, "计算结果超过夹具输出预算。");
    return text;
}

int Run(std::span<const std::string> arguments) {
    try {
        const auto options = ParseOptions(arguments);
        const auto bytes = ReadBytes(options.input, War3Map::Render::MdxLimits{}.maxInputBytes);
        const auto text = BuildFixture(bytes);
        if (!options.verify.empty()) {
            const auto expected = ReadBytes(options.verify, MaxFixtureBytes);
            const auto expectedText = std::string_view(reinterpret_cast<const char*>(expected.data()), expected.size());
            Require(expectedText == text, "黄金夹具逐字节不同；未改写已有夹具。");
            std::cout << "FOOTMAN_GOLDEN_VERIFIED: byte_equal=1 samples=3\n";
        } else {
            Require(!std::filesystem::exists(options.output), "输出已经存在，拒绝覆盖。");
            std::ofstream stream(options.output, std::ios::binary);
            Require(stream.is_open(), "无法创建夹具输出文件；输出父目录须已存在。");
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.close();
            Require(!stream.fail(), "夹具写入失败。");
            std::cout << "FOOTMAN_GOLDEN_EXPORTED: samples=3\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FOOTMAN_GOLDEN_REJECTED: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#if defined(_WIN32) && defined(_MSC_VER)
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) {
        const auto utf8 = std::filesystem::path(argv[i]).u8string();
        arguments.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    return Run(arguments);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return Run(arguments);
}
#endif
