// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "GeosetChannelFixture.h"
#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"
#include "RenderCoreAllocProbe.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using GeosetChannelFixture::Bytes;

// 夹具动作按时间顺序：Stand=0（带 GEOA 键）、Death=1。
Code Compare(const MdxModel& model,std::uint64_t time,std::uint64_t global,std::uint32_t sequence=0,
    NodeKeySamplingPolicy keys=NodeKeySamplingPolicy::Strict) {
    const auto built=CompileMdxModel(model,{1,1},{"channels.mdx","synthetic","rgb"});
    Check(built.Succeeded(),"通道编译失败");
    const std::array assets{*built.value};
    auto made=PoseBatchEvaluator::Create(assets,1,BatchLimits{},GeosetFailurePolicy::Strict,{},{},{},keys);
    Check(made.Succeeded(),"通道预热失败");auto evaluator=std::move(*made.value);
    PoseRequest request;request.asset={1,1};request.instance={1,1};request.sequenceIndex=sequence;
    request.sequenceElapsedMs=time;request.globalElapsedMs=global;
    RenderCoreAllocProbe::Begin();auto result=evaluator.Evaluate(std::span(&request,1));const auto allocated=RenderCoreAllocProbe::End();
    Check(result.error==Code::Ok && allocated==0,"颜色热路径分配或批次失败");
    std::string error;
    for (std::uint32_t g=0;g<model.geosets.size();++g) {
        const auto a=SampleMdxGeosetAlpha(model,{g,sequence,time,global});
        if (!a.Succeeded()) { error=a.diagnostics[0].code;break; }
        const auto c=SampleMdxGeosetColor(model,{g,sequence,time,global});
        if (!c.Succeeded()) { error=c.diagnostics[0].code;break; }
        if (result.failedInstances==0) {
            Check(std::memcmp(&*a.value,&evaluator.Alphas(0)[g],4)==0,"透明度新旧逐位差分");
            Check(std::memcmp(c.value->data(),evaluator.Colors(0)[g].data(),12)==0,"RGB 新旧逐位差分");
        }
    }
    const auto code=evaluator.Receipts()[0].error;
    // 参考实现按原版取值；只有严格模式会对当前动作区间内的逆序键另行拒绝。
    Check(error==(code==Code::Ok ? "" : CodeName(code)) ||
        (keys==NodeKeySamplingPolicy::Strict && code==Code::GeoaKeysInvalid && error.empty()),"通道拒绝码差分");
    if (code!=Code::Ok) Check(evaluator.Colors(0).empty() && evaluator.Alphas(0).empty(),"失败泄漏部分颜色");
    return code;
}
void Run() {
    std::uint32_t samples=0;
    for (std::uint32_t mode=0;mode<4;++mode) for (const bool global:{false,true}) {
        const auto bytes=Bytes(mode,global);const auto parsed=ParseMdx(bytes);Check(parsed.Succeeded(),"真实格式 KGAC 解析失败");
        const auto& a=*parsed.value->geosets[1].animation;
        Check(a.staticColor==Vec3{1,0.211765f,0},"同一 GEOA 的静态 RGB 被按 KGAC 倒序");
        if (!global) Check(Compare(*parsed.value,250,0,1)==Code::Ok &&
            *SampleMdxGeosetColor(*parsed.value,{1,1,250,0}).value==a.staticColor,
            "同一原字节 GEOA 在无 KGAC 动作键时未回到静态 RGB");
        Check(a.hasColorTrack && a.colorInterpolation==mode && a.colorKeys[0].value==Vec3{0,0.25f,1},"KGAC BGR/插值丢失");
        if (mode>=2) for (const auto& key:a.colorKeys) {
            const Vec3 in=mode==2 ? Vec3{0.625f,0.375f,-0.125f} : Vec3{0.75f,0.25f,0.25f};
            const Vec3 out=mode==2 ? Vec3{0.625f,0.375f,-0.125f} : Vec3{0.25f,0.25f,0.75f};
            Check(key.inTangent==in && key.outTangent==out,"KGAC 非对称 BGR 入/出切线转换错误");
        }
        for (auto t:{0u,1u,123u,250u,500u,999u,1000u,1521u}) {
            Check(Compare(*parsed.value,t,t)==Code::Ok,"完整按键区间被拒绝");++samples;
        }
        // 独立手算中点；不能只让两个实现复制同一个错误。
        const auto alpha=SampleMdxGeosetAlpha(*parsed.value,{1,0,500,500});
        const auto color=SampleMdxGeosetColor(*parsed.value,{1,0,500,500});
        Check(alpha.Succeeded() && color.Succeeded(),"手算样本失败");
        Check(*alpha.value==(mode==0 ? 0.0f : 0.5f),"KGAO 多项式中点错误");
        Check(*color.value==(mode==0 ? Vec3{0,0.25f,1} : Vec3{0.5f,0.25f,0.5f}),"KGAC 多项式/BGR 错误");
        auto truncated=bytes;truncated.pop_back();Check(!ParseMdx(truncated).Succeeded(),"截断未拒绝");
    }
    auto zero=*ParseMdx(Bytes(1,true)).value;zero.globalSequences[0]=0;
    Check(Compare(zero,500,UINT64_MAX)==Code::Ok,"零周期 GEOA 差分");
    Check(*SampleMdxGeosetAlpha(zero,{1,0,500,UINT64_MAX}).value==0 &&
        *SampleMdxGeosetColor(zero,{1,0,500,UINT64_MAX}).value==Vec3{0,0.25f,1},"零周期 GEOA 被混为动作时钟");
    auto& constant=*zero.geosets[1].animation;constant.alphaKeys.resize(1);constant.colorKeys.resize(1);
    constant.alphaKeys[0].frame=constant.colorKeys[0].frame=333;constant.alphaKeys[0].value=0.25f;
    for (auto period:{0u,100u}) { zero.globalSequences[0]=period;Check(Compare(zero,500,UINT64_MAX)==Code::Ok,"全局单键 GEOA 差分");
        Check(*SampleMdxGeosetAlpha(zero,{1,0,500,UINT64_MAX}).value==0.25f &&
            *SampleMdxGeosetColor(zero,{1,0,500,UINT64_MAX}).value==Vec3{0,0.25f,1},"全局单键 GEOA 误取默认值"); }
    zero.globalSequences[0]=0;constant.alphaKeys.push_back(constant.alphaKeys[0]);constant.alphaKeys.back().frame=500;
    Check(Compare(zero,500,0)==Code::Ok && *SampleMdxGeosetAlpha(zero,{1,0,500,0}).value==0.25f,"零周期KGAO未取归零首键");
    constant.colorKeys.push_back(constant.colorKeys[0]);constant.colorKeys.back().frame=500;
    Check(Compare(zero,500,0)==Code::Ok && *SampleMdxGeosetColor(zero,{1,0,500,0}).value==Vec3{0,0.25f,1},"零周期KGAC未取归零首键");
    zero.globalSequences[0]=100;constant.alphaKeys.back().frame=constant.colorKeys.back().frame=533;
    constant.alphaKeys.back().value=0.75f;constant.colorKeys.back().value={1,0.25f,0};
    Check(Compare(zero,500,50)==Code::Ok && *SampleMdxGeosetAlpha(zero,{1,0,500,50}).value==0.375f &&
        *SampleMdxGeosetColor(zero,{1,0,500,50}).value==Vec3{0.25f,0.25f,0.75f},"归零后周期外右键插值错误");
    constant.colorKeys.back().frame=UINT32_MAX;Check(Compare(zero,500,0)==Code::GeoaGlobalSpanUnsupported,"全局帧跨度溢出未拒绝");
    // Necropolis 类型：整条轨迹有多个动作的键，但当前动作只有末端一键。
    for (auto mode:{0u,1u,2u,3u}) {
        auto single=*ParseMdx(Bytes(mode,false)).value;
        auto& channel=*single.geosets[1].animation;
        channel.alphaKeys[0].frame=channel.colorKeys[0].frame=700;
        channel.alphaKeys[0].value=0.25f;channel.colorKeys[0].value={0.25f,0.5f,0.75f};
        channel.alphaKeys[1].frame=channel.colorKeys[1].frame=2000;
        for (auto t:{0u,699u,700u,999u}) {
            Check(Compare(single,t,0)==Code::Ok,"普通动作单键新旧差分");
            Check(*SampleMdxGeosetAlpha(single,{1,0,t,0}).value==0.25f &&
                *SampleMdxGeosetColor(single,{1,0,t,0}).value==Vec3{0.25f,0.5f,0.75f},"普通单键错误等待首帧或借用别的动作");
        }
    }
    auto model=*ParseMdx(Bytes(2,false)).value;
    model.geosets[1].animation->colorKeys[0].outTangent[0]=20;
    Check(Compare(model,500,0)==Code::Ok,"有限颜色曲线不能产生 NaN");
    Check((*SampleMdxGeosetColor(model,{1,0,500,0}).value)[0]==1,"RGB 预览夹取没有确定值");
    auto bad=model;bad.geosets[1].animation->colorInterpolation=99;Check(Compare(bad,500,0)==Code::GeoaInterpolationUnsupported,"未知插值未拒绝");
    bad=model;bad.geosets[1].animation->colorKeys[0].outTangent[0]=std::numeric_limits<float>::quiet_NaN();
    Check(Compare(bad,500,0)==Code::GeoaColorInvalid,"NaN 控制点未拒绝");
    bad=model;bad.geosets[1].animation->colorGlobalSequence=0;
    Check(Compare(bad,500,0)==Code::GeoaGlobalSequenceUnsupported,"无效全局序列未拒绝");
    // 原版接缝（IDA 6F789EB0/6F77A720/6F78A1B0）：首键前取末键并向首键回绕、比例为负；末键后接回首键，负跨度加动作长度。
    bad=model;bad.geosets[1].animation->colorKeys[0].frame=100;
    Check(Compare(bad,50,0)==Code::Ok,"首键前回绕差分");
    bad=model;bad.geosets[1].animation->alphaKeys[1].frame=700;
    Check(Compare(bad,800,0)==Code::Ok,"曲线尾键后接缝差分");
    {
        auto seam=*ParseMdx(Bytes(1,false)).value; // 线性 KGAO {0:0,1000:1}
        auto& keys=seam.geosets[1].animation->alphaKeys;
        keys[0].frame=100;
        Check(Compare(seam,50,0)==Code::Ok && *SampleMdxGeosetAlpha(seam,{1,0,50,0}).value==1.5f,"首键前未按原版负比例回绕");
        keys[0].frame=0;keys[1].frame=700;
        Check(Compare(seam,850,0)==Code::Ok && *SampleMdxGeosetAlpha(seam,{1,0,850,0}).value==0.5f,"末键后未接回首键");
    }
    {
        // 巫妖王类：当前动作区间内源键逆序。严格模式拒绝；原序预览按原版顺序查找，不排序、不删键。
        auto order=*ParseMdx(Bytes(1,false)).value;
        auto& keys=order.geosets[1].animation->alphaKeys;
        keys.push_back(keys[0]);keys.back().frame=500;keys.back().value=0.25f; // {0:0,1000:1,500:.25}
        Check(Compare(order,200,0)==Code::GeoaKeysInvalid,"严格模式放过逆序 GEOA");
        for (auto t:{0u,200u,500u,999u})
            Check(Compare(order,t,0,0,NodeKeySamplingPolicy::SourceOrderPreview)==Code::Ok,"原序预览 GEOA 差分");
        Check(*SampleMdxGeosetAlpha(order,{1,0,200,0}).value==0.2f && *SampleMdxGeosetAlpha(order,{1,0,500,0}).value==0.5f,
            "逆序区间未按原版顺序查找");
        // 排在 Stand 之后的 Death 动作：游标已越过全部键，区间为空，取静态值。
        Check(Compare(order,250,0,1,NodeKeySamplingPolicy::SourceOrderPreview)==Code::Ok &&
            *SampleMdxGeosetAlpha(order,{1,1,250,0}).value==order.geosets[1].animation->staticAlpha,"空区间未取静态值");
    }
    bad=model;bad.geosets[1].animation->flags=1;
    Check(Compare(bad,500,0)==Code::GeoaUninterpreted,"未知投影位未拒绝");
    {
        const auto compiled=CompileMdxModel(bad,{1,1},{"partial.mdx","synthetic","rgb"});
        Check(compiled.Succeeded(),"局部 GEOA 编译失败");const std::array partialAssets{*compiled.value};
        BatchLimits limits;limits.maxInstances=1;limits.maxNodes=compiled.value->NodeCount();
        limits.maxGroups=compiled.value->GroupCount();limits.maxGeosets=compiled.value->GeosetCount();
        auto strict=PoseBatchEvaluator::Create(partialAssets,1,limits);
        auto preview=PoseBatchEvaluator::Create(partialAssets,1,limits,GeosetFailurePolicy::PreviewHideInvalidGeosets);
        Check(strict.Succeeded() && preview.Succeeded(),"部件隔离预热失败");
        PoseRequest request;request.asset={1,1};request.instance={1,1};request.sequenceIndex=0;request.sequenceElapsedMs=500;
        Check(strict.value->Evaluate(std::span(&request,1)).failedInstances==1 &&
            strict.value->Receipts()[0].error==Code::GeoaUninterpreted && strict.value->GeosetIssues(0).empty(),"严格入口被预览放宽");
        RenderCoreAllocProbe::Begin();const auto result=preview.value->Evaluate(std::span(&request,1));
        const auto allocated=RenderCoreAllocProbe::End();
        const auto issues=preview.value->GeosetIssues(0);
        Check(result.error==Code::Ok && result.failedInstances==0 && allocated==0 &&
            preview.value->Receipts()[0].poseVersion!=0 && preview.value->Groups(0).size()==compiled.value->GroupCount(),
            "部分预览丢失真姿势或热路径发生分配");
        Check(issues.size()==1 && issues[0].geosetIndex==1 && issues[0].diagnostic.code==Code::GeoaUninterpreted &&
            issues[0].diagnostic.objectId==NoIndex && preview.value->Alphas(0)[1]==0 && preview.value->Colors(0)[1]==Vec3{},
            "坏部件未透明隔离或来源身份错误");
        const auto goodAlpha=SampleMdxGeosetAlpha(bad,{0,0,500,0});const auto goodColor=SampleMdxGeosetColor(bad,{0,0,500,0});
        Check(goodAlpha.Succeeded() && goodColor.Succeeded() && preview.value->Alphas(0)[0]==*goodAlpha.value &&
            preview.value->Colors(0)[0]==*goodColor.value,"隔离破坏好部件数值");
        Check(preview.value->StorageBytes()>strict.value->StorageBytes(),"预览缓存未计入预算");
        limits.maxBytes=strict.value->StorageBytes();
        const auto bounded=PoseBatchEvaluator::Create(partialAssets,1,limits,GeosetFailurePolicy::PreviewHideInvalidGeosets);
        Check(!bounded.Succeeded() && bounded.diagnostics[0].code==CodeName(Code::CapacityExceeded),"预览绕过严格存储预算");
        const std::array excess{request,request};
        Check(preview.value->Evaluate(excess).error==Code::CapacityExceeded && preview.value->Receipts().empty() &&
            preview.value->GeosetIssues(0).empty(),"前检失败泄漏上帧部件诊断");
    }
    // 两实例 RGB 在同一批独立，重复求值逐位一致，外部组矩阵仍使用各自时钟颜色。
    auto plain=*ParseMdx(Bytes(1,false)).value;auto built=CompileMdxModel(plain,{1,1},{"x","test","rgb"});
    const std::array assets{*built.value};auto made=PoseBatchEvaluator::Create(assets,1);auto evaluator=std::move(*made.value);
    std::array<PoseRequest,2> req{};for(std::uint32_t i=0;i<2;++i) { req[i].instance={i+1,1};req[i].asset={1,1};req[i].sequenceIndex=0;req[i].sequenceElapsedMs=i*500; }
    Check(evaluator.Evaluate(req).failedInstances==0,"双实例颜色失败");const auto first=evaluator.Colors(0)[1],second=evaluator.Colors(1)[1];
    Check(first!=second,"颜色串实例");Check(evaluator.Evaluate(req).failedInstances==0 && evaluator.Colors(0)[1]==first && evaluator.Colors(1)[1]==second,"颜色不确定");
    std::cout<<"PASS geoa-channels samples="<<samples<<" modes=4 global=1 bitwise_mismatches=0 new_calls=0 analytic_midpoints=1 game_seam=1 source_order_preview=1 unknown_nan_rejected=1\n";
}
}
int main() { try { Run();return 0; } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; } }
