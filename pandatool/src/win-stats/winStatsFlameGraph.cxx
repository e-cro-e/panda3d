/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file winStatsFlameGraph.cxx
 * @author rdb
 * @date 2022-01-28
 */

#include "winStatsFlameGraph.h"
#include "winStatsLabel.h"
#include "winStatsMonitor.h"
#include "pStatCollectorDef.h"

static const int default_flame_graph_width = 800;
static const int default_flame_graph_height = 150;

bool WinStatsFlameGraph::_window_class_registered = false;
const char * const WinStatsFlameGraph::_window_class_name = "flame";

/**
 *
 */
WinStatsFlameGraph::
WinStatsFlameGraph(WinStatsMonitor *monitor, int thread_index,
                   int collector_index) :
  PStatFlameGraph(monitor, monitor->get_view(thread_index),
                  thread_index, collector_index,
                  monitor->get_pixel_scale() * default_flame_graph_width / 4,
                  monitor->get_pixel_scale() * default_flame_graph_height / 4),
  WinStatsGraph(monitor)
{
  _left_margin = _pixel_scale * 2;
  _right_margin = _pixel_scale * 2;
  _top_margin = _pixel_scale * 6;
  _bottom_margin = _pixel_scale * 2;

  // Let's show the units on the guide bar labels.  There's room.
  set_guide_bar_units(get_guide_bar_units() | GBU_show_units);

  _average_check_box = 0;

  create_window();
  clear_region();
}

/**
 *
 */
WinStatsFlameGraph::
~WinStatsFlameGraph() {
}

/**
 * Called as each frame's data is made available.  There is no guarantee the
 * frames will arrive in order, or that all of them will arrive at all.  The
 * monitor should be prepared to accept frames received out-of-order or
 * missing.
 */
void WinStatsFlameGraph::
new_data(int thread_index, int frame_number) {
  if (is_title_unknown()) {
    std::string window_title = get_title_text();
    if (!is_title_unknown()) {
      SetWindowText(_window, window_title.c_str());
    }
  }

  if (!_pause) {
    update();

    std::string text = format_number(get_horizontal_scale(), get_guide_bar_units(), get_guide_bar_unit_name());
    if (_net_value_text != text) {
      _net_value_text = text;
      RECT rect;
      GetClientRect(_window, &rect);
      rect.bottom = _top_margin;
      InvalidateRect(_window, &rect, TRUE);
    }
  }
}

/**
 * Called when it is necessary to redraw the entire graph.
 */
void WinStatsFlameGraph::
force_redraw() {
  PStatFlameGraph::force_redraw();
}

/**
 * Called when the user has resized the window, forcing a resize of the graph.
 */
void WinStatsFlameGraph::
changed_graph_size(int graph_xsize, int graph_ysize) {
  PStatFlameGraph::changed_size(graph_xsize, graph_ysize);
}

/**
 * Called when the user selects a new time units from the monitor pulldown
 * menu, this should adjust the units for the graph to the indicated mask if
 * it is a time-based graph.
 */
void WinStatsFlameGraph::
set_time_units(int unit_mask) {
  int old_unit_mask = get_guide_bar_units();
  if ((old_unit_mask & (GBU_hz | GBU_ms)) != 0) {
    unit_mask = unit_mask & (GBU_hz | GBU_ms);
    unit_mask |= (old_unit_mask & GBU_show_units);
    set_guide_bar_units(unit_mask);

    RECT rect;
    GetClientRect(_window, &rect);
    rect.left = _right_margin;
    InvalidateRect(_window, &rect, TRUE);
  }
}

/**
 * Called when the user single-clicks on a label.
 */
void WinStatsFlameGraph::
on_click_label(int collector_index) {
  int prev_collector_index = get_collector_index();
  if (collector_index == prev_collector_index && collector_index != 0) {
    // Clicking on the top label means to go up to the parent level.
    const PStatClientData *client_data =
      WinStatsGraph::_monitor->get_client_data();
    if (client_data->has_collector(collector_index)) {
      const PStatCollectorDef &def =
        client_data->get_collector_def(collector_index);
      collector_index = def._parent_index;
      set_collector_index(collector_index);
    }
  }
  else {
    // Clicking on any other label means to focus on that.
    set_collector_index(collector_index);
  }

  // Change the root collector to show the full name.
  if (prev_collector_index != collector_index) {
    auto it = _labels.find(prev_collector_index);
    if (it != _labels.end()) {
      it->second->update_text(false);
    }
    it = _labels.find(collector_index);
    if (it != _labels.end()) {
      it->second->update_text(true);
    }
  }
}

/**
 * Called when the user hovers the mouse over a label.
 */
void WinStatsFlameGraph::
on_enter_label(int collector_index) {
  if (collector_index != _highlighted_index) {
    _highlighted_index = collector_index;
  }
}

/**
 * Called when the user's mouse cursor leaves a label.
 */
void WinStatsFlameGraph::
on_leave_label(int collector_index) {
  if (collector_index == _highlighted_index && collector_index != -1) {
    _highlighted_index = -1;
  }
}

/**
 * Repositions the labels.
 */
void WinStatsFlameGraph::
update_labels() {
  if (_graph_window) {
    PStatFlameGraph::update_labels();
  }
}

/**
 * Repositions a label.  If width is 0, the label should be deleted.
 */
void WinStatsFlameGraph::
update_label(int collector_index, int row, int x, int width) {
  WinStatsLabel *label;

  auto it = _labels.find(collector_index);
  if (it != _labels.end()) {
    if (width == 0) {
      delete it->second;
      _labels.erase(it);
      return;
    }
    label = it->second;
  } else {
    if (width == 0) {
      return;
    }
    label = new WinStatsLabel(WinStatsGraph::_monitor, this, _thread_index, collector_index, false, false);
    _labels[collector_index] = label;
    label->setup(_graph_window);
  }

  label->set_pos(x, _ysize - 2 - row * label->get_height(), std::min(width, _xsize - 2));
}

/**
 * Calls update_guide_bars with parameters suitable to this kind of graph.
 */
void WinStatsFlameGraph::
normal_guide_bars() {
  // We want vaguely 100 pixels between guide bars.
  int num_bars = get_xsize() / (_pixel_scale * 25);

  _guide_bars.clear();

  double dist = get_horizontal_scale() / num_bars;

  for (int i = 1; i < num_bars; ++i) {
    _guide_bars.push_back(make_guide_bar(i * dist));
  }

  _guide_bars_changed = true;
}

/**
 * Erases the chart area.
 */
void WinStatsFlameGraph::
clear_region() {
  RECT rect = { 0, 0, get_xsize(), get_ysize() };
  FillRect(_bitmap_dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
}

/**
 * Erases the chart area in preparation for drawing a bunch of bars.
 */
void WinStatsFlameGraph::
begin_draw() {
  clear_region();

  // Draw in the guide bars.
  int num_guide_bars = get_num_guide_bars();
  for (int i = 0; i < num_guide_bars; i++) {
    draw_guide_bar(_bitmap_dc, get_guide_bar(i));
  }
}

/**
 * Called after all the bars have been drawn, this triggers a refresh event to
 * draw it to the window.
 */
void WinStatsFlameGraph::
end_draw() {
  InvalidateRect(_graph_window, nullptr, FALSE);
}

/**
 * Called at the end of the draw cycle.
 */
void WinStatsFlameGraph::
idle() {
}

/**
 *
 */
LONG WinStatsFlameGraph::
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_LBUTTONDOWN:
    if (_potential_drag_mode == DM_new_guide_bar) {
      set_drag_mode(DM_new_guide_bar);
      SetCapture(_graph_window);
      return 0;
    }
    break;

  case WM_COMMAND:
    switch (LOWORD(wparam)) {
    case BN_CLICKED:
      if ((HWND)lparam == _average_check_box) {
        int result = SendMessage(_average_check_box, BM_GETCHECK, 0, 0);
        set_average_mode(result == BST_CHECKED);
        return 0;
      }
      break;
    }
    break;

  default:
    break;
  }

  return WinStatsGraph::window_proc(hwnd, msg, wparam, lparam);
}

/**
 *
 */
LONG WinStatsFlameGraph::
graph_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_LBUTTONDOWN:
    if (_potential_drag_mode == DM_guide_bar && _drag_guide_bar >= 0) {
      set_drag_mode(DM_guide_bar);
      int16_t x = LOWORD(lparam);
      _drag_start_x = x;
      SetCapture(_graph_window);
      return 0;
    }
    break;

  case WM_MOUSEMOVE:
    if (_drag_mode == DM_new_guide_bar) {
      // We haven't created the new guide bar yet; we won't until the mouse
      // comes within the graph's region.
      int16_t x = LOWORD(lparam);
      if (x >= 0 && x < get_xsize()) {
        set_drag_mode(DM_guide_bar);
        _drag_guide_bar = add_user_guide_bar(pixel_to_height(x));
        return 0;
      }
    }
    else if (_drag_mode == DM_guide_bar) {
      int16_t x = LOWORD(lparam);
      move_user_guide_bar(_drag_guide_bar, pixel_to_height(x));
      return 0;
    }
    break;

  case WM_LBUTTONUP:
    if (_drag_mode == DM_guide_bar) {
      int16_t x = LOWORD(lparam);
      if (x < 0 || x >= get_xsize()) {
        remove_user_guide_bar(_drag_guide_bar);
      } else {
        move_user_guide_bar(_drag_guide_bar, pixel_to_height(x));
      }
      set_drag_mode(DM_none);
      ReleaseCapture();
      return 0;
    }
    break;

  case WM_LBUTTONDBLCLK:
    {
      // Clicking on whitespace in the graph goes to the parent.
      on_click_label(get_collector_index());
      return 0;
    }
    break;

  default:
    break;
  }

  return WinStatsGraph::graph_window_proc(hwnd, msg, wparam, lparam);
}

/**
 * This is called during the servicing of WM_PAINT; it gives a derived class
 * opportunity to do some further painting into the window (the outer window,
 * not the graph window).
 */
void WinStatsFlameGraph::
additional_window_paint(HDC hdc) {
  // Draw in the labels for the guide bars.
  SelectObject(hdc, WinStatsGraph::_monitor->get_font());
  SetTextAlign(hdc, TA_LEFT | TA_BOTTOM);
  SetBkMode(hdc, TRANSPARENT);

  int y = _top_margin - _pixel_scale / 2;

  int i;
  int num_guide_bars = get_num_guide_bars();
  for (i = 0; i < num_guide_bars; i++) {
    draw_guide_label(hdc, y, get_guide_bar(i));
  }

  int num_user_guide_bars = get_num_user_guide_bars();
  for (i = 0; i < num_user_guide_bars; i++) {
    draw_guide_label(hdc, y, get_user_guide_bar(i));
  }

  RECT rect;
  GetClientRect(_window, &rect);

  // Now draw the "net value" label at the top.
  SetTextAlign(hdc, TA_RIGHT | TA_BOTTOM);
  SetTextColor(hdc, RGB(0, 0, 0));
  TextOut(hdc, rect.right - _right_margin, y,
          _net_value_text.data(), _net_value_text.length());
}

/**
 * This is called during the servicing of WM_PAINT; it gives a derived class
 * opportunity to do some further painting into the window (the outer window,
 * not the graph window).
 */
void WinStatsFlameGraph::
additional_graph_window_paint(HDC hdc) {
  int num_user_guide_bars = get_num_user_guide_bars();
  for (int i = 0; i < num_user_guide_bars; i++) {
    draw_guide_bar(hdc, get_user_guide_bar(i));
  }
}

/**
 * Based on the mouse position within the window's client area, look for
 * draggable things the mouse might be hovering over and return the
 * apprioprate DragMode enum or DM_none if nothing is indicated.
 */
WinStatsGraph::DragMode WinStatsFlameGraph::
consider_drag_start(int mouse_x, int mouse_y, int width, int height) {
  if (mouse_y >= _graph_top && mouse_y < _graph_top + get_ysize()) {
    if (mouse_x >= _graph_left && mouse_x < _graph_left + get_xsize()) {
      // See if the mouse is over a user-defined guide bar.
      int x = mouse_x - _graph_left;
      double from_height = pixel_to_height(x - 2);
      double to_height = pixel_to_height(x + 2);
      _drag_guide_bar = find_user_guide_bar(from_height, to_height);
      if (_drag_guide_bar >= 0) {
        return DM_guide_bar;
      }

    } else if (mouse_x < _left_margin - 2 ||
               mouse_x > width - _right_margin + 2) {
      // The mouse is left or right of the graph; maybe create a new guide
      // bar.
      return DM_new_guide_bar;
    }
  }

  // Don't upcall; there's no point resizing the margins.
  return DM_none;
}

/**
 * Repositions the graph child window within the parent window according to
 * the _margin variables.
 */
void WinStatsFlameGraph::
move_graph_window(int graph_left, int graph_top, int graph_xsize, int graph_ysize) {
  WinStatsGraph::move_graph_window(graph_left, graph_top, graph_xsize, graph_ysize);
  if (_average_check_box != 0) {
    SIZE size;
    SendMessage(_average_check_box, BCM_GETIDEALSIZE, 0, (LPARAM)&size);

    SetWindowPos(_average_check_box, 0,
                 _left_margin, _top_margin - size.cy - _pixel_scale / 2,
                 size.cx, size.cy,
                 SWP_NOZORDER | SWP_SHOWWINDOW);
    InvalidateRect(_average_check_box, nullptr, TRUE);
  }
}

/**
 * Draws the line for the indicated guide bar on the graph.
 */
void WinStatsFlameGraph::
draw_guide_bar(HDC hdc, const PStatGraph::GuideBar &bar) {
  int x = height_to_pixel(bar._height);

  if (x > 0 && x < get_xsize() - 1) {
    // Only draw it if it's not too close to either edge.
    switch (bar._style) {
    case GBS_target:
      SelectObject(hdc, _light_pen);
      break;

    case GBS_user:
      SelectObject(hdc, _user_guide_bar_pen);
      break;

    case GBS_normal:
      SelectObject(hdc, _dark_pen);
      break;
    }
    MoveToEx(hdc, x, 0, nullptr);
    LineTo(hdc, x, get_ysize());
  }
}

/**
 * Draws the text for the indicated guide bar label at the top of the graph.
 */
void WinStatsFlameGraph::
draw_guide_label(HDC hdc, int y, const PStatGraph::GuideBar &bar) {
  switch (bar._style) {
  case GBS_target:
    SetTextColor(hdc, _light_color);
    break;

  case GBS_user:
    SetTextColor(hdc, _user_guide_bar_color);
    break;

  case GBS_normal:
    SetTextColor(hdc, _dark_color);
    break;
  }

  int x = height_to_pixel(bar._height);
  const std::string &label = bar._label;
  SIZE size;
  GetTextExtentPoint32(hdc, label.data(), label.length(), &size);

  if (bar._style != GBS_user) {
    double from_height = pixel_to_height(x - size.cx);
    double to_height = pixel_to_height(x + size.cx);
    if (find_user_guide_bar(from_height, to_height) >= 0) {
      // Omit the label: there's a user-defined guide bar in the same space.
      return;
    }
  }

  int this_x = _graph_left + x - size.cx / 2;
  if (x >= 0 && x < get_xsize()) {
    TextOut(hdc, this_x, y,
            label.data(), label.length());
  }
}

/**
 * Creates the window for this strip chart.
 */
void WinStatsFlameGraph::
create_window() {
  if (_window) {
    return;
  }

  HINSTANCE application = GetModuleHandle(nullptr);
  register_window_class(application);

  std::string window_title = get_title_text();

  RECT win_rect = {
    0, 0,
    _left_margin + get_xsize() + _right_margin,
    _top_margin + get_ysize() + _bottom_margin
  };

  // compute window size based on desired client area size
  AdjustWindowRect(&win_rect, graph_window_style, FALSE);

  _window =
    CreateWindow(_window_class_name, window_title.c_str(), graph_window_style,
                 CW_USEDEFAULT, CW_USEDEFAULT,
                 win_rect.right - win_rect.left,
                 win_rect.bottom - win_rect.top,
                 WinStatsGraph::_monitor->get_window(), nullptr, application, 0);
  if (!_window) {
    nout << "Could not create FlameGraph window!\n";
    exit(1);
  }

  SetWindowLongPtr(_window, 0, (LONG_PTR)this);

  _average_check_box =
    CreateWindow(WC_BUTTON, "Average", WS_CHILD | BS_AUTOCHECKBOX,
                 0, 0, 0, 0,
                 _window, nullptr, application, 0);
  SendMessage(_average_check_box, WM_SETFONT,
              (WPARAM)WinStatsGraph::_monitor->get_font(), TRUE);

  if (get_average_mode()) {
    SendMessage(_average_check_box, BM_SETCHECK, BST_CHECKED, 0);
  }

  // Ensure that the window is on top of the stack.
  SetWindowPos(_window, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

/**
 * Registers the window class for the FlameGraph window, if it has not already
 * been registered.
 */
void WinStatsFlameGraph::
register_window_class(HINSTANCE application) {
  if (_window_class_registered) {
    return;
  }

  WNDCLASS wc;

  ZeroMemory(&wc, sizeof(WNDCLASS));
  wc.style = 0;
  wc.lpfnWndProc = (WNDPROC)static_window_proc;
  wc.hInstance = application;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = _window_class_name;

  // Reserve space to associate the this pointer with the window.
  wc.cbWndExtra = sizeof(WinStatsFlameGraph *);

  if (!RegisterClass(&wc)) {
    nout << "Could not register FlameGraph window class!\n";
    exit(1);
  }

  _window_class_registered = true;
}

/**
 *
 */
LONG WINAPI WinStatsFlameGraph::
static_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  WinStatsFlameGraph *self = (WinStatsFlameGraph *)GetWindowLongPtr(hwnd, 0);
  if (self != nullptr && self->_window == hwnd) {
    return self->window_proc(hwnd, msg, wparam, lparam);
  } else {
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
}
