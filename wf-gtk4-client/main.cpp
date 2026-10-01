#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_map>
#include <vector>

#include <gio/gio.h>

#include "protocol.hpp"

GtkApplication *app;

struct custom_data
{
    uint32_t id;
    GtkWidget *area;
};

static GtkCssProvider *decorator_css_provider = nullptr;
static GSettings *desktop_interface_settings = nullptr;
static gulong color_scheme_changed_handler = 0;

static std::unordered_map<GtkWidget *, GtkWidget *> window_title_widgets;
static std::unordered_map<GtkWidget *, GtkWidget *> window_icon_widgets;
static std::unordered_map<GtkWidget *, GtkWidget *> window_control_widgets;


/* ============================================================
 * Forward declarations
 * ============================================================ */

static void add_tab_button(window_data *wdata, window_data *cdata);
static void clear_group_tabs(uint32_t group_id);
static void reparent_group(uint32_t group_id, window_data *last_parent);
static void refresh_group(uint32_t group_id);
static void ungroup(window_data *wdata, bool notify_server);

static void on_area_resized(
    GtkDrawingArea *area,
    int width,
    int height,
    gpointer data
);

static void apply_decorator_css();

static void toggle_maximize(
    GtkWindow *window
);


/* ============================================================
 * GNOME color scheme
 * ============================================================ */

static bool is_gnome_dark_mode()
{
    if (!desktop_interface_settings)
    {
        desktop_interface_settings =
            g_settings_new("org.gnome.desktop.interface");
    }

    gchar *scheme =
        g_settings_get_string(
            desktop_interface_settings,
            "color-scheme"
        );

    const bool dark =
        scheme &&
        g_strcmp0(scheme, "prefer-dark") == 0;

    g_free(scheme);

    return dark;
}


/* ============================================================
 * CSS
 * ============================================================ */

static const char *DECORATOR_DARK_CSS = R"CSS(

window.wf-decorator-window {
    background-color: #1e1e1e;
    background-image: none;

    border-radius: 10px;

    box-shadow: none;

    padding: 0;
    margin: 0;

    color: #ffffff;
}

window.wf-decorator-window > headerbar,
window.wf-decorator-window > headerbar windowhandle,
window.wf-decorator-window > headerbar windowhandle > box {
    background-color: #1e1e1e;
    background-image: none;

    border: none;
    box-shadow: none;

    color: #ffffff;
}

window.wf-decorator-window > headerbar {
    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0;

    border-radius: 9px 9px 0 0;

    color: #ffffff;
}


/* ------------------------------------------------------------
 * Title
 * ------------------------------------------------------------ */

window.wf-decorator-window > headerbar .wf-title {
    min-width: 1px;

    margin: 0;
    padding: 0;

    color: #ffffff;

    font-weight: 600;

    opacity: 1;

    halign: center;
    valign: center;

    hexpand: true;
    vexpand: false;
}

window.wf-decorator-window > headerbar label.wf-title,
window.wf-decorator-window > headerbar .wf-title label {
    color: #ffffff;

    font-weight: 600;

    opacity: 1;

    margin: 0;
    padding: 0;
}


/* ------------------------------------------------------------
 * Controls
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-controls {
    min-width: 70px;
    width: 70px;

    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0 0 0 10px;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;

    spacing: 6px;

    halign: start;
    valign: center;

    hexpand: false;
    vexpand: false;
}

window.wf-decorator-window button.wf-control,
window.wf-decorator-window button.wf-control:hover,
window.wf-decorator-window button.wf-control:active,
window.wf-decorator-window button.wf-control:checked,
window.wf-decorator-window button.wf-control:focus,
window.wf-decorator-window button.wf-control:focus-visible {
    min-width: 14px;
    min-height: 14px;

    width: 14px;
    height: 14px;

    margin: 0;
    padding: 0;

    border: none;
    border-radius: 999px;

    background-image: none;

    box-shadow: none;
    outline: none;

    halign: center;
    valign: center;

    hexpand: false;
    vexpand: false;

    opacity: 1;
}

window.wf-decorator-window button.wf-control > * {
    margin: 0;
    padding: 0;

    min-width: 0;
    min-height: 0;
}

window.wf-decorator-window button.wf-close {
    background-color: #ff5f57;
}

window.wf-decorator-window button.wf-minimize {
    background-color: #febc2e;
}

window.wf-decorator-window button.wf-maximize {
    background-color: #28c840;
}


/* ------------------------------------------------------------
 * Icon
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-icon-area {
    min-width: 38px;
    width: 38px;

    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;

    halign: center;
    valign: center;

    hexpand: false;
    vexpand: false;
}

window.wf-decorator-window image.wf-app-icon {
    min-width: 18px;
    min-height: 18px;

    width: 18px;
    height: 18px;

    margin: 0;
    padding: 0;

    opacity: 1;

    halign: center;
    valign: center;
}


/* ------------------------------------------------------------
 * Tabs
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-tabs-container {
    min-width: 0;

    margin: 0;
    padding: 0;

    background: transparent;
    background-image: none;
}

window.wf-decorator-window scrolledwindow.wf-tabs-scroll {
    min-width: 0;
    min-height: 30px;

    margin: 0;
    padding: 0;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;
}

window.wf-decorator-window scrolledwindow.wf-tabs-scroll > viewport {
    background: transparent;
    background-image: none;
}

window.wf-decorator-window .wf-tabs-box {
    min-height: 30px;

    margin: 0;
    padding: 0;

    background: transparent;
    background-image: none;
}

window.wf-decorator-window button.wf-tab-button {
    min-width: 28px;
    min-height: 28px;

    margin: 0;
    padding: 3px 6px;

    border: none;
    border-radius: 6px;

    background: transparent;
    background-image: none;

    box-shadow: none;
}

window.wf-decorator-window button.wf-tab-button:hover {
    background-color: rgba(255,255,255,0.08);
    background-image: none;
}

window.wf-decorator-window button.wf-tab-button:active {
    background-color: rgba(255,255,255,0.12);
    background-image: none;
}

window.wf-decorator-window .wf-content {
    border: none;
    background: transparent;
}

)CSS";


static const char *DECORATOR_LIGHT_CSS = R"CSS(

window.wf-decorator-window {
    background-color: #ffffff;
    background-image: none;

    border-radius: 10px;

    box-shadow: none;

    padding: 0;
    margin: 0;

    color: #000000;
}

window.wf-decorator-window > headerbar,
window.wf-decorator-window > headerbar windowhandle,
window.wf-decorator-window > headerbar windowhandle > box {
    background-color: #ffffff;
    background-image: none;

    border: none;
    box-shadow: none;

    color: #000000;
}

window.wf-decorator-window > headerbar {
    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0;

    border-radius: 9px 9px 0 0;

    color: #000000;
}


/* ------------------------------------------------------------
 * Title
 * ------------------------------------------------------------ */

window.wf-decorator-window > headerbar .wf-title {
    min-width: 1px;

    margin: 0;
    padding: 0;

    color: #000000;

    font-weight: 600;

    opacity: 1;

    halign: center;
    valign: center;

    hexpand: true;
    vexpand: false;
}

window.wf-decorator-window > headerbar label.wf-title,
window.wf-decorator-window > headerbar .wf-title label {
    color: #000000;

    font-weight: 600;

    opacity: 1;

    margin: 0;
    padding: 0;
}


/* ------------------------------------------------------------
 * Controls
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-controls {
    min-width: 70px;
    width: 70px;

    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0 0 0 10px;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;

    spacing: 6px;

    halign: start;
    valign: center;

    hexpand: false;
    vexpand: false;
}

window.wf-decorator-window button.wf-control,
window.wf-decorator-window button.wf-control:hover,
window.wf-decorator-window button.wf-control:active,
window.wf-decorator-window button.wf-control:checked,
window.wf-decorator-window button.wf-control:focus,
window.wf-decorator-window button.wf-control:focus-visible {
    min-width: 14px;
    min-height: 14px;

    width: 14px;
    height: 14px;

    margin: 0;
    padding: 0;

    border: none;
    border-radius: 999px;

    background-image: none;

    box-shadow: none;
    outline: none;

    halign: center;
    valign: center;

    hexpand: false;
    vexpand: false;

    opacity: 1;
}

window.wf-decorator-window button.wf-control > * {
    margin: 0;
    padding: 0;

    min-width: 0;
    min-height: 0;
}

window.wf-decorator-window button.wf-close {
    background-color: #ff5f57;
}

window.wf-decorator-window button.wf-minimize {
    background-color: #febc2e;
}

window.wf-decorator-window button.wf-maximize {
    background-color: #28c840;
}


/* ------------------------------------------------------------
 * Icon
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-icon-area {
    min-width: 38px;
    width: 38px;

    min-height: 38px;
    height: 38px;

    margin: 0;
    padding: 0;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;

    halign: center;
    valign: center;

    hexpand: false;
    vexpand: false;
}

window.wf-decorator-window image.wf-app-icon {
    min-width: 18px;
    min-height: 18px;

    width: 18px;
    height: 18px;

    margin: 0;
    padding: 0;

    opacity: 1;

    halign: center;
    valign: center;
}


/* ------------------------------------------------------------
 * Tabs
 * ------------------------------------------------------------ */

window.wf-decorator-window .wf-tabs-container {
    min-width: 0;

    margin: 0;
    padding: 0;

    background: transparent;
    background-image: none;
}

window.wf-decorator-window scrolledwindow.wf-tabs-scroll {
    min-width: 0;
    min-height: 30px;

    margin: 0;
    padding: 0;

    border: none;

    background: transparent;
    background-image: none;

    box-shadow: none;
}

window.wf-decorator-window scrolledwindow.wf-tabs-scroll > viewport {
    background: transparent;
    background-image: none;
}

window.wf-decorator-window .wf-tabs-box {
    min-height: 30px;

    margin: 0;
    padding: 0;

    background: transparent;
    background-image: none;
}

window.wf-decorator-window button.wf-tab-button {
    min-width: 28px;
    min-height: 28px;

    margin: 0;
    padding: 3px 6px;

    border: none;
    border-radius: 6px;

    background: transparent;
    background-image: none;

    box-shadow: none;
}

window.wf-decorator-window button.wf-tab-button:hover {
    background-color: rgba(0,0,0,0.06);
    background-image: none;
}

window.wf-decorator-window button.wf-tab-button:active {
    background-color: rgba(0,0,0,0.10);
    background-image: none;
}

window.wf-decorator-window .wf-content {
    border: none;
    background: transparent;
}

)CSS";


/* ============================================================
 * Apply CSS
 * ============================================================ */

static void apply_decorator_css()
{
    if (!decorator_css_provider)
    {
        decorator_css_provider =
            gtk_css_provider_new();

        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(decorator_css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    const char *css =
        is_gnome_dark_mode()
            ? DECORATOR_DARK_CSS
            : DECORATOR_LIGHT_CSS;

    gtk_css_provider_load_from_string(
        decorator_css_provider,
        css
    );
}


static void on_gnome_color_scheme_changed(
    GSettings *,
    gchar *,
    gpointer)
{
    apply_decorator_css();
}


/* ============================================================
 * Maximize
 *
 * GTK's GtkWindowHandle already implements titlebar
 * double-click behavior. We keep that path enabled and use
 * the exact same asynchronous GTK maximize/unmaximize API
 * for the custom maximize button.
 *
 * There is deliberately no retry timer here.
 * Wayland maximize/unmaximize is asynchronous and GTK tracks
 * the compositor-confirmed state internally.
 * ============================================================ */

static void toggle_maximize(
    GtkWindow *window)
{
    if (!window)
    {
        return;
    }

    if (gtk_window_is_maximized(window))
    {
        gtk_window_unmaximize(window);
    }
    else
    {
        gtk_window_maximize(window);
    }
}


/* ============================================================
 * GTK activate
 * ============================================================ */

static void activate(
    GtkApplication *application,
    gpointer)
{
    GdkDisplay *display =
        gdk_display_get_default();

    setup_protocol(display);

    GtkSettings *settings =
        gtk_settings_get_default();

    g_object_set(
        settings,
        "gtk-decoration-layout",
        "",

        /*
         * Keep GTK's native GtkWindowHandle double-click path.
         *
         * GtkWindowHandle implements the expected titlebar
         * double-click behavior and this setting tells it to
         * toggle maximization.
         */
        "gtk-titlebar-double-click",
        "toggle-maximize",

        nullptr
    );

    desktop_interface_settings =
        g_settings_new(
            "org.gnome.desktop.interface"
        );

    color_scheme_changed_handler =
        g_signal_connect(
            desktop_interface_settings,
            "changed::color-scheme",
            G_CALLBACK(
                on_gnome_color_scheme_changed
            ),
            nullptr
        );

    apply_decorator_css();

    g_application_hold(
        G_APPLICATION(application)
    );
}


/* ============================================================
 * Close request
 * ============================================================ */

static gboolean on_close_request(
    GtkWindow *,
    gpointer data)
{
    auto *wdata =
        static_cast<window_data *>(data);

    close_request(
        wdata->wf_id
    );

    return true;
}


/* ============================================================
 * Custom control buttons
 * ============================================================ */

static void on_custom_close_clicked(
    GtkButton *,
    gpointer data)
{
    auto *wdata =
        static_cast<window_data *>(data);

    if (!wdata)
    {
        return;
    }

    close_request(
        wdata->wf_id
    );
}


static void on_custom_minimize_clicked(
    GtkButton *,
    gpointer data)
{
    auto *window =
        GTK_WINDOW(data);

    if (!window)
    {
        return;
    }

    gtk_window_minimize(window);
}


static void on_custom_maximize_clicked(
    GtkButton *,
    gpointer data)
{
    auto *window =
        GTK_WINDOW(data);

    toggle_maximize(window);
}


static GtkWidget *create_control_button(
    const char *css_class,
    const char *tooltip,
    GCallback callback,
    gpointer data)
{
    GtkWidget *button =
        gtk_button_new();

    gtk_widget_add_css_class(
        button,
        "wf-control"
    );

    gtk_widget_add_css_class(
        button,
        css_class
    );

    gtk_widget_set_size_request(
        button,
        14,
        14
    );

    gtk_widget_set_halign(
        button,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_valign(
        button,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_hexpand(
        button,
        FALSE
    );

    gtk_widget_set_vexpand(
        button,
        FALSE
    );

    gtk_button_set_has_frame(
        GTK_BUTTON(button),
        FALSE
    );

    gtk_widget_set_focusable(
        button,
        FALSE
    );

    gtk_widget_set_tooltip_text(
        button,
        tooltip
    );

    g_signal_connect(
        button,
        "clicked",
        callback,
        data
    );

    return button;
}


static GtkWidget *create_control_box(
    GtkWidget *window,
    window_data *wdata)
{
    GtkWidget *controls =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            6
        );

    gtk_widget_add_css_class(
        controls,
        "wf-controls"
    );

    gtk_widget_set_halign(
        controls,
        GTK_ALIGN_START
    );

    gtk_widget_set_valign(
        controls,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_hexpand(
        controls,
        FALSE
    );

    gtk_widget_set_vexpand(
        controls,
        FALSE
    );

    GtkWidget *close_button =
        create_control_button(
            "wf-close",
            "Close",
            G_CALLBACK(
                on_custom_close_clicked
            ),
            wdata
        );

    GtkWidget *minimize_button =
        create_control_button(
            "wf-minimize",
            "Minimize",
            G_CALLBACK(
                on_custom_minimize_clicked
            ),
            window
        );

    GtkWidget *maximize_button =
        create_control_button(
            "wf-maximize",
            "Maximize",
            G_CALLBACK(
                on_custom_maximize_clicked
            ),
            window
        );

    gtk_box_append(
        GTK_BOX(controls),
        close_button
    );

    gtk_box_append(
        GTK_BOX(controls),
        minimize_button
    );

    gtk_box_append(
        GTK_BOX(controls),
        maximize_button
    );

    return controls;
}


/* ============================================================
 * Application icon
 * ============================================================ */

static GtkWidget *get_icon(
    std::string app_id)
{
    GtkWidget *image = nullptr;

    auto theme =
        gtk_icon_theme_get_for_display(
            gdk_display_get_default()
        );

    auto _app_id =
        app_id;

    auto dot_pos =
        _app_id.find_last_of(".");

    auto lower_case_app_id =
        app_id;

    for (char &c :
         lower_case_app_id)
    {
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)
            )
        );
    }

    if (gtk_icon_theme_has_icon(
            theme,
            lower_case_app_id.c_str()))
    {
        image =
            gtk_image_new_from_icon_name(
                lower_case_app_id.c_str()
            );
    }
    else if (
        dot_pos != std::string::npos &&
        dot_pos < _app_id.length() - 1)
    {
        _app_id =
            _app_id.substr(
                dot_pos + 1
            );

        if (gtk_icon_theme_has_icon(
                theme,
                _app_id.c_str()))
        {
            image =
                gtk_image_new_from_icon_name(
                    _app_id.c_str()
                );
        }
    }

    if (!image &&
        dot_pos != std::string::npos &&
        dot_pos <
            lower_case_app_id.length() - 1)
    {
        _app_id =
            lower_case_app_id.substr(
                dot_pos + 1
            );

        if (gtk_icon_theme_has_icon(
                theme,
                _app_id.c_str()))
        {
            image =
                gtk_image_new_from_icon_name(
                    _app_id.c_str()
                );
        }
    }

    if (!image)
    {
        image =
            gtk_image_new_from_icon_name(
                app_id.c_str()
            );
    }

    return image;
}


/* ============================================================
 * Drag source
 * ============================================================ */

static GdkContentProvider *drag_prepare_cb(
    GtkDragSource *,
    double,
    double,
    gpointer user_data)
{
    auto *data =
        static_cast<window_data *>(user_data);

    GValue value =
        G_VALUE_INIT;

    g_value_init(
        &value,
        G_TYPE_INT
    );

    g_value_set_int(
        &value,
        static_cast<int>(data->wf_id)
    );

    GdkContentProvider *content_provider =
        gdk_content_provider_new_for_value(
            &value
        );

    g_value_unset(&value);

    gtk_gesture_set_state(
        GTK_GESTURE(
            data->drag_source
        ),
        GTK_EVENT_SEQUENCE_CLAIMED
    );

    return content_provider;
}


static void drag_begin_cb(
    GtkDragSource *,
    GdkDrag *drag,
    gpointer user_data)
{
    auto *data =
        static_cast<window_data *>(user_data);

    GtkDragIcon *drag_icon =
        GTK_DRAG_ICON(
            gtk_drag_icon_get_for_drag(drag)
        );

    GtkWidget *image =
        get_icon(
            data->app_id
        );

    gtk_image_set_pixel_size(
        GTK_IMAGE(image),
        48
    );

    gtk_drag_icon_set_child(
        drag_icon,
        image
    );
}


static void drag_end_cb(
    GtkDragSource *,
    GdkDrag *,
    gboolean,
    gpointer)
{
}


/* ============================================================
 * Scroll
 * ============================================================ */

static void scroll_sync(
    window_data *wdata)
{
    if (!wdata->group.id)
    {
        return;
    }

    auto h_adj =
        gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(
                wdata->scrolled_window
            )
        );

    auto adj =
        gtk_adjustment_new(
            gtk_adjustment_get_value(h_adj),
            gtk_adjustment_get_lower(h_adj),
            gtk_adjustment_get_upper(h_adj),
            gtk_adjustment_get_step_increment(h_adj),
            gtk_adjustment_get_page_increment(h_adj),
            gtk_adjustment_get_page_size(h_adj)
        );

    for (auto &cdata : win_data)
    {
        if (
            cdata.second->group.id ==
            wdata->group.id)
        {
            gtk_scrolled_window_set_hadjustment(
                GTK_SCROLLED_WINDOW(
                    cdata.second->scrolled_window
                ),
                adj
            );
        }
    }
}


static void on_primary_button_released(
    GtkGestureClick *gesture,
    int,
    double,
    double,
    gpointer user_data)
{
    auto *wdata =
        static_cast<window_data *>(user_data);

    if (gtk_drag_source_get_drag(
            wdata->drag_source))
    {
        return;
    }

    select_window(
        wdata->wf_id
    );
}


static void on_middle_button_pressed(
    GtkGestureClick *,
    int,
    double,
    double,
    gpointer user_data)
{
    auto *wdata =
        static_cast<window_data *>(user_data);

    ungroup(
        wdata,
        true
    );
}


/* ============================================================
 * Box helpers
 * ============================================================ */

int get_box_children_count(
    GtkWidget *box)
{
    int count = 0;

    GtkWidget *child =
        gtk_widget_get_first_child(box);

    while (child)
    {
        count++;

        child =
            gtk_widget_get_next_sibling(child);
    }

    return count;
}


static void clear_box(
    GtkWidget *box)
{
    GtkWidget *child;

    while (
        (child =
            gtk_widget_get_first_child(box))
        != nullptr)
    {
        gtk_box_remove(
            GTK_BOX(box),
            child
        );
    }
}


/* ============================================================
 * Ungroup
 * ============================================================ */

static void ungroup(
    window_data *wdata,
    bool notify_server)
{
    auto group_id =
        wdata->group.id;

    clear_group_tabs(
        group_id
    );

    if (group_id)
    {
        for (auto &cdata : win_data)
        {
            if (
                group_id ==
                    cdata.second->group.id &&
                cdata.second->group.parent)
            {
                cdata.second->group.order.erase(
                    std::remove(
                        cdata.second->group.order.begin(),
                        cdata.second->group.order.end(),
                        wdata->wf_id
                    ),
                    cdata.second->group.order.end()
                );

                if (wdata->group.parent)
                {
                    reparent_group(
                        group_id,
                        wdata
                    );
                }

                scroll_sync(
                    cdata.second.get()
                );

                break;
            }
        }
    }

    wdata->group.parent = false;
    wdata->group.order.clear();
    wdata->group.id = 0;

    if (group_id)
    {
        add_tab_button(
            wdata,
            wdata
        );
    }

    refresh_group(
        group_id
    );

    if (notify_server)
    {
        ungroup_window(
            wdata->wf_id
        );
    }
}


/* ============================================================
 * Group
 * ============================================================ */

static void group(
    window_data *drop_target_data,
    uint32_t wf_id)
{
    auto drag_source_data =
        win_data[
            view_to_decor[wf_id]
        ];

    uint32_t group_id = 1;

    if (
        drag_source_data->group.id &&
        drag_source_data->group.id ==
            drop_target_data->group.id)
    {
        g_print(
            "Cannot add tab to the same group.\n"
        );

        return;
    }

    ungroup(
        drag_source_data.get(),
        false
    );

    group_windows(
        drop_target_data->wf_id,
        wf_id
    );

    if (drop_target_data->group.id)
    {
        group_id =
            drop_target_data->group.id;
    }
    else
    {
        for (auto &wdata : win_data)
        {
            if (
                wdata.second->group.id >=
                group_id)
            {
                group_id =
                    wdata.second->group.id + 1;
            }
        }

        drop_target_data->group.parent =
            true;

        drop_target_data->group.id =
            group_id;

        drop_target_data->group.order.push_back(
            drop_target_data->wf_id
        );

        GtkAdjustment *h_adj =
            gtk_scrolled_window_get_hadjustment(
                GTK_SCROLLED_WINDOW(
                    drop_target_data->scrolled_window
                )
            );

        auto adj =
            gtk_adjustment_new(
                gtk_adjustment_get_value(h_adj),
                gtk_adjustment_get_lower(h_adj),
                gtk_adjustment_get_upper(h_adj),
                gtk_adjustment_get_step_increment(h_adj),
                gtk_adjustment_get_page_increment(h_adj),
                gtk_adjustment_get_page_size(h_adj)
            );

        gtk_scrolled_window_set_hadjustment(
            GTK_SCROLLED_WINDOW(
                drop_target_data->scrolled_window
            ),
            adj
        );
    }

    drag_source_data->group.id =
        group_id;

    for (auto &wdata : win_data)
    {
        if (
            wdata.second->group.id ==
                group_id &&
            wdata.second->group.parent)
        {
            wdata.second->group.order.push_back(
                drag_source_data->wf_id
            );

            break;
        }
    }

    clear_group_tabs(
        group_id
    );

    refresh_group(
        group_id
    );

    scroll_sync(
        drop_target_data
    );
}


/* ============================================================
 * Tab button
 * ============================================================ */

static void add_tab_button(
    window_data *wdata,
    window_data *cdata)
{
    GtkWidget *button =
        gtk_button_new();

    gtk_widget_add_css_class(
        button,
        "wf-tab-button"
    );

    gtk_button_set_child(
        GTK_BUTTON(button),
        get_icon(cdata->app_id)
    );

    auto drag_source =
        cdata->drag_source =
            gtk_drag_source_new();

    gtk_drag_source_set_actions(
        drag_source,
        GdkDragAction(
            GDK_ACTION_COPY |
            GDK_ACTION_MOVE
        )
    );

    g_signal_connect(
        drag_source,
        "prepare",
        G_CALLBACK(drag_prepare_cb),
        cdata
    );

    g_signal_connect(
        drag_source,
        "drag-begin",
        G_CALLBACK(drag_begin_cb),
        cdata
    );

    g_signal_connect(
        drag_source,
        "drag-end",
        G_CALLBACK(drag_end_cb),
        nullptr
    );

    gtk_widget_add_controller(
        button,
        GTK_EVENT_CONTROLLER(drag_source)
    );

    GtkGesture *click_gesture =
        gtk_gesture_click_new();

    gtk_gesture_single_set_button(
        GTK_GESTURE_SINGLE(click_gesture),
        1
    );

    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(click_gesture),
        GTK_PHASE_CAPTURE
    );

    g_signal_connect(
        click_gesture,
        "released",
        G_CALLBACK(
            on_primary_button_released
        ),
        cdata
    );

    gtk_widget_add_controller(
        button,
        GTK_EVENT_CONTROLLER(click_gesture)
    );

    click_gesture =
        gtk_gesture_click_new();

    gtk_gesture_single_set_button(
        GTK_GESTURE_SINGLE(click_gesture),
        2
    );

    g_signal_connect(
        click_gesture,
        "pressed",
        G_CALLBACK(
            on_middle_button_pressed
        ),
        cdata
    );

    gtk_widget_add_controller(
        button,
        GTK_EVENT_CONTROLLER(click_gesture)
    );

    gtk_widget_set_tooltip_text(
        button,
        cdata->title.c_str()
    );

    gtk_box_append(
        GTK_BOX(wdata->tab_box),
        button
    );
}


/* ============================================================
 * Clear group tabs
 * ============================================================ */

static void clear_group_tabs(
    uint32_t group_id)
{
    std::vector<uint32_t> button_order;

    if (!group_id)
    {
        return;
    }

    for (auto &wdata : win_data)
    {
        if (
            wdata.second->group.id ==
                group_id &&
            wdata.second->group.parent)
        {
            button_order =
                wdata.second->group.order;

            break;
        }
    }

    for (auto id : button_order)
    {
        auto view_it =
            view_to_decor.find(id);

        if (view_it ==
            view_to_decor.end())
        {
            continue;
        }

        auto win_it =
            win_data.find(
                view_it->second
            );

        if (win_it ==
            win_data.end())
        {
            continue;
        }

        auto wdata =
            win_it->second;

        if (
            wdata->group.id ==
            group_id)
        {
            clear_box(
                wdata->tab_box
            );
        }
    }
}


/* ============================================================
 * Reparent group
 * ============================================================ */

static void reparent_group(
    uint32_t group_id,
    window_data *last_parent)
{
    if (!group_id)
    {
        return;
    }

    for (auto &wdata : win_data)
    {
        if (
            wdata.second->group.id ==
                group_id &&
            last_parent !=
                wdata.second.get())
        {
            wdata.second->group.order =
                last_parent->group.order;

            wdata.second->group.parent =
                true;

            last_parent->group.parent =
                false;

            last_parent->group.order.clear();
            last_parent->group.id = 0;

            scroll_sync(
                wdata.second.get()
            );

            break;
        }
    }
}


/* ============================================================
 * Refresh group
 * ============================================================ */

static void refresh_group(
    uint32_t group_id)
{
    std::vector<uint32_t> button_order;

    if (!group_id)
    {
        return;
    }

    for (auto &wdata : win_data)
    {
        if (
            wdata.second->group.id ==
                group_id &&
            wdata.second->group.parent)
        {
            button_order =
                wdata.second->group.order;

            break;
        }
    }

    for (auto &wdata : win_data)
    {
        for (auto id : button_order)
        {
            auto view_it =
                view_to_decor.find(id);

            if (
                view_it ==
                view_to_decor.end())
            {
                continue;
            }

            auto win_it =
                win_data.find(
                    view_it->second
                );

            if (
                win_it ==
                win_data.end())
            {
                continue;
            }

            auto cdata =
                win_it->second;

            if (
                cdata->group.id ==
                    group_id &&
                wdata.second->group.id ==
                    group_id)
            {
                add_tab_button(
                    wdata.second.get(),
                    cdata.get()
                );
            }
        }
    }

    for (auto &cdata : win_data)
    {
        if (
            get_box_children_count(
                cdata.second->tab_box
            ) == 1)
        {
            clear_box(
                cdata.second->tab_box
            );

            cdata.second->group.id = 0;
            cdata.second->group.parent = false;
            cdata.second->group.order.clear();

            add_tab_button(
                cdata.second.get(),
                cdata.second.get()
            );
        }
    }
}


/* ============================================================
 * Drop target
 * ============================================================ */

static gboolean drop_cb(
    GtkDropTarget *,
    const GValue *value,
    double,
    double,
    gpointer user_data)
{
    auto *drop_target_data =
        static_cast<window_data *>(user_data);

    if (!G_VALUE_HOLDS(
            value,
            G_TYPE_INT))
    {
        g_print(
            "Drop data does not contain int value. Fail.\n"
        );

        return false;
    }

    const uint32_t wf_id =
        static_cast<uint32_t>(
            g_value_get_int(value)
        );

    if (
        drop_target_data->wf_id ==
        wf_id)
    {
        g_print(
            "Dropped on self, ignoring\n"
        );

        return false;
    }

    group(
        drop_target_data,
        wf_id
    );

    return true;
}


/* ============================================================
 * Scroll callback
 * ============================================================ */

static gboolean on_scroll_cb(
    GtkEventControllerScroll *,
    gdouble,
    gdouble dy,
    gpointer user_data)
{
    auto *wdata =
        static_cast<window_data *>(user_data);

    GtkAdjustment *h_adj =
        gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(
                wdata->scrolled_window
            )
        );

    gdouble current_value =
        gtk_adjustment_get_value(h_adj);

    gdouble new_value =
        current_value +
        (dy * 10.0);

    gdouble lower =
        gtk_adjustment_get_lower(h_adj);

    gdouble upper =
        gtk_adjustment_get_upper(h_adj);

    new_value =
        CLAMP(
            new_value,
            lower,
            upper
        );

    gtk_adjustment_set_value(
        h_adj,
        new_value
    );

    scroll_sync(
        wdata
    );

    return false;
}


/* ============================================================
 * Area resize / Wayfire borders
 * ============================================================ */

static void on_area_resized(
    GtkDrawingArea *,
    int,
    int,
    gpointer data)
{
    auto *cdata =
        static_cast<custom_data *>(data);

    if (!cdata ||
        !cdata->area)
    {
        return;
    }

    GtkNative *native =
        gtk_widget_get_native(
            cdata->area
        );

    if (!native)
    {
        return;
    }

    double surface_x = 0.0;
    double surface_y = 0.0;

    gtk_native_get_surface_transform(
        native,
        &surface_x,
        &surface_y
    );

    graphene_rect_t bounds;

    if (!gtk_widget_compute_bounds(
            cdata->area,
            GTK_WIDGET(native),
            &bounds))
    {
        return;
    }

    const double final_x =
        surface_x +
        bounds.origin.x;

    const double final_y =
        surface_y +
        bounds.origin.y;

    if (final_y >= 0.0)
    {
        update_borders(
            cdata->id,
            final_y,
            0,
            final_x,
            0,
            0
        );
    }
}


/* ============================================================
 * Create decorator window
 * ============================================================ */

GtkWidget *create_deco_window(
    uint32_t wf_id)
{
    GtkWidget *window =
        gtk_application_window_new(app);

    gtk_widget_add_css_class(
        window,
        "wf-decorator-window"
    );

    gtk_window_set_default_size(
        GTK_WINDOW(window),
        250,
        250
    );

    gtk_widget_set_size_request(
        window,
        300,
        50
    );


    /* --------------------------------------------------------
     * Content
     * -------------------------------------------------------- */

    GtkWidget *area =
        gtk_drawing_area_new();

    gtk_widget_add_css_class(
        area,
        "wf-content"
    );

    gtk_window_set_child(
        GTK_WINDOW(window),
        area
    );

    gtk_window_set_title(
        GTK_WINDOW(window),
        (
            "__wf_decorator:" +
            std::to_string(wf_id)
        ).c_str()
    );


    /* --------------------------------------------------------
     * Custom data
     * -------------------------------------------------------- */

    auto *cdata =
        static_cast<custom_data *>(
            g_malloc(sizeof(custom_data))
        );

    cdata->id =
        wf_id;

    cdata->area =
        area;


    /* --------------------------------------------------------
     * Window data
     * -------------------------------------------------------- */

    auto wdata =
        std::make_shared<window_data>();


    /* --------------------------------------------------------
     * Header bar
     * -------------------------------------------------------- */

    GtkWidget *header =
        gtk_header_bar_new();

    gtk_header_bar_set_show_title_buttons(
        GTK_HEADER_BAR(header),
        FALSE
    );

    gtk_header_bar_set_decoration_layout(
        GTK_HEADER_BAR(header),
        ""
    );


    /* --------------------------------------------------------
     * Left macOS controls
     * -------------------------------------------------------- */

    GtkWidget *controls =
        create_control_box(
            window,
            wdata.get()
        );

    gtk_header_bar_pack_start(
        GTK_HEADER_BAR(header),
        controls
    );

    window_control_widgets[window] =
        controls;


    /* --------------------------------------------------------
     * Center title
     * -------------------------------------------------------- */

    GtkWidget *title =
        gtk_label_new("");

    gtk_widget_add_css_class(
        title,
        "wf-title"
    );

    gtk_widget_add_css_class(
        title,
        "title"
    );

    gtk_widget_set_visible(
        title,
        TRUE
    );

    gtk_widget_set_opacity(
        title,
        1.0
    );

    gtk_widget_set_hexpand(
        title,
        TRUE
    );

    gtk_widget_set_vexpand(
        title,
        FALSE
    );

    gtk_widget_set_halign(
        title,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_valign(
        title,
        GTK_ALIGN_CENTER
    );

    gtk_label_set_single_line_mode(
        GTK_LABEL(title),
        TRUE
    );

    gtk_label_set_ellipsize(
        GTK_LABEL(title),
        PANGO_ELLIPSIZE_END
    );

    gtk_header_bar_set_title_widget(
        GTK_HEADER_BAR(header),
        title
    );

    window_title_widgets[window] =
        title;


    /* --------------------------------------------------------
     * Right application icon
     * -------------------------------------------------------- */

    GtkWidget *icon_area =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            0
        );

    gtk_widget_add_css_class(
        icon_area,
        "wf-icon-area"
    );

    gtk_widget_set_size_request(
        icon_area,
        38,
        38
    );

    gtk_widget_set_halign(
        icon_area,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_valign(
        icon_area,
        GTK_ALIGN_CENTER
    );

    gtk_widget_set_hexpand(
        icon_area,
        FALSE
    );

    gtk_widget_set_vexpand(
        icon_area,
        FALSE
    );

    gtk_header_bar_pack_end(
        GTK_HEADER_BAR(header),
        icon_area
    );

    window_icon_widgets[window] =
        icon_area;


    /* --------------------------------------------------------
     * Install custom headerbar BEFORE present
     * -------------------------------------------------------- */

    gtk_window_set_titlebar(
        GTK_WINDOW(window),
        header
    );


    /* --------------------------------------------------------
     * Tabs
     * -------------------------------------------------------- */

    GtkWidget *scrolled_window =
        gtk_scrolled_window_new();

    gtk_widget_add_css_class(
        scrolled_window,
        "wf-tabs-scroll"
    );

    GtkWidget *tab_box =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            3
        );

    gtk_widget_add_css_class(
        tab_box,
        "wf-tabs-box"
    );

    GtkWidget *title_box =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            0
        );

    gtk_widget_add_css_class(
        title_box,
        "wf-tabs-container"
    );

    gtk_widget_set_hexpand(
        title_box,
        TRUE
    );

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(
            scrolled_window
        ),
        GTK_POLICY_EXTERNAL,
        GTK_POLICY_NEVER
    );

    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(
            scrolled_window
        ),
        tab_box
    );

    gtk_scrolled_window_set_min_content_width(
        GTK_SCROLLED_WINDOW(
            scrolled_window
        ),
        115
    );

    gtk_box_prepend(
        GTK_BOX(title_box),
        scrolled_window
    );

    GtkEventController *controller =
        gtk_event_controller_scroll_new(
            GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
        );

    gtk_widget_add_controller(
        tab_box,
        controller
    );


    /* --------------------------------------------------------
     * Save window data
     * -------------------------------------------------------- */

    wdata->scrolled_window =
        scrolled_window;

    wdata->title_box =
        title_box;

    wdata->header_bar =
        header;

    wdata->tab_box =
        tab_box;

    wdata->wf_id =
        wf_id;

    win_data[window] =
        wdata;


    /* --------------------------------------------------------
     * Signals
     * -------------------------------------------------------- */

    g_signal_connect(
        controller,
        "scroll",
        G_CALLBACK(on_scroll_cb),
        wdata.get()
    );

    g_signal_connect(
        area,
        "resize",
        G_CALLBACK(on_area_resized),
        cdata
    );

    g_signal_connect(
        window,
        "close-request",
        G_CALLBACK(on_close_request),
        wdata.get()
    );


    /* --------------------------------------------------------
     * Present
     * -------------------------------------------------------- */

    gtk_window_present(
        GTK_WINDOW(window)
    );

    return window;
}


/* ============================================================
 * Close decorator
 * ============================================================ */

static gboolean close_window(
    GtkWindow *,
    gpointer data)
{
    GtkWidget *win =
        static_cast<GtkWidget *>(data);

    auto it =
        std::find_if(
            view_to_decor.begin(),
            view_to_decor.end(),
            [&win](
                const std::pair<uint32_t,
                                GtkWidget *> &element)
            {
                return element.second == win;
            }
        );

    auto wdata_it =
        win_data.find(win);

    if (wdata_it != win_data.end())
    {
        ungroup(
            wdata_it->second.get(),
            true
        );
    }

    if (it != view_to_decor.end())
    {
        view_to_decor.erase(
            it
        );
    }

    window_title_widgets.erase(
        win
    );

    window_icon_widgets.erase(
        win
    );

    window_control_widgets.erase(
        win
    );

    win_data.erase(
        win
    );

    return false;
}


void destroy_deco_window(
    uint32_t wf_id)
{
    auto it =
        view_to_decor.find(wf_id);

    if (it == view_to_decor.end())
    {
        return;
    }

    GtkWidget *window =
        it->second;

    if (window)
    {
        close_window(
            GTK_WINDOW(window),
            window
        );
    }
}


/* ============================================================
 * App ID
 * ============================================================ */

void set_app_id(
    GtkWidget *window,
    const char *app_id)
{
    auto wdata_it =
        win_data.find(window);

    if (wdata_it ==
        win_data.end())
    {
        return;
    }

    auto wdata =
        wdata_it->second;

    wdata->app_id =
        app_id ? app_id : "";


    /* --------------------------------------------------------
     * Tab
     * -------------------------------------------------------- */

    add_tab_button(
        wdata.get(),
        wdata.get()
    );


    /* --------------------------------------------------------
     * Right-side app icon
     * -------------------------------------------------------- */

    auto icon_area_it =
        window_icon_widgets.find(window);

    if (
        icon_area_it !=
        window_icon_widgets.end())
    {
        GtkWidget *icon_area =
            icon_area_it->second;

        GtkWidget *child =
            gtk_widget_get_first_child(
                icon_area
            );

        while (child)
        {
            GtkWidget *next =
                gtk_widget_get_next_sibling(
                    child
                );

            gtk_box_remove(
                GTK_BOX(icon_area),
                child
            );

            child = next;
        }

        GtkWidget *icon =
            get_icon(
                wdata->app_id
            );

        gtk_widget_add_css_class(
            icon,
            "wf-app-icon"
        );

        gtk_image_set_pixel_size(
            GTK_IMAGE(icon),
            18
        );

        gtk_widget_set_size_request(
            icon,
            18,
            18
        );

        gtk_widget_set_halign(
            icon,
            GTK_ALIGN_CENTER
        );

        gtk_widget_set_valign(
            icon,
            GTK_ALIGN_CENTER
        );

        gtk_box_append(
            GTK_BOX(icon_area),
            icon
        );
    }


    /* --------------------------------------------------------
     * Drop target
     * -------------------------------------------------------- */

    GtkDropTarget *drop_target =
        gtk_drop_target_new(
            G_TYPE_INT,
            GdkDragAction(
                GDK_ACTION_COPY |
                GDK_ACTION_MOVE
            )
        );

    g_signal_connect(
        drop_target,
        "drop",
        G_CALLBACK(drop_cb),
        wdata.get()
    );

    gtk_widget_add_controller(
        wdata->tab_box,
        GTK_EVENT_CONTROLLER(drop_target)
    );
}


/* ============================================================
 * Window title
 * ============================================================ */

void set_title(
    GtkWidget *window,
    const char *title)
{
    auto wdata_it =
        win_data.find(window);

    if (wdata_it ==
        win_data.end())
    {
        return;
    }

    auto wdata =
        wdata_it->second;

    wdata->title =
        title ? title : "";


    /*
     * Keep the actual GtkWindow title synchronized.
     */
    gtk_window_set_title(
        GTK_WINDOW(window),
        wdata->title.c_str()
    );


    /*
     * Update our explicit custom GtkLabel.
     */
    auto title_it =
        window_title_widgets.find(window);

    if (
        title_it !=
        window_title_widgets.end() &&
        title_it->second)
    {
        GtkWidget *title_widget =
            title_it->second;

        gtk_label_set_text(
            GTK_LABEL(title_widget),
            wdata->title.c_str()
        );

        gtk_widget_set_visible(
            title_widget,
            TRUE
        );

        gtk_widget_set_opacity(
            title_widget,
            1.0
        );

        gtk_widget_set_halign(
            title_widget,
            GTK_ALIGN_CENTER
        );

        gtk_widget_set_valign(
            title_widget,
            GTK_ALIGN_CENTER
        );

        gtk_widget_set_hexpand(
            title_widget,
            TRUE
        );

        gtk_widget_set_vexpand(
            title_widget,
            FALSE
        );
    }
}


/* ============================================================
 * Main
 * ============================================================ */

int main(
    int argc,
    char **argv)
{
    app =
        gtk_application_new(
            "org.wf.sample-decorator",
            G_APPLICATION_NON_UNIQUE
        );

    g_signal_connect(
        app,
        "activate",
        G_CALLBACK(activate),
        nullptr
    );

    int status =
        g_application_run(
            G_APPLICATION(app),
            argc,
            argv
        );

    if (color_scheme_changed_handler &&
        desktop_interface_settings)
    {
        g_signal_handler_disconnect(
            desktop_interface_settings,
            color_scheme_changed_handler
        );
    }

    if (desktop_interface_settings)
    {
        g_object_unref(
            desktop_interface_settings
        );

        desktop_interface_settings =
            nullptr;
    }

    if (decorator_css_provider)
    {
        g_object_unref(
            decorator_css_provider
        );

        decorator_css_provider =
            nullptr;
    }

    g_object_unref(app);

    return status;
}

