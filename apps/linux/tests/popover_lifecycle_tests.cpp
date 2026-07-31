#include "CueletPopoverLifecycle.h"
#include "TestSupport.h"

#include <gtk/gtk.h>

namespace {

void closedPopoverSurvivesCurrentDispatch()
{
    GtkWidget* parent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_ref_sink(parent);
    GtkWidget* popover = gtk_popover_new();
    gpointer livePopover = popover;
    g_object_add_weak_pointer(G_OBJECT(popover), &livePopover);
    gtk_widget_set_parent(popover, parent);
    cuelet_linux::installDeferredPopoverCleanup(GTK_POPOVER(popover));

    g_signal_emit_by_name(popover, "closed");
    CUELET_REQUIRE(gtk_widget_get_parent(popover) == parent);
    CUELET_REQUIRE(livePopover != nullptr);

    while (g_main_context_iteration(nullptr, false)) {
    }
    CUELET_REQUIRE(gtk_widget_get_first_child(parent) == nullptr);
    CUELET_REQUIRE(livePopover == nullptr);

    g_object_unref(parent);
}

} // namespace

int main()
{
    gtk_init();
    return cuelet_linux::tests::run("cuelet popover lifecycle tests", [] {
        closedPopoverSurvivesCurrentDispatch();
    });
}
