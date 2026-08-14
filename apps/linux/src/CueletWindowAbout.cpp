#include "CueletWindow.h"

#include "CueletAboutDialog.h"

void CueletWindow::showAbout()
{
    if (!aboutDialog_) {
        aboutDialog_ = cuelet_linux::createAboutDialog();
        g_object_ref_sink(aboutDialog_);
        g_signal_connect(
            aboutDialog_,
            "closed",
            G_CALLBACK(+[](AdwDialog* dialog, gpointer userData) {
                auto* self = static_cast<CueletWindow*>(userData);
                if (ADW_DIALOG(self->aboutDialog_) == dialog) {
                    g_clear_object(&self->aboutDialog_);
                }
            }),
            this);
    }

    adw_dialog_present(ADW_DIALOG(aboutDialog_), GTK_WIDGET(window_));
    cuelet_linux::styleAboutHeading(aboutDialog_);
}
