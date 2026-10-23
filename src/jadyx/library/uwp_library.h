// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Jadyx {
/// Returned by ShowGameLibrary instead of a game when the player picks a firmware library applet in
/// the settings home: the boot opens that applet's NCA as the session, without a game. The Mii
/// editor is the only one so far, and the console has no other way into it.
inline constexpr std::string_view AppletRequestPrefix = "@applet:";
inline constexpr std::string_view MiiEditorRequest = "@applet:mii_edit";
// Runs on the CoreWindow thread, before constructing the guest renderer. Returns
// an absolute internal path or token-scoped external path, or nullopt on exit.
// The seeding callback and directory scan run off the UI thread. A non-empty auto_pick (a path
// relative to the root, boot.cfg "library_pick=") is chosen two seconds after the scan, for
// unattended library -> game -> library runs.
std::optional<std::string> ShowGameLibrary(void* core_window, unsigned width, unsigned height,
                                         const std::filesystem::path& root,
                                         const std::function<void()>& seed,
                                         std::string_view auto_pick = {},
                                         std::string_view initial_notice = {});
} // namespace Jadyx
