#include "CueletAboutDialog.h"
#include "CueletVersion.h"
#include "TestSupport.h"

#include <adwaita.h>

#include <cmath>
#include <string>

namespace {

GtkWidget* labelWithText(GtkWidget* widget, const char* text)
{
    if (GTK_IS_LABEL(widget)
        && std::string(gtk_label_get_text(GTK_LABEL(widget))) == text) {
        return widget;
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        if (GtkWidget* match = labelWithText(child, text)) {
            return match;
        }
    }
    return nullptr;
}

bool colorsMatch(const GdkRGBA& first, const GdkRGBA& second)
{
    constexpr double tolerance = 1.0 / 65535.0;
    return std::abs(first.red - second.red) < tolerance
        && std::abs(first.green - second.green) < tolerance
        && std::abs(first.blue - second.blue) < tolerance
        && std::abs(first.alpha - second.alpha) < tolerance;
}

unsigned int labelCountWithCssClass(GtkWidget* widget, const char* cssClass)
{
    unsigned int count = GTK_IS_LABEL(widget)
            && gtk_widget_has_css_class(widget, cssClass)
        ? 1
        : 0;
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        count += labelCountWithCssClass(child, cssClass);
    }
    return count;
}

void aboutMetadataMatchesReleaseMetadata()
{
    AdwAboutDialog* about = cuelet_linux::createAboutDialog();
    g_object_ref_sink(about);

    CUELET_REQUIRE(std::string(adw_about_dialog_get_application_icon(about))
        == "io.cuelet.Cuelet");
    CUELET_REQUIRE(std::string(adw_about_dialog_get_application_name(about))
        == "Cuelet");
    CUELET_REQUIRE(std::string(adw_about_dialog_get_developer_name(about))
        == "Cuelet contributors");
    CUELET_REQUIRE(std::string(adw_about_dialog_get_version(about))
        == CUELET_VERSION);
    CUELET_REQUIRE(std::string(adw_about_dialog_get_comments(about))
        == "A cross-platform soundboard and virtual microphone.\n\n"
           "Cuelet is free and open-source software licensed under the "
           "GNU Affero General Public License version 3 only.");
    CUELET_REQUIRE(std::string(adw_about_dialog_get_website(about))
        == "https://github.com/okixk/cuelet");
    CUELET_REQUIRE(std::string(adw_about_dialog_get_issue_url(about))
        == "https://github.com/okixk/cuelet/issues");
    CUELET_REQUIRE(adw_about_dialog_get_license_type(about)
        == GTK_LICENSE_AGPL_3_0_ONLY);
    CUELET_REQUIRE(gtk_widget_has_css_class(
        GTK_WIDGET(about), "cuelet-about-dialog"));

    g_object_unref(about);
}

void onlyAppNameUsesBrandColor()
{
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/io/cuelet/linux/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    AdwAboutDialog* about = cuelet_linux::createAboutDialog();
    g_object_ref_sink(about);
    GtkWidget* parent = adw_window_new();
    g_object_ref_sink(parent);
    gtk_window_present(GTK_WINDOW(parent));
    adw_dialog_present(ADW_DIALOG(about), parent);
    while (g_main_context_iteration(nullptr, false)) {
    }
    CUELET_REQUIRE(cuelet_linux::styleAboutHeading(about));
    GtkWidget* appName = labelWithText(GTK_WIDGET(about), "Cuelet");
    GtkWidget* developerName = labelWithText(GTK_WIDGET(about), "Cuelet contributors");
    CUELET_REQUIRE(appName != nullptr);
    CUELET_REQUIRE(developerName != nullptr);
    CUELET_REQUIRE(gtk_widget_has_css_class(appName, "cuelet-about-heading"));
    CUELET_REQUIRE(!gtk_widget_has_css_class(
        developerName, "cuelet-about-heading"));
    CUELET_REQUIRE(labelCountWithCssClass(
        GTK_WIDGET(about), "cuelet-about-heading") == 1);

    GdkRGBA expectedBrand;
    GdkRGBA appNameColor;
    GdkRGBA developerColor;
    CUELET_REQUIRE(gdk_rgba_parse(&expectedBrand, "#6a00ff"));
    gtk_widget_get_color(appName, &appNameColor);
    gtk_widget_get_color(developerName, &developerColor);
    CUELET_REQUIRE(colorsMatch(appNameColor, expectedBrand));
    CUELET_REQUIRE(!colorsMatch(developerColor, expectedBrand));

    adw_dialog_force_close(ADW_DIALOG(about));
    gtk_window_destroy(GTK_WINDOW(parent));
    g_object_unref(about);
    g_object_unref(parent);
    g_object_unref(provider);
}

} // namespace

int main()
{
    adw_init();
    return cuelet_linux::tests::run("cuelet About dialog tests", [] {
        aboutMetadataMatchesReleaseMetadata();
        onlyAppNameUsesBrandColor();
    });
}
