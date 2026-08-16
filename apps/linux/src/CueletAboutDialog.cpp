#include "CueletAboutDialog.h"

#include "CueletVersion.h"

namespace cuelet_linux {

namespace {

GtkWidget* findApplicationNameHeading(GtkWidget* widget, const char* applicationName)
{
    if (GTK_IS_LABEL(widget)
        && gtk_widget_has_css_class(widget, "title-1")
        && g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), applicationName) == 0) {
        return widget;
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        if (GtkWidget* match = findApplicationNameHeading(child, applicationName)) {
            return match;
        }
    }
    return nullptr;
}

} // namespace

AdwAboutDialog* createAboutDialog()
{
    AdwAboutDialog* about = ADW_ABOUT_DIALOG(adw_about_dialog_new());
    adw_about_dialog_set_application_icon(about, "io.cuelet.Cuelet");
    adw_about_dialog_set_application_name(about, "Cuelet");
    adw_about_dialog_set_developer_name(about, "Cuelet contributors");
    adw_about_dialog_set_version(about, CUELET_VERSION);
    adw_about_dialog_set_comments(
        about,
        "A cross-platform soundboard and virtual microphone.\n\n"
        "Cuelet is free and open-source software licensed under the "
        "GNU Affero General Public License version 3 only.");
    adw_about_dialog_set_website(about, "https://github.com/okixk/cuelet");
    adw_about_dialog_set_issue_url(about, "https://github.com/okixk/cuelet/issues");
    adw_about_dialog_set_license_type(about, GTK_LICENSE_AGPL_3_0_ONLY);
    gtk_widget_add_css_class(GTK_WIDGET(about), "cuelet-about-dialog");
    return about;
}

bool styleAboutHeading(AdwAboutDialog* about)
{
    GtkWidget* heading = findApplicationNameHeading(
        GTK_WIDGET(about),
        adw_about_dialog_get_application_name(about));
    if (!heading) {
        return false;
    }
    gtk_widget_add_css_class(heading, "cuelet-about-heading");
    return true;
}

} // namespace cuelet_linux
