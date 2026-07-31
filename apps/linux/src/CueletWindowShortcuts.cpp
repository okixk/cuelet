#include "CueletWindow.h"

#include "CueletWindowHelpers.h"

#include <functional>
#include <vector>

namespace {

constexpr const char* shortcutSoundIdKey = "cuelet-shortcut-sound-id";
constexpr const char* shortcutGlobalToggleKey = "cuelet-shortcut-global-toggle-id";
constexpr const char* portalStatusRowKey = "cuelet-portal-status-row";
constexpr const char* shortcutsGroupKey = "cuelet-shortcuts-group";

std::string overallPortalSummary(
    const cuelet_linux::LinuxGlobalShortcutsController& controller)
{
    using cuelet_linux::PortalOverallState;
    const std::string version = controller.portalVersion() > 0
        ? "Portal version " + std::to_string(controller.portalVersion()) + ". "
        : std::string{};
    switch (controller.overallState()) {
    case PortalOverallState::NotStarted:
        return version + "No global shortcuts are currently requested.";
    case PortalOverallState::Unavailable:
        return "GlobalShortcuts is unavailable. Use the GNOME custom-shortcut command fallback.";
    case PortalOverallState::Connecting:
        return version + "Creating a global-shortcut session.";
    case PortalOverallState::Pending:
        return version + "Waiting for desktop confirmation.";
    case PortalOverallState::Active:
        return version + "All requested global shortcuts are active.";
    case PortalOverallState::Partial:
        return version + "Some shortcuts are active; others were not approved.";
    case PortalOverallState::Denied:
        return version + "No requested shortcuts were approved.";
    case PortalOverallState::Disconnected:
        return "The portal disconnected. Local and GNOME command fallbacks remain available.";
    case PortalOverallState::Error:
        return version + "Portal registration failed. Use the GNOME command fallback.";
    case PortalOverallState::Stopped:
        return "The global-shortcut session is closed.";
    }
    return "Global shortcut status is unknown.";
}

} // namespace

void CueletWindow::syncGlobalShortcuts()
{
    if (globalShortcuts_) {
        globalShortcuts_->setDesiredShortcuts(
            cuelet_linux::portalShortcutSpecs(clips_));
    }
}

void CueletWindow::handleGlobalShortcutActivation(const std::string& soundId)
{
    const auto* clip = cuelet_linux::soundByStableId(clips_, soundId);
    if (!clip) {
        return;
    }
    if (clip->missing || clip->absolutePath.empty()) {
        showToast("The globally selected sound is missing from disk.");
        return;
    }
    const std::string relativePath = clip->relativePath;
    playSound(relativePath);
}

std::string CueletWindow::shortcutStatusText(const cuelet::SoundClip& clip) const
{
    if (!clip.shortcut) {
        return "No shortcut assigned.";
    }
    if (!clip.shortcut->global) {
        return "Local application shortcut · " + clip.shortcut->label
            + " · works while Cuelet is focused.";
    }
    const auto status = globalShortcuts_
        ? globalShortcuts_->statusForSound(clip.id)
        : std::nullopt;
    if (!status) {
        return "Global portal shortcut · Not connected · Preferred "
            + clip.shortcut->label + ".";
    }

    const std::string prefix = "Global portal shortcut · ";
    const auto withDetail = [&](std::string text) {
        if (!status->detail.empty()) {
            text += " · " + status->detail;
        }
        return text;
    };
    switch (status->state) {
    case cuelet_linux::PortalShortcutState::Active:
        return prefix + "Active · "
            + (status->triggerDescription.empty()
                ? clip.shortcut->label
                : status->triggerDescription);
    case cuelet_linux::PortalShortcutState::Pending:
        return prefix + "Pending desktop confirmation · Preferred "
            + clip.shortcut->label;
    case cuelet_linux::PortalShortcutState::Denied:
        return withDetail(prefix + "Denied or not approved · Local fallback "
            + clip.shortcut->label);
    case cuelet_linux::PortalShortcutState::Unavailable:
        return withDetail(
            prefix + "Unavailable · Local fallback " + clip.shortcut->label);
    case cuelet_linux::PortalShortcutState::Disconnected:
        return withDetail(
            prefix + "Disconnected · Local fallback " + clip.shortcut->label);
    case cuelet_linux::PortalShortcutState::Error:
        return withDetail(
            prefix + "Registration failed · Local fallback " + clip.shortcut->label);
    }
    return prefix + "Unknown state";
}

std::string CueletWindow::shortcutBadgeText(const cuelet::SoundClip& clip) const
{
    if (!clip.shortcut) {
        return {};
    }
    if (!clip.shortcut->global) {
        return clip.shortcut->label;
    }
    const auto status = globalShortcuts_
        ? globalShortcuts_->statusForSound(clip.id)
        : std::nullopt;
    if (!status) {
        return "Global pending";
    }
    switch (status->state) {
    case cuelet_linux::PortalShortcutState::Active:
        return status->triggerDescription.empty()
            ? clip.shortcut->label
            : status->triggerDescription;
    case cuelet_linux::PortalShortcutState::Pending: return "Global pending";
    case cuelet_linux::PortalShortcutState::Denied: return "Global denied";
    case cuelet_linux::PortalShortcutState::Unavailable: return "Local fallback";
    case cuelet_linux::PortalShortcutState::Disconnected: return "Local fallback";
    case cuelet_linux::PortalShortcutState::Error: return "Global failed";
    }
    return clip.shortcut->label;
}

void CueletWindow::setShortcutGlobal(
    const std::string& relativePath,
    bool global)
{
    auto* clip = clipByPath(relativePath);
    if (!clip || !clip->shortcut || clip->shortcut->global == global) {
        return;
    }
    clip->shortcut->global = global;
    saveMetadata();
    refreshContent();
    refreshShortcutPreferenceRows();
    showToast(global
        ? "Requested a global shortcut. GNOME may ask for confirmation."
        : "The shortcut now works only while Cuelet is focused.");
}

void CueletWindow::refreshShortcutPreferenceRows()
{
    if (!preferencesDialog_ || !globalShortcuts_) {
        return;
    }

    auto* portalStatusRow = static_cast<GtkWidget*>(g_object_get_data(
        G_OBJECT(preferencesDialog_),
        portalStatusRowKey));
    if (portalStatusRow && ADW_IS_ACTION_ROW(portalStatusRow)) {
        const std::string summary = overallPortalSummary(*globalShortcuts_)
            + " Cuelet must remain running; closing its window ends the session.";
        const std::string escapedSummary = cuelet_linux::escapeMarkup(summary);
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(portalStatusRow),
            escapedSummary.c_str());
    }

    auto* shortcutsGroup = static_cast<GtkWidget*>(g_object_get_data(
        G_OBJECT(preferencesDialog_),
        shortcutsGroupKey));
    if (!shortcutsGroup) {
        return;
    }

    std::vector<GtkWidget*> shortcutRows;
    std::vector<GtkWidget*> globalButtons;
    std::function<void(GtkWidget*)> collect = [&](GtkWidget* widget) {
        if (!widget) {
            return;
        }
        const char* soundId = static_cast<const char*>(
            g_object_get_data(G_OBJECT(widget), shortcutSoundIdKey));
        if (soundId && ADW_IS_ACTION_ROW(widget)) {
            shortcutRows.push_back(widget);
        }
        const char* toggleId = static_cast<const char*>(
            g_object_get_data(G_OBJECT(widget), shortcutGlobalToggleKey));
        if (toggleId && GTK_IS_BUTTON(widget)) {
            globalButtons.push_back(widget);
        }
        for (GtkWidget* child = gtk_widget_get_first_child(widget);
             child;
             child = gtk_widget_get_next_sibling(child)) {
            collect(child);
        }
    };
    collect(shortcutsGroup);

    for (GtkWidget* row : shortcutRows) {
        const char* soundId = static_cast<const char*>(
            g_object_get_data(G_OBJECT(row), shortcutSoundIdKey));
        const auto* clip = cuelet_linux::soundByStableId(
            clips_, soundId ? soundId : "");
        if (clip) {
            const std::string subtitle = shortcutStatusText(*clip);
            const std::string escapedSubtitle = cuelet_linux::escapeMarkup(subtitle);
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(row),
                escapedSubtitle.c_str());
        }
    }
    for (GtkWidget* button : globalButtons) {
        const char* soundId = static_cast<const char*>(
            g_object_get_data(G_OBJECT(button), shortcutGlobalToggleKey));
        const auto* clip = cuelet_linux::soundByStableId(
            clips_, soundId ? soundId : "");
        if (clip && clip->shortcut) {
            gtk_button_set_label(
                GTK_BUTTON(button),
                clip->shortcut->global ? "Use Locally Only" : "Request Global");
            gtk_widget_set_tooltip_text(
                button,
                clip->shortcut->global
                    ? "Disable portal registration but keep the local shortcut"
                    : "Ask the desktop portal to register this shortcut globally");
        }
    }
}
