/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file gtkStatsStripChart.cxx
 * @author drose
 * @date 2006-01-16
 */

#include "gtkStatsStripChart.h"
#include "gtkStatsMonitor.h"
#include "pStatCollectorDef.h"
#include "numeric_types.h"

static const int default_strip_chart_width = 400;
static const int default_strip_chart_height = 100;

/**
 *
 */
GtkStatsStripChart::
GtkStatsStripChart(GtkStatsMonitor *monitor, int thread_index,
                   int collector_index, bool show_level) :
  PStatStripChart(monitor,
                  show_level ? monitor->get_level_view(collector_index, thread_index) : monitor->get_view(thread_index),
                  thread_index,
                  collector_index,
                  default_strip_chart_width,
                  default_strip_chart_height),
  GtkStatsGraph(monitor)
{
  if (show_level) {
    // If it's a level-type graph, show the appropriate units.
    if (_unit_name.empty()) {
      set_guide_bar_units(GBU_named);
    } else {
      set_guide_bar_units(GBU_named | GBU_show_units);
    }

  } else {
    // If it's a time-type graph, show the msHz units.
    set_guide_bar_units(get_guide_bar_units() | GBU_show_units);
  }

  // Put some stuff on top of the graph.
  _top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(_graph_vbox), _top_hbox,
         FALSE, FALSE, 0);

  _smooth_check_box = gtk_check_button_new_with_label("Smooth");
  g_signal_connect(G_OBJECT(_smooth_check_box), "toggled",
       G_CALLBACK(toggled_callback), this);

  _total_label = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(_top_hbox), _smooth_check_box,
         FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(_top_hbox), _total_label,
       FALSE, FALSE, 0);

  // Add a DrawingArea widget to the right of the graph, to display all of the
  // scale units.
  _scale_area = gtk_drawing_area_new();
  g_signal_connect(G_OBJECT(_scale_area), "draw",
       G_CALLBACK(draw_callback), this);
  gtk_box_pack_start(GTK_BOX(_graph_hbox), _scale_area,
         FALSE, FALSE, 0);

  // Make it wide enough to display a typical label.
  {
    PangoLayout *layout = gtk_widget_create_pango_layout(_window, "99 ms");
    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);
    gtk_widget_set_size_request(_scale_area, width, 0);
    g_object_unref(layout);
  }

  gtk_widget_set_size_request(_graph_window, default_strip_chart_width,
            default_strip_chart_height);

  gtk_widget_show_all(_window);
  gtk_widget_show(_window);

  // Allow the window to be resized as small as the user likes.  We have to do
  // this after the window has been shown; otherwise, it will affect the
  // window's initial size.
  gtk_widget_set_size_request(_window, 0, 0);

  clear_region();
}

/**
 *
 */
GtkStatsStripChart::
~GtkStatsStripChart() {
}

/**
 * Called whenever a new Collector definition is received from the client.
 */
void GtkStatsStripChart::
new_collector(int collector_index) {
  GtkStatsGraph::new_collector(collector_index);
}

/**
 * Called as each frame's data is made available.  There is no gurantee the
 * frames will arrive in order, or that all of them will arrive at all.  The
 * monitor should be prepared to accept frames received out-of-order or
 * missing.
 */
void GtkStatsStripChart::
new_data(int thread_index, int frame_number) {
  if (is_title_unknown()) {
    std::string window_title = get_title_text();
    if (!is_title_unknown()) {
      gtk_window_set_title(GTK_WINDOW(_window), window_title.c_str());
    }
  }

  if (!_pause) {
    update();

    std::string text = format_number(get_average_net_value(), get_guide_bar_units(), get_guide_bar_unit_name());
    if (_net_value_text != text) {
      _net_value_text = text;
      gtk_label_set_text(GTK_LABEL(_total_label), _net_value_text.c_str());
    }
  }
}

/**
 * Called when it is necessary to redraw the entire graph.
 */
void GtkStatsStripChart::
force_redraw() {
  PStatStripChart::force_redraw();
}

/**
 * Called when the user has resized the window, forcing a resize of the graph.
 */
void GtkStatsStripChart::
changed_graph_size(int graph_xsize, int graph_ysize) {
  PStatStripChart::changed_size(graph_xsize, graph_ysize);
}

/**
 * Called when the user selects a new time units from the monitor pulldown
 * menu, this should adjust the units for the graph to the indicated mask if
 * it is a time-based graph.
 */
void GtkStatsStripChart::
set_time_units(int unit_mask) {
  int old_unit_mask = get_guide_bar_units();
  if ((old_unit_mask & (GBU_hz | GBU_ms)) != 0) {
    unit_mask = unit_mask & (GBU_hz | GBU_ms);
    unit_mask |= (old_unit_mask & GBU_show_units);
    set_guide_bar_units(unit_mask);

    gtk_widget_queue_draw(_scale_area);
  }
}

/**
 * Called when the user selects a new scroll speed from the monitor pulldown
 * menu, this should adjust the speed for the graph to the indicated value.
 */
void GtkStatsStripChart::
set_scroll_speed(double scroll_speed) {
  // The speed factor indicates chart widths per minute.
  if (scroll_speed != 0.0f) {
    set_horizontal_scale(60.0f / scroll_speed);
  }
}

/**
 * Called when the user single-clicks on a label.
 */
void GtkStatsStripChart::
on_click_label(int collector_index) {
  if (collector_index < 0) {
    // Clicking on whitespace in the graph is the same as clicking on the top
    // label.
    collector_index = get_collector_index();
  }

  if (collector_index == get_collector_index() && collector_index != 0) {
    // Clicking on the top label means to go up to the parent level.
    const PStatClientData *client_data =
      GtkStatsGraph::_monitor->get_client_data();
    if (client_data->has_collector(collector_index)) {
      const PStatCollectorDef &def =
        client_data->get_collector_def(collector_index);
      if (def._parent_index == 0 && get_view().get_show_level()) {
        // Unless the parent is "Frame", and we're not a time collector.
      } else {
        set_collector_index(def._parent_index);
      }
    }

  } else {
    // Clicking on any other label means to focus on that.
    set_collector_index(collector_index);
  }
}

/**
 * Called when the mouse hovers over a label, and should return the text that
 * should appear on the tooltip.
 */
std::string GtkStatsStripChart::
get_label_tooltip(int collector_index) const {
  return PStatStripChart::get_label_tooltip(collector_index);
}

/**
 * Changes the value the height of the vertical axis represents.  This may
 * force a redraw.
 */
void GtkStatsStripChart::
set_vertical_scale(double value_height) {
  PStatStripChart::set_vertical_scale(value_height);

  gtk_widget_queue_draw(_graph_window);
  gtk_widget_queue_draw(_scale_area);
}

/**
 * Resets the list of labels.
 */
void GtkStatsStripChart::
update_labels() {
  PStatStripChart::update_labels();

  _label_stack.clear_labels();
  for (int i = 0; i < get_num_labels(); i++) {
    _label_stack.add_label(GtkStatsGraph::_monitor, this, _thread_index,
                           get_label_collector(i), false);
  }
  _labels_changed = false;
}

/**
 * Erases the chart area.
 */
void GtkStatsStripChart::
clear_region() {
  cairo_set_source_rgb(_cr, 1.0, 1.0, 1.0);
  cairo_paint(_cr);
}

/**
 * Should be overridden by the user class to copy a region of the chart from
 * one part of the chart to another.  This is used to implement scrolling.
 */
void GtkStatsStripChart::
copy_region(int start_x, int end_x, int dest_x) {
  // We are not allowed to copy a surface onto itself, so we have to create a
  // temporary surface to copy to.
  end_x = std::min(end_x, get_xsize());
  cairo_surface_t *temp_surface =
    cairo_image_surface_create(CAIRO_FORMAT_RGB24, end_x - start_x, get_ysize());
  {
    cairo_t *temp_cr = cairo_create(temp_surface);
    cairo_set_source_surface(temp_cr, _cr_surface, -start_x, 0);
    cairo_paint(temp_cr);
    cairo_destroy(temp_cr);
  }

  cairo_set_source_surface(_cr, temp_surface, 0, 0);
  cairo_rectangle(_cr, dest_x, 0, end_x - start_x, get_ysize());
  cairo_fill(_cr);

  cairo_surface_destroy(temp_surface);

  GdkWindow *window = gtk_widget_get_window(_graph_window);
  GdkRectangle rect = {
    dest_x, 0, end_x - start_x, get_ysize()
  };
  gdk_window_invalidate_rect(window, &rect, FALSE);
}

/**
 * Draws a single vertical slice of the strip chart, at the given pixel
 * position, and corresponding to the indicated level data.
 */
void GtkStatsStripChart::
draw_slice(int x, int w, const PStatStripChart::FrameData &fdata) {
  // Start by clearing the band first.
  cairo_set_source_rgb(_cr, 1.0, 1.0, 1.0);
  cairo_rectangle(_cr, x, 0, w, get_ysize());
  cairo_fill(_cr);

  double overall_time = 0.0;
  int y = get_ysize();

  FrameData::const_iterator fi;
  for (fi = fdata.begin(); fi != fdata.end(); ++fi) {
    const ColorData &cd = (*fi);
    overall_time += cd._net_value;
    cairo_set_source(_cr, get_collector_pattern(cd._collector_index,
      _highlighted_index == cd._collector_index));

    if (overall_time > get_vertical_scale()) {
      // Off the top.  Go ahead and clamp it by hand, in case it's so far off
      // the top we'd overflow the 16-bit pixel value.
      cairo_rectangle(_cr, x, 0, w, y);
      cairo_fill(_cr);
      // And we can consider ourselves done now.
      return;
    }

    int top_y = height_to_pixel(overall_time);
    cairo_rectangle(_cr, x, top_y, w, y - top_y);
    cairo_fill(_cr);
    y = top_y;
  }
}

/**
 * Draws a single vertical slice of background color.
 */
void GtkStatsStripChart::
draw_empty(int x, int w) {
  cairo_set_source_rgb(_cr, 1.0, 1.0, 1.0);
  cairo_rectangle(_cr, x, 0, w, get_ysize());
}

/**
 * Draws a single vertical slice of foreground color.
 */
void GtkStatsStripChart::
draw_cursor(int x) {
  cairo_set_source_rgb(_cr, 0.0, 0.0, 0.0);
  cairo_move_to(_cr, x, 0);
  cairo_line_to(_cr, x, get_ysize());
  cairo_stroke(_cr);
}

/**
 * Should be overridden by the user class.  This hook will be called after
 * drawing a series of color bars in the strip chart; it gives the pixel range
 * that was just redrawn.
 */
void GtkStatsStripChart::
end_draw(int from_x, int to_x) {
  // Draw in the guide bars.
  int num_guide_bars = get_num_guide_bars();
  for (int i = 0; i < num_guide_bars; i++) {
    draw_guide_bar(_cr, from_x, to_x, get_guide_bar(i));
  }

  GdkWindow *window = gtk_widget_get_window(_graph_window);
  GdkRectangle rect = {
    from_x, 0, to_x - from_x, get_ysize()
  };
  gdk_window_invalidate_rect(window, &rect, FALSE);
}

/**
 * This is called during the servicing of the draw event; it gives a derived
 * class opportunity to do some further painting into the graph window.
 */
void GtkStatsStripChart::
additional_graph_window_paint(cairo_t *cr) {
  int num_user_guide_bars = get_num_user_guide_bars();
  for (int i = 0; i < num_user_guide_bars; i++) {
    draw_guide_bar(cr, 0, get_xsize(), get_user_guide_bar(i));
  }
}

/**
 * Based on the mouse position within the graph window, look for draggable
 * things the mouse might be hovering over and return the appropriate DragMode
 * enum or DM_none if nothing is indicated.
 */
GtkStatsGraph::DragMode GtkStatsStripChart::
consider_drag_start(int graph_x, int graph_y) {
  if (graph_x >= 0 && graph_x < get_xsize()) {
    if (graph_y >= 0 && graph_y < get_ysize()) {
      // See if the mouse is over a user-defined guide bar.
      int y = graph_y;
      double from_height = pixel_to_height(y + 2);
      double to_height = pixel_to_height(y - 2);
      _drag_guide_bar = find_user_guide_bar(from_height, to_height);
      if (_drag_guide_bar >= 0) {
        return DM_guide_bar;
      }

    } else {
      // The mouse is above or below the graph; maybe create a new guide bar.
      return DM_new_guide_bar;
    }
  }

  return GtkStatsGraph::consider_drag_start(graph_x, graph_y);
}

/**
 * This should be called whenever the drag mode needs to change state.  It
 * provides hooks for a derived class to do something special.
 */
void GtkStatsStripChart::
set_drag_mode(GtkStatsGraph::DragMode drag_mode) {
  GtkStatsGraph::set_drag_mode(drag_mode);

  switch (_drag_mode) {
  case DM_scale:
  case DM_sizing:
    // Disable smoothing for these expensive operations.
    set_average_mode(false);
    break;

  default:
    // Restore smoothing according to the current setting of the check box.
    bool active =
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_smooth_check_box));
    set_average_mode(active);
    break;
  }
}

/**
 * Called when the mouse button is depressed within the graph window.
 */
gboolean GtkStatsStripChart::
handle_button_press(GtkWidget *widget, int graph_x, int graph_y,
        bool double_click) {
  if (graph_x >= 0 && graph_y >= 0 && graph_x < get_xsize() && graph_y < get_ysize()) {
    if (double_click) {
      // Double-clicking on a color bar in the graph is the same as double-
      // clicking on the corresponding label.
      on_click_label(get_collector_under_pixel(graph_x, graph_y));
      return TRUE;
    }

    if (_potential_drag_mode == DM_none) {
      set_drag_mode(DM_scale);
      _drag_scale_start = pixel_to_height(graph_y);
      // SetCapture(_graph_window);
      return TRUE;
    }
  }

  if (_potential_drag_mode == DM_guide_bar && _drag_guide_bar >= 0) {
    set_drag_mode(DM_guide_bar);
    _drag_start_y = graph_y;
    // SetCapture(_graph_window);
    return TRUE;
  }

  return GtkStatsGraph::handle_button_press(widget, graph_x, graph_y,
              double_click);
}

/**
 * Called when the mouse button is released within the graph window.
 */
gboolean GtkStatsStripChart::
handle_button_release(GtkWidget *widget, int graph_x, int graph_y) {
  if (_drag_mode == DM_scale) {
    set_drag_mode(DM_none);
    // ReleaseCapture();
    return handle_motion(widget, graph_x, graph_y);

  } else if (_drag_mode == DM_guide_bar) {
    if (graph_y < 0 || graph_y >= get_ysize()) {
      remove_user_guide_bar(_drag_guide_bar);
    } else {
      move_user_guide_bar(_drag_guide_bar, pixel_to_height(graph_y));
    }
    set_drag_mode(DM_none);
    // ReleaseCapture();
    return handle_motion(widget, graph_x, graph_y);
  }

  return GtkStatsGraph::handle_button_release(widget, graph_x, graph_y);
}

/**
 * Called when the mouse is moved within the graph window.
 */
gboolean GtkStatsStripChart::
handle_motion(GtkWidget *widget, int graph_x, int graph_y) {
  if (_drag_mode == DM_none && _potential_drag_mode == DM_none &&
      graph_x >= 0 && graph_y >= 0 && graph_x < get_xsize() && graph_y < get_ysize()) {
    // When the mouse is over a color bar, highlight it.
    int collector_index = get_collector_under_pixel(graph_x, graph_y);
    _label_stack.highlight_label(collector_index);
    on_enter_label(collector_index);
  }
  else {
    // If the mouse is in some drag mode, stop highlighting.
    _label_stack.highlight_label(-1);
    on_leave_label(_highlighted_index);
  }

  if (_drag_mode == DM_scale) {
    double ratio = 1.0f - ((double)graph_y / (double)get_ysize());
    if (ratio > 0.0f) {
      set_vertical_scale(_drag_scale_start / ratio);
    }
    return TRUE;

  } else if (_drag_mode == DM_new_guide_bar) {
    // We haven't created the new guide bar yet; we won't until the mouse
    // comes within the graph's region.
    if (graph_y >= 0 && graph_y < get_ysize()) {
      set_drag_mode(DM_guide_bar);
      _drag_guide_bar = add_user_guide_bar(pixel_to_height(graph_y));
      return TRUE;
    }

  } else if (_drag_mode == DM_guide_bar) {
    move_user_guide_bar(_drag_guide_bar, pixel_to_height(graph_y));
    return TRUE;
  }

  return GtkStatsGraph::handle_motion(widget, graph_x, graph_y);
}

/**
 * Draws the line for the indicated guide bar on the graph.
 */
void GtkStatsStripChart::
draw_guide_bar(cairo_t *cr, int from_x, int to_x,
               const PStatGraph::GuideBar &bar) {
  int y = height_to_pixel(bar._height);

  if (y > 0) {
    // Only draw it if it's not too close to the top.
    switch (bar._style) {
    case GBS_target:
      cairo_set_source_rgb(cr, rgb_light_gray[0], rgb_light_gray[1], rgb_light_gray[2]);
      break;

    case GBS_user:
      cairo_set_source_rgb(cr, rgb_user_guide_bar[0], rgb_user_guide_bar[1], rgb_user_guide_bar[2]);
      break;

    case GBS_normal:
      cairo_set_source_rgb(cr, rgb_dark_gray[0], rgb_dark_gray[1], rgb_dark_gray[2]);
      break;
    }
    cairo_move_to(cr, from_x, y);
    cairo_line_to(cr, to_x, y);
    cairo_stroke(cr);
  }
}

/**
 * This is called during the servicing of the draw event.
 */
void GtkStatsStripChart::
draw_guide_labels(cairo_t *cr) {
  // Draw in the labels for the guide bars.
  int last_y = -100;

  int i;
  int num_guide_bars = get_num_guide_bars();
  for (i = 0; i < num_guide_bars; i++) {
    last_y = draw_guide_label(cr, get_guide_bar(i), last_y);
  }

  GuideBar top_value = make_guide_bar(get_vertical_scale());
  draw_guide_label(cr, top_value, last_y);

  last_y = -100;
  int num_user_guide_bars = get_num_user_guide_bars();
  for (i = 0; i < num_user_guide_bars; i++) {
    last_y = draw_guide_label(cr, get_user_guide_bar(i), last_y);
  }
}

/**
 * Draws the text for the indicated guide bar label to the right of the graph,
 * unless it would overlap with the indicated last label, whose top pixel
 * value is given.  Returns the top pixel value of the new label.
 */
int GtkStatsStripChart::
draw_guide_label(cairo_t *cr, const PStatGraph::GuideBar &bar, int last_y) {
  switch (bar._style) {
  case GBS_target:
    cairo_set_source_rgb(cr, rgb_light_gray[0], rgb_light_gray[1], rgb_light_gray[2]);
    break;

  case GBS_user:
    cairo_set_source_rgb(cr, rgb_user_guide_bar[0], rgb_user_guide_bar[1], rgb_user_guide_bar[2]);
    break;

  case GBS_normal:
    cairo_set_source_rgb(cr, rgb_dark_gray[0], rgb_dark_gray[1], rgb_dark_gray[2]);
    break;
  }

  int y = height_to_pixel(bar._height);
  const std::string &label = bar._label;

  PangoLayout *layout = gtk_widget_create_pango_layout(_window, label.c_str());
  int width, height;
  pango_layout_get_pixel_size(layout, &width, &height);

  if (bar._style != GBS_user) {
    double from_height = pixel_to_height(y + height);
    double to_height = pixel_to_height(y - height);
    if (find_user_guide_bar(from_height, to_height) >= 0) {
      // Omit the label: there's a user-defined guide bar in the same space.
      g_object_unref(layout);
      return last_y;
    }
  }

  if (y >= 0 && y < get_ysize()) {
    // Now convert our y to a coordinate within our drawing area.
    int junk_x;

    // The y coordinate comes from the graph_window.
    gtk_widget_translate_coordinates(_graph_window, _scale_area,
             0, y,
             &junk_x, &y);

    int this_y = y - height / 2;
    if (last_y < this_y || last_y > this_y + height) {
      cairo_move_to(cr, 0, this_y);
      pango_cairo_show_layout(cr, layout);
      last_y = this_y;
    }
  }

  g_object_unref(layout);
  return last_y;
}

/**
 * Called when the smooth check box is toggled.
 */
void GtkStatsStripChart::
toggled_callback(GtkToggleButton *button, gpointer data) {
  GtkStatsStripChart *self = (GtkStatsStripChart *)data;

  bool active = gtk_toggle_button_get_active(button);
  self->set_average_mode(active);
}

/**
 * Draws in the scale labels.
 */
gboolean GtkStatsStripChart::
draw_callback(GtkWidget *widget, cairo_t *cr, gpointer data) {
  GtkStatsStripChart *self = (GtkStatsStripChart *)data;
  self->draw_guide_labels(cr);

  return TRUE;
}
