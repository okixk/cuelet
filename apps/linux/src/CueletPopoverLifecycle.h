#pragma once

#include <gtk/gtk.h>

namespace cuelet_linux {

// GtkPopoverMenu closes before its selected GAction has finished dispatching.
// Keep the popover parented until the current main-loop dispatch completes.
void installDeferredPopoverCleanup(GtkPopover* popover);

} // namespace cuelet_linux
