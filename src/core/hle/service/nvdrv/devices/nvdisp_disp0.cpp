// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <thread>

#include <boost/container/small_vector.hpp>

#include "common/adpf.h"
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/frame_trace.h"
#include "video_core/perf_counters.h"

namespace Service::Nvidia::Devices {

namespace {

/// Presented frames per second the guest is paced to, 0 for none (see SetFrameLimit).
std::atomic<u32> frame_limit_fps{};

Tegra::BlendMode ConvertBlending(Service::Nvnflinger::LayerBlending blending) {
    switch (blending) {
    case Service::Nvnflinger::LayerBlending::None:
    default:
        return Tegra::BlendMode::Opaque;
    case Service::Nvnflinger::LayerBlending::Premultiplied:
        return Tegra::BlendMode::Premultiplied;
    case Service::Nvnflinger::LayerBlending::Coverage:
        return Tegra::BlendMode::Coverage;
    }
}

} // namespace

void SetFrameLimit(u32 fps) {
    frame_limit_fps.store(fps, std::memory_order_relaxed);
}

nvdisp_disp0::nvdisp_disp0(Core::System& system_, NvCore::Container& core)
    : nvdevice{system_}, container{core}, nvmap{core.GetNvMapFile()} {}
nvdisp_disp0::~nvdisp_disp0() = default;

NvResult nvdisp_disp0::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvdisp_disp0::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {}
void nvdisp_disp0::OnClose(DeviceFD fd) {}

void nvdisp_disp0::Composite(std::span<const Nvnflinger::HwcLayer> sorted_layers) {
    std::vector<Tegra::FramebufferConfig> output_layers;
    std::vector<Service::Nvidia::NvFence> output_fences;
    output_layers.reserve(sorted_layers.size());
    output_fences.reserve(sorted_layers.size());

    for (auto& layer : sorted_layers) {
        output_layers.emplace_back(Tegra::FramebufferConfig{
            .address = nvmap.GetHandleAddress(layer.buffer_handle),
            .offset = layer.offset,
            .width = layer.width,
            .height = layer.height,
            .stride = layer.stride,
            .pixel_format = layer.format,
            .transform_flags = layer.transform,
            .crop_rect = layer.crop_rect,
            .blending = ConvertBlending(layer.blending),
            .layer_stack_mask = layer.layer_stack_mask,
        });

        for (size_t i = 0; i < layer.acquire_fence.num_fences; i++) {
            if (layer.acquire_fence.fences[i].id >= 0) {
                output_fences.push_back(layer.acquire_fence.fences[i]);
            }
        }
    }

    VideoCore::Perf::Add(VideoCore::Perf::Counter::VsyncFrames, 1);
    VideoCore::FrameTrace::Mark(VideoCore::FrameTrace::Event::VsyncComposed, 1);
    system.GPU().RequestComposite(std::move(output_layers), std::move(output_fences));
    Common::ADPF::ReportFrameInterval();
    system.SpeedLimiter().DoSpeedLimiting(system.CoreTiming().GetGlobalTimeUs());
    // The speed limiter above does nothing in multicore builds, so the frame cap is what keeps a
    // game that steps once per presented frame at its designed rate when the host vsync is off.
    if (const u32 fps = frame_limit_fps.load(std::memory_order_relaxed); fps != 0) {
        using Clock = std::chrono::steady_clock;
        static Clock::time_point next_present{};
        const auto interval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / static_cast<double>(fps)));
        const Clock::time_point now = Clock::now();
        if (next_present.time_since_epoch().count() == 0) {
            next_present = now;
        }
        const Clock::time_point target = next_present + interval;
        if (target > now) {
            // Sleep only what is left of the interval: never more, so a frame that is already at
            // the rate is not pushed past it.
            std::this_thread::sleep_until(target);
            next_present = target;
            VideoCore::Perf::AddDetailed(VideoCore::Perf::Counter::FramePaced, 1);
        } else {
            // The guest is slower than the cap: restart the cadence from now instead of keeping a
            // backlog, which would come out as bursts of fast frames (the game speeds up).
            next_present = now;
        }
    }
    system.GetPerfStats().EndSystemFrame();
    system.GetPerfStats().BeginSystemFrame();
}

void nvdisp_disp0::WaitForComposite() {
    const auto start = std::chrono::steady_clock::now();
    system.GPU().WaitForComposite();
    const u64 waited_us = VideoCore::Perf::ElapsedUs(start);
    VideoCore::Perf::Add(VideoCore::Perf::Counter::VsyncComposeWaitUs, waited_us);
    VideoCore::FrameTrace::Mark(VideoCore::FrameTrace::Event::ComposeWaitEnd, waited_us);
}

Kernel::KEvent* nvdisp_disp0::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown DISP Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
