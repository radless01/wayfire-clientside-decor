#include <algorithm>
#include "protocol.hpp"

GtkApplication *app;

struct custom_data
{
    uint32_t id;
    GtkWidget *area;
};

static void add_tab_button(window_data *wdata, window_data *cdata);
static void clear_group_tabs(uint32_t group_id);
static void reparent_group(uint32_t group_id, window_data *last_parent);
static void refresh_group(uint32_t group_id);
static void ungroup(window_data *wdata, bool notify_server);

static void activate(GtkApplication *app, gpointer)
{
    GdkDisplay *display = gdk_display_get_default();
    setup_protocol(display);

    g_application_hold(G_APPLICATION(app));
}

static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    auto wdata = (window_data*)data;

    close_request(wdata->wf_id);

    return true;
}

static gboolean close_window(GtkWindow *window, gpointer data)
{
    GtkWidget *win = (GtkWidget*)data;
    auto it = std::find_if(view_to_decor.begin(), view_to_decor.end(),
        [&win] (const std::pair<uint32_t, GtkWidget*>& element)
    {
        return element.second == win;
    });

    ungroup(win_data[(GtkWidget*)window].get(), true);

    if (it != view_to_decor.end())
    {
        uint32_t id = it->first;
        view_to_decor.erase(id);
    }

    win_data.erase(win);

    return false;
}

static void on_area_resized(GtkDrawingArea*, int w, int h, gpointer data)
{
    printf("on_area_resized\n");
    auto cdata = (custom_data*)data;
    auto id    = cdata->id;
    auto area  = cdata->area;
    GtkNative *native = gtk_widget_get_native(area);

    double surface_x, surface_y;
    gtk_native_get_surface_transform(native, &surface_x, &surface_y);

    graphene_rect_t bounds;
    gtk_widget_compute_bounds(area, GTK_WIDGET(native), &bounds);

    double final_x = surface_x + bounds.origin.x;
    double final_y = surface_y + bounds.origin.y;
    double width   = bounds.size.width;
    double height  = bounds.size.height;

    if (final_y > 0)
    {
        update_borders(id, final_y, 3, final_x, 0, 2);
    }
}

static GtkWidget *get_icon(std::string app_id)
{
    GtkWidget *image = nullptr;
    auto theme = gtk_icon_theme_get_for_display(gdk_display_get_default());

    auto _app_id = app_id;
    auto dot_pos = _app_id.find_last_of(".");

    auto lower_case_app_id = app_id;
    for (char & c : lower_case_app_id)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (gtk_icon_theme_has_icon(theme, lower_case_app_id.c_str()))
    {
        image = gtk_image_new_from_icon_name(lower_case_app_id.c_str());
    } else if ((dot_pos != std::string::npos) && (dot_pos < _app_id.length() - 1))
    {
        _app_id = _app_id.substr(dot_pos + 1);

        if (gtk_icon_theme_has_icon(theme, _app_id.c_str()))
        {
            image = gtk_image_new_from_icon_name(_app_id.c_str());
        }
    } else if ((dot_pos != std::string::npos) && (dot_pos < lower_case_app_id.length() - 1))
    {
        _app_id = lower_case_app_id.substr(dot_pos + 1);

        if (gtk_icon_theme_has_icon(theme, _app_id.c_str()))
        {
            image = gtk_image_new_from_icon_name(_app_id.c_str());
        }
    }

    if (!image)
    {
        image = gtk_image_new_from_icon_name(app_id.c_str());
    }

    return image;
}

// --- Drag Source Setup ---
static GdkContentProvider *drag_prepare_cb(GtkDragSource *source,
    double x,
    double y,
    gpointer user_data)
{
    g_print("Drag prepare.\n");
    auto data = (window_data*)user_data;
    // Initialize the GValue with a string and set it
    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_TYPE_INT);
    g_value_set_int(&value, data->wf_id);
    auto content_provider = gdk_content_provider_new_for_value(&value);
    gtk_gesture_set_state(GTK_GESTURE(source), GTK_EVENT_SEQUENCE_CLAIMED);
    return content_provider;
}

static void drag_begin_cb(GtkDragSource *source,
    GdkDrag *drag,
    gpointer user_data)
{
    g_print("Drag begin.\n");
    auto data = (window_data*)user_data;
    GtkDragIcon *drag_icon = GTK_DRAG_ICON(gtk_drag_icon_get_for_drag(drag));
    GtkWidget *image = get_icon(data->app_id);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
    gtk_drag_icon_set_child(drag_icon, image);
}

static void drag_end_cb(GtkDragSource *source,
    GdkDrag *drag,
    gboolean delete_data,
    gpointer user_data)
{
    g_print("Drag operation completed.\n");
}

static void scroll_sync(window_data *wdata)
{
    if (!wdata->group.id)
    {
        return;
    }

    auto h_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(wdata->scrolled_window));
    auto adj   = gtk_adjustment_new(gtk_adjustment_get_value(h_adj),
        gtk_adjustment_get_lower(h_adj),
        gtk_adjustment_get_upper(h_adj),
        gtk_adjustment_get_step_increment(h_adj),
        gtk_adjustment_get_page_increment(h_adj),
        gtk_adjustment_get_page_size(h_adj));

    for (auto cdata : win_data)
    {
        if (cdata.second->group.id == wdata->group.id)
        {
            gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(cdata.second->scrolled_window), adj);
        }
    }
}

static void on_primary_button_released(GtkGestureClick *gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data)
{
    auto wdata = (window_data*)user_data;
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    if (gtk_drag_source_get_drag(wdata->drag_source))
    {
        return;
    }

    select_window(wdata->wf_id);
}

static void on_middle_button_pressed(GtkGestureClick *gesture,
    int n_press,
    double x,
    double y,
    gpointer user_data)
{
    auto wdata = (window_data*)user_data;
    ungroup(wdata, true);
}

int get_box_children_count(GtkWidget *box)
{
    int count = 0;
    GtkWidget *child = gtk_widget_get_first_child(box);

    while (child)
    {
        count++;
        child = gtk_widget_get_next_sibling(child);
    }

    return count;
}

static void clear_box(GtkWidget *box)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(box)) != NULL)
    {
        gtk_box_remove(GTK_BOX(box), child);
    }
}

static void ungroup(window_data *wdata, bool notify_server)
{
    auto group_id = wdata->group.id;

    clear_group_tabs(group_id);

    if (group_id)
    {
        for (auto cdata : win_data)
        {
            if ((group_id == cdata.second->group.id) && cdata.second->group.parent)
            {
                cdata.second->group.order.erase(std::remove(cdata.second->group.order.begin(),
                    cdata.second->group.order.end(), wdata->wf_id), cdata.second->group.order.end());

                if (wdata->group.parent)
                {
                    reparent_group(group_id, wdata);
                }

                scroll_sync(cdata.second.get());
                break;
            }
        }
    }

    wdata->group.parent = false;
    wdata->group.order.clear();
    wdata->group.id = 0;

    if (group_id)
    {
        add_tab_button(wdata, wdata);
    }

    refresh_group(group_id);
    if (notify_server)
    {
        ungroup_window(wdata->wf_id);
    }
}

static void group(window_data *drop_target_data, uint32_t wf_id)
{
    auto drag_source_data = win_data[view_to_decor[wf_id]];
    uint32_t group_id     = 1;

    if (drag_source_data->group.id && (drag_source_data->group.id == drop_target_data->group.id))
    {
        g_print("Cannot add tab to the same group.\n");
        return;
    }

    ungroup(drag_source_data.get(), false);
    group_windows(drop_target_data->wf_id, wf_id);

    if (drop_target_data->group.id)
    {
        group_id = drop_target_data->group.id;
    } else
    {
        for (auto wdata : win_data)
        {
            if (wdata.second->group.id >= group_id)
            {
                group_id = wdata.second->group.id + 1;
            }
        }

        drop_target_data->group.parent = true;
        drop_target_data->group.id     = group_id;
        drop_target_data->group.order.push_back(drop_target_data->wf_id);
        GtkAdjustment *h_adj =
            gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(drop_target_data->scrolled_window));
        auto adj = gtk_adjustment_new(gtk_adjustment_get_value(h_adj),
            gtk_adjustment_get_lower(h_adj),
            gtk_adjustment_get_upper(h_adj),
            gtk_adjustment_get_step_increment(h_adj),
            gtk_adjustment_get_page_increment(h_adj),
            gtk_adjustment_get_page_size(h_adj));
        gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(drop_target_data->scrolled_window), adj);
    }

    drag_source_data->group.id = group_id;

    for (auto wdata : win_data)
    {
        if ((wdata.second->group.id == group_id) && wdata.second->group.parent)
        {
            wdata.second->group.order.push_back(drag_source_data->wf_id);
            break;
        }
    }

    clear_group_tabs(group_id);
    refresh_group(group_id);
    scroll_sync(drop_target_data);
}

static void add_tab_button(window_data *wdata, window_data *cdata)
{
    GtkWidget *button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), get_icon(cdata->app_id));

    auto drag_source = cdata->drag_source = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag_source, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(drag_source, "prepare", G_CALLBACK(drag_prepare_cb), cdata);
    g_signal_connect(drag_source, "drag-begin", G_CALLBACK(drag_begin_cb), cdata);
    g_signal_connect(drag_source, "drag-end", G_CALLBACK(drag_end_cb), NULL);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(drag_source));

    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 1);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click_gesture), GTK_PHASE_CAPTURE);
    g_signal_connect(click_gesture, "released", G_CALLBACK(on_primary_button_released), cdata);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(click_gesture));

    click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), 2);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_middle_button_pressed), cdata);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(click_gesture));

    gtk_widget_set_tooltip_text(button, cdata->title.c_str());

    gtk_box_append(GTK_BOX(wdata->tab_box), button);
}

static void clear_group_tabs(uint32_t group_id)
{
    std::vector<uint32_t> button_order;

    if (!group_id)
    {
        return;
    }

    for (auto wdata : win_data)
    {
        if ((wdata.second->group.id == group_id) && wdata.second->group.parent)
        {
            button_order = wdata.second->group.order;
            break;
        }
    }

    for (auto id : button_order)
    {
        auto wdata = win_data[view_to_decor[id]];
        if (wdata->group.id == group_id)
        {
            clear_box(wdata->tab_box);
        }
    }
}

static void reparent_group(uint32_t group_id, window_data *last_parent)
{
    if (!group_id)
    {
        return;
    }

    for (auto wdata : win_data)
    {
        if ((wdata.second->group.id == group_id) && (last_parent != wdata.second.get()))
        {
            wdata.second->group.order  = last_parent->group.order;
            wdata.second->group.parent = true;
            last_parent->group.parent  = false;
            last_parent->group.order.clear();
            last_parent->group.id = 0;
            scroll_sync(wdata.second.get());
            break;
        }
    }
}

static void refresh_group(uint32_t group_id)
{
    std::vector<uint32_t> button_order;

    if (!group_id)
    {
        return;
    }

    for (auto wdata : win_data)
    {
        if ((wdata.second->group.id == group_id) && wdata.second->group.parent)
        {
            button_order = wdata.second->group.order;
            break;
        }
    }

    for (auto wdata : win_data)
    {
        for (auto id : button_order)
        {
            auto cdata = win_data[view_to_decor[id]];
            if ((cdata->group.id == group_id) && (wdata.second->group.id == group_id))
            {
                add_tab_button(wdata.second.get(), cdata.get());
            }
        }
    }

    for (auto cdata : win_data)
    {
        if (get_box_children_count(cdata.second->tab_box) == 1)
        {
            clear_box(cdata.second->tab_box);
            cdata.second->group.id     = 0;
            cdata.second->group.parent = false;
            cdata.second->group.order.clear();
            add_tab_button(cdata.second.get(), cdata.second.get());
        }
    }
}

// --- Drop Target Setup ---
static gboolean drop_cb(GtkDropTarget *target,
    const GValue *value,
    double x,
    double y,
    gpointer user_data)
{
    g_print("Drop.\n");
    auto drop_target_data = (window_data*)user_data;

    if (G_VALUE_HOLDS(value, G_TYPE_INT))
    {
        auto wf_id = g_value_get_int(value);
        printf("%s: wf_id: %d\n", __func__, wf_id);

        if (drop_target_data->wf_id == wf_id)
        {
            g_print("Dropped on self, ignoring\n");
            return false;
        }

        g_print("Dropped on target, success!\n");

        group(drop_target_data, wf_id);

        return true;
    }

    g_print("Drop data does not contain int value. Fail.\n");
    return false;
}

static gboolean on_scroll_cb(GtkEventControllerScroll *controller,
    gdouble dx,
    gdouble dy,
    gpointer user_data)
{
    auto wdata = (window_data*)user_data;

    GtkAdjustment *h_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(wdata->scrolled_window));

    gdouble current_value = gtk_adjustment_get_value(h_adj);
    gdouble new_value     = current_value + (dy * 10.0);

    gdouble lower = gtk_adjustment_get_lower(h_adj);
    gdouble upper = gtk_adjustment_get_upper(h_adj);
    new_value = CLAMP(new_value, lower, upper);
    gtk_adjustment_set_value(h_adj, new_value);
    scroll_sync(wdata);

    return false;
}

GtkWidget *create_deco_window(uint32_t wf_id)
{
    auto window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 250, 250);
    gtk_widget_set_size_request(window, 300, 50);
    auto area = gtk_drawing_area_new();
    gtk_window_set_child(GTK_WINDOW(window), area);
    gtk_window_set_title(GTK_WINDOW(window), ("__wf_decorator:" + std::to_string(wf_id)).c_str());
    auto cdata = (custom_data*)malloc(sizeof(custom_data));
    cdata->id   = wf_id;
    cdata->area = area;

    auto wdata = std::make_shared<window_data>();

    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    GtkWidget *tab_box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(title_box, true);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_EXTERNAL,
        GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), tab_box);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled_window), 115);
    gtk_box_prepend(GTK_BOX(title_box), scrolled_window);

    GtkEventController *controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(tab_box, controller);

    wdata->scrolled_window = scrolled_window;
    wdata->title_box  = title_box;
    wdata->header_bar = header;
    wdata->tab_box    = tab_box;
    wdata->wf_id     = wf_id;
    win_data[window] = wdata;

    g_signal_connect(controller, "scroll", G_CALLBACK(on_scroll_cb), wdata.get());

    g_signal_connect(area, "resize", G_CALLBACK(on_area_resized), cdata);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), wdata.get());

    gtk_window_present(GTK_WINDOW(window));

    return window;
}

void destroy_deco_window(uint32_t wf_id)
{
    auto window = view_to_decor[wf_id];
    if (window)
    {
        close_window(GTK_WINDOW(window), win_data[window].get());
    }
}

void set_app_id(GtkWidget *window, const char *app_id)
{
    auto wdata = win_data[window];
    wdata->app_id = app_id;

    add_tab_button(wdata.get(), wdata.get());

    GtkDropTarget *drop_target =
        gtk_drop_target_new(G_TYPE_INT, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE));

    g_signal_connect(drop_target, "drop", G_CALLBACK(drop_cb), wdata.get());
    gtk_widget_add_controller(wdata->tab_box, GTK_EVENT_CONTROLLER(drop_target));

    gtk_header_bar_pack_start(GTK_HEADER_BAR(wdata->header_bar), wdata->title_box);
}


void set_title(GtkWidget *window, const char *title)
{
    gtk_window_set_title(GTK_WINDOW(window), title);

    auto wdata = win_data[window];
    wdata->title = title;
}



int main(int argc, char **argv)
{
    int status;

    app = gtk_application_new("org.wf.sample-decorator", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
