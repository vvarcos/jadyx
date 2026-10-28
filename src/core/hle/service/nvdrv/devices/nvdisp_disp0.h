// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include "common/common_types.h"
#include "common/math_util.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvnflinger/hwc_layer.h"

namespace Service::Nvidia::NvCore {
class Container;
class NvMap;
} // namespace Service::Nvidia::NvCore

namespace Service::Nvidia::Devices {

class nvmap;

/// Caps how often the guest may present ("frame_limit=<fps>", 0 for no cap). The emulator's own
/// speed limiter is off in multicore builds, so with the host vsync unlocked a game that advances
/// its simulation once per presented frame runs as fast as the emulator can go; this paces those
/// presents to the rate the game was designed for.
void SetFrameLimit(u32 fps);

class nvdisp_disp0 final : public nvdevice {
public:
    explicit nvdisp_disp0(Core::System& system_, NvCore::Container& core);
    ~nvdisp_disp0() override;

    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output) override;
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output) override;
    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output,
                    std::span<u8> inline_output) override;

    void OnOpen(NvCore::SessionId session_id, DeviceFD fd) override;
    void OnClose(DeviceFD fd) override;

    /// Performs a screen flip, compositing each buffer.
    void Composite(std::span<const Nvnflinger::HwcLayer> sorted_layers);

    void WaitForComposite();

    Kernel::KEvent* QueryEvent(u32 event_id) override;

    // Pulse: the display device keeps no state of its own; composition belongs to vi.
    bool PulseCapture(DeviceFD fd, Service::Pulse::StateWriter& out) const override {
        return true;
    }

private:
    NvCore::Container& container;
    NvCore::NvMap& nvmap;
};

} // namespace Service::Nvidia::Devices
