#include "CueletWindow.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

void CueletWindow::scheduleVisualCaptureFromEnvironment()
{
    if (visualCaptureScheduled_) {
        return;
    }

    const char* captureEnabled = std::getenv("CUELET_TEST_CAPTURE_ENABLED");
    if (!captureEnabled || std::string(captureEnabled) != "1") {
        return;
    }

    const char* requestedPath = std::getenv("CUELET_TEST_CAPTURE_PATH");
    if (!requestedPath || *requestedPath == '\0') {
        return;
    }
    visualCaptureScheduled_ = true;

    const std::filesystem::path capturePath =
        std::filesystem::u8path(requestedPath).lexically_normal();
    std::error_code pathError;
    if (!capturePath.is_absolute()
        || capturePath.extension() != ".png"
        || !std::filesystem::is_directory(capturePath.parent_path(), pathError)
        || pathError) {
        g_printerr(
            "cuelet: CUELET_TEST_CAPTURE_PATH must be an absolute PNG path "
            "inside an existing directory.\n");
        g_application_quit(G_APPLICATION(application_));
        return;
    }

    const char* requestedState = std::getenv("CUELET_TEST_VISUAL_STATE");
    const std::string state = requestedState ? requestedState : "populated";
    if (state == "context-menu") {
        g_printerr(
            "cuelet: context menus use a separate Wayland popup surface and "
            "cannot be captured by the application renderer.\n");
        g_application_quit(G_APPLICATION(application_));
        return;
    }
    if (state == "search-results") {
        gtk_editable_set_text(GTK_EDITABLE(searchEntry_), "Fixture");
    } else if (state == "search-no-results") {
        gtk_editable_set_text(GTK_EDITABLE(searchEntry_), "no such cuelet sound");
    } else if (state == "category") {
        const auto category = std::find_if(
            categories_.begin(), categories_.end(), [](const cuelet::Category& item) {
                return item.editable;
            });
        if (category != categories_.end()) {
            selection_ = SidebarSelection{SidebarKind::Category, category->id};
            refreshAll();
        }
    } else if (state == "collapsed") {
        gtk_window_set_default_size(GTK_WINDOW(window_), 720, 720);
    } else if (state == "narrow-grid") {
        gtk_window_set_default_size(GTK_WINDOW(window_), 720, 720);
        adw_navigation_split_view_set_show_content(splitView_, TRUE);
    } else if (state == "maximized") {
        gtk_window_maximize(GTK_WINDOW(window_));
    }

    struct CaptureRequest {
        CueletWindow* self = nullptr;
        std::string path;
        std::string state;
        bool prepared = false;
    };
    auto* request = new CaptureRequest{
        this,
        capturePath.u8string(),
        state,
        false,
    };

    visualCaptureSourceId_ = g_timeout_add_full(
        G_PRIORITY_DEFAULT,
        700,
        +[](gpointer userData) -> gboolean {
            auto* request = static_cast<CaptureRequest*>(userData);
            auto* self = request->self;

            if (!request->prepared) {
                request->prepared = true;
                if (request->state == "category-editor") {
                    const auto category = std::find_if(
                        self->categories_.begin(),
                        self->categories_.end(),
                        [](const cuelet::Category& item) {
                            return item.editable;
                        });
                    if (category != self->categories_.end()) {
                        self->promptRenameCategory(category->id);
                    }
                    return G_SOURCE_CONTINUE;
                }
                if (request->state == "delete-managed-file") {
                    const auto clip = std::find_if(
                        self->clips_.begin(),
                        self->clips_.end(),
                        [](const cuelet::SoundClip& item) {
                            return item.storageMode == cuelet::SoundStorageMode::Managed
                                && !item.missing
                                && !item.absolutePath.empty();
                        });
                    if (clip != self->clips_.end()) {
                        self->confirmDeleteManagedFile(clip->relativePath);
                    }
                    return G_SOURCE_CONTINUE;
                }
                if (request->state == "playback") {
                    const auto clip = std::find_if(
                        self->clips_.begin(),
                        self->clips_.end(),
                        [](const cuelet::SoundClip& item) {
                            return !item.missing && !item.absolutePath.empty();
                        });
                    if (clip != self->clips_.end()) {
                        self->playSound(clip->relativePath);
                    }
                    return G_SOURCE_CONTINUE;
                }
                if (request->state == "settings"
                    || request->state == "audio-routing") {
                    self->showPreferences();
                    if (request->state == "audio-routing"
                        && self->preferencesDialog_) {
                        adw_preferences_dialog_set_visible_page_name(
                            ADW_PREFERENCES_DIALOG(self->preferencesDialog_),
                            "audio");
                    }
                    return G_SOURCE_CONTINUE;
                }
            }

            GtkWidget* widget = GTK_WIDGET(self->window_);
            const int width = gtk_widget_get_width(widget);
            const int height = gtk_widget_get_height(widget);
            GdkPaintable* paintable = gtk_widget_paintable_new(widget);
            GtkSnapshot* snapshot = gtk_snapshot_new();
            gdk_paintable_snapshot(paintable, snapshot, width, height);
            GskRenderNode* node = gtk_snapshot_free_to_node(snapshot);
            const graphene_rect_t bounds = GRAPHENE_RECT_INIT(
                0,
                0,
                static_cast<float>(width),
                static_cast<float>(height));
            GtkNative* native = gtk_widget_get_native(widget);
            GskRenderer* renderer = native ? gtk_native_get_renderer(native) : nullptr;
            GdkTexture* texture =
                renderer && node
                ? gsk_renderer_render_texture(renderer, node, &bounds)
                : nullptr;
            const bool saved = texture
                && gdk_texture_save_to_png(texture, request->path.c_str());

            if (texture) {
                g_object_unref(texture);
            }
            if (node) {
                gsk_render_node_unref(node);
            }
            g_object_unref(paintable);

            if (saved) {
                g_print("cuelet: captured %s\n", request->path.c_str());
            } else {
                g_printerr(
                    "cuelet: could not capture %s\n",
                    request->path.c_str());
            }
            self->audio_.stopAll();
            if (self->virtualMicrophoneService_) {
                self->virtualMicrophoneService_->shutdown();
            }
            self->visualCaptureSourceId_ = 0;
            g_application_quit(G_APPLICATION(self->application_));
            return G_SOURCE_REMOVE;
        },
        request,
        +[](gpointer userData) {
            delete static_cast<CaptureRequest*>(userData);
        });
}
