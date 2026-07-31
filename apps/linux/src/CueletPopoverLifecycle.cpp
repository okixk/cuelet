#include "CueletPopoverLifecycle.h"

namespace cuelet_linux {

void installDeferredPopoverCleanup(GtkPopover* popover)
{
    g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover* closed, gpointer) {
        GtkWidget* widget = GTK_WIDGET(closed);
        constexpr const char* pendingKey = "cuelet-popover-cleanup-pending";
        if (g_object_get_data(G_OBJECT(widget), pendingKey)) {
            return;
        }

        g_object_set_data(G_OBJECT(widget), pendingKey, GINT_TO_POINTER(1));
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer data) -> gboolean {
                GtkWidget* popoverWidget = GTK_WIDGET(data);
                g_object_set_data(
                    G_OBJECT(popoverWidget),
                    "cuelet-popover-cleanup-pending",
                    nullptr);
                if (gtk_widget_get_parent(popoverWidget)) {
                    gtk_widget_unparent(popoverWidget);
                }
                return G_SOURCE_REMOVE;
            },
            g_object_ref(widget),
            g_object_unref);
    }), nullptr);
}

} // namespace cuelet_linux
