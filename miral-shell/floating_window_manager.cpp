/*
 * Copyright © 2016-2017 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "floating_window_manager.h"
#include "decoration_provider.h"

#include <miral/application_info.h>
#include <miral/internal_client.h>
#include <miral/window_info.h>
#include <miral/window_manager_tools.h>

#include <linux/input.h>
#include <csignal>

using namespace miral;

namespace
{
int const title_bar_height = 12;

struct PolicyData
{
    bool in_hidden_workspace{false};

    MirWindowState old_state;
};

inline PolicyData& policy_data_for(WindowInfo const& info)
{
    return *std::static_pointer_cast<PolicyData>(info.userdata());
}
}

FloatingWindowManagerPolicy::FloatingWindowManagerPolicy(
    WindowManagerTools const& tools,
    SpinnerSplash const& spinner,
    miral::InternalClientLauncher const& launcher,
    std::function<void()>& shutdown_hook) :
    CanonicalWindowManagerPolicy(tools),
    spinner{spinner},
    decoration_provider{std::make_unique<DecorationProvider>(tools)}
{
    launcher.launch("decorations", *decoration_provider);
    shutdown_hook = [this] { decoration_provider->stop(); };

    for (auto key : {KEY_F1, KEY_F2, KEY_F3, KEY_F4})
        key_to_workspace[key] = this->tools.create_workspace();

    active_workspace = key_to_workspace[KEY_F1];
}

FloatingWindowManagerPolicy::~FloatingWindowManagerPolicy() = default;

bool FloatingWindowManagerPolicy::handle_pointer_event(MirPointerEvent const* event)
{
    auto const action = mir_pointer_event_action(event);
    auto const modifiers = mir_pointer_event_modifiers(event) & modifier_mask;
    Point const cursor{
        mir_pointer_event_axis_value(event, mir_pointer_axis_x),
        mir_pointer_event_axis_value(event, mir_pointer_axis_y)};

    bool consumes_event = false;
    bool is_resize_event = false;

    if (action == mir_pointer_action_button_down)
    {
        if (auto const window = tools.window_at(cursor))
            tools.select_active_window(window);
    }
    else if (action == mir_pointer_action_motion &&
             modifiers == mir_input_event_modifier_alt)
    {
        if (mir_pointer_event_button_state(event, mir_pointer_button_primary))
        {
            if (auto const target = tools.window_at(old_cursor))
            {
                if (tools.select_active_window(target) == target)
                    tools.drag_active_window(cursor - old_cursor);
            }
            consumes_event = true;
        }

        if (mir_pointer_event_button_state(event, mir_pointer_button_tertiary))
        {
            {   // Workaround for lp:1627697
                auto now = std::chrono::steady_clock::now();
                if (resizing && now < last_resize+std::chrono::milliseconds(20))
                    return true;

                last_resize = now;
            }

            if (!resizing)
                tools.select_active_window(tools.window_at(old_cursor));
            is_resize_event = resize(tools.active_window(), cursor, old_cursor);
            consumes_event = true;
        }
    }

    if (!consumes_event && action == mir_pointer_action_motion && !modifiers)
    {
        if (mir_pointer_event_button_state(event, mir_pointer_button_primary))
        {
            if (auto const possible_titlebar = tools.window_at(old_cursor))
            {
                auto const& info = tools.info_for(possible_titlebar);
                if (decoration_provider->is_titlebar(info))
                {
                    if (tools.select_active_window(info.parent()) == info.parent())
                        tools.drag_active_window(cursor - old_cursor);
                    consumes_event = true;
                }
            }
        }
    }

    if (resizing && !is_resize_event)
        end_resize();

    resizing = is_resize_event;
    old_cursor = cursor;
    return consumes_event;
}

void FloatingWindowManagerPolicy::end_resize()
{
    if (!resizing  && !pinching)
        return;

    if (auto window = tools.active_window())
    {
        auto& window_info = tools.info_for(window);

        auto new_size = window.size();
        auto new_pos  = window.top_left();
        window_info.constrain_resize(new_pos, new_size);

        WindowSpecification modifications;
        modifications.top_left() = new_pos;
        modifications.size() = new_size;
        tools.modify_window(window_info, modifications);
    }

    resizing = false;
    pinching = false;
}

bool FloatingWindowManagerPolicy::handle_touch_event(MirTouchEvent const* event)
{
    auto const count = mir_touch_event_point_count(event);

    long total_x = 0;
    long total_y = 0;

    for (auto i = 0U; i != count; ++i)
    {
        total_x += mir_touch_event_axis_value(event, i, mir_touch_axis_x);
        total_y += mir_touch_event_axis_value(event, i, mir_touch_axis_y);
    }

    Point cursor{total_x/count, total_y/count};

    bool is_drag = true;
    for (auto i = 0U; i != count; ++i)
    {
        switch (mir_touch_event_action(event, i))
        {
        case mir_touch_action_up:
            return false;

        case mir_touch_action_down:
            is_drag = false;

        default:
            continue;
        }
    }

    int touch_pinch_top = std::numeric_limits<int>::max();
    int touch_pinch_left = std::numeric_limits<int>::max();
    int touch_pinch_width = 0;
    int touch_pinch_height = 0;

    for (auto i = 0U; i != count; ++i)
    {
        for (auto j = 0U; j != i; ++j)
        {
            int dx = mir_touch_event_axis_value(event, i, mir_touch_axis_x) -
                     mir_touch_event_axis_value(event, j, mir_touch_axis_x);

            int dy = mir_touch_event_axis_value(event, i, mir_touch_axis_y) -
                     mir_touch_event_axis_value(event, j, mir_touch_axis_y);

            if (touch_pinch_width < dx)
                touch_pinch_width = dx;

            if (touch_pinch_height < dy)
                touch_pinch_height = dy;
        }

        int const x = mir_touch_event_axis_value(event, i, mir_touch_axis_x);

        int const y = mir_touch_event_axis_value(event, i, mir_touch_axis_y);

        if (touch_pinch_top > y)
            touch_pinch_top = y;

        if (touch_pinch_left > x)
            touch_pinch_left = x;
    }

    bool consumes_event = false;
    if (is_drag)
    {
        if (count == 3)
        {
            if (auto window = tools.active_window())
            {
                auto const old_size = window.size();
                auto const delta_width = DeltaX{touch_pinch_width - old_touch_pinch_width};
                auto const delta_height = DeltaY{touch_pinch_height - old_touch_pinch_height};

                auto new_width = std::max(old_size.width + delta_width, Width{5});
                auto new_height = std::max(old_size.height + delta_height, Height{5});
                Displacement delta{
                    DeltaX{touch_pinch_left - old_touch_pinch_left},
                    DeltaY{touch_pinch_top  - old_touch_pinch_top}};

                auto& window_info = tools.info_for(window);
                keep_size_within_limits(window_info, delta, new_width, new_height);

                auto new_pos = window.top_left() + delta;
                Size new_size{new_width, new_height};

                {   // Workaround for lp:1627697
                    auto now = std::chrono::steady_clock::now();
                    if (pinching && now < last_resize+std::chrono::milliseconds(20))
                        return true;

                    last_resize = now;
                }

                WindowSpecification modifications;
                modifications.top_left() = new_pos;
                modifications.size() = new_size;
                tools.modify_window(window_info, modifications);
                pinching = true;
            }
            consumes_event = true;
        }
    }
    else
    {
        if (auto const& window = tools.window_at(cursor))
            tools.select_active_window(window);
    }

    if (!consumes_event && pinching)
        end_resize();

    old_cursor = cursor;
    old_touch_pinch_top = touch_pinch_top;
    old_touch_pinch_left = touch_pinch_left;
    old_touch_pinch_width = touch_pinch_width;
    old_touch_pinch_height = touch_pinch_height;
    return consumes_event;
}

void FloatingWindowManagerPolicy::advise_new_window(WindowInfo const& window_info)
{
    CanonicalWindowManagerPolicy::advise_new_window(window_info);

    auto const parent = window_info.parent();

    if (decoration_provider->is_titlebar(window_info))
    {
        decoration_provider->advise_new_titlebar(window_info);

        if (tools.active_window() == parent)
            decoration_provider->paint_titlebar_for(tools.info_for(parent), 0xFF);
        else
            decoration_provider->paint_titlebar_for(tools.info_for(parent), 0x3F);
    }

    if (!parent)
        tools.add_tree_to_workspace(window_info.window(), active_workspace);
    else
    {
        if (policy_data_for(tools.info_for(parent)).in_hidden_workspace)
            apply_workspace_hidden_to(window_info.window());
    }
}

void FloatingWindowManagerPolicy::handle_window_ready(WindowInfo& window_info)
{
    if (window_info.window().application() != spinner.session() && window_info.needs_titlebar(window_info.type()))
        decoration_provider->create_titlebar_for(window_info.window());

    CanonicalWindowManagerPolicy::handle_window_ready(window_info);
}

void FloatingWindowManagerPolicy::advise_focus_lost(WindowInfo const& info)
{
    CanonicalWindowManagerPolicy::advise_focus_lost(info);

    decoration_provider->paint_titlebar_for(info, 0x3F);
}

void FloatingWindowManagerPolicy::advise_focus_gained(WindowInfo const& info)
{
    CanonicalWindowManagerPolicy::advise_focus_gained(info);

    decoration_provider->paint_titlebar_for(info, 0xFF);

    // Frig to force the spinner to the top
    if (auto const spinner_session = spinner.session())
    {
        auto const& spinner_info = tools.info_for(spinner_session);

        if (spinner_info.windows().size() > 0)
            tools.raise_tree(spinner_info.windows()[0]);
    }
}

void FloatingWindowManagerPolicy::advise_state_change(WindowInfo const& window_info, MirWindowState state)
{
    CanonicalWindowManagerPolicy::advise_state_change(window_info, state);

    decoration_provider->advise_state_change(window_info, state);
}

void FloatingWindowManagerPolicy::advise_resize(WindowInfo const& window_info, Size const& new_size)
{
    CanonicalWindowManagerPolicy::advise_resize(window_info, new_size);

    decoration_provider->resize_titlebar_for(window_info, new_size);
}

void FloatingWindowManagerPolicy::advise_delete_window(WindowInfo const& window_info)
{
    CanonicalWindowManagerPolicy::advise_delete_window(window_info);

    decoration_provider->destroy_titlebar_for(window_info.window());
}

bool FloatingWindowManagerPolicy::handle_keyboard_event(MirKeyboardEvent const* event)
{
    auto const action = mir_keyboard_event_action(event);
    auto const scan_code = mir_keyboard_event_scan_code(event);
    auto const modifiers = mir_keyboard_event_modifiers(event) & modifier_mask;

    // Switch workspaces
    if (action == mir_keyboard_action_down &&
        modifiers == (mir_input_event_modifier_alt | mir_input_event_modifier_meta))
    {
        switch (scan_code)
        {
        case KEY_F1:
        case KEY_F2:
        case KEY_F3:
        case KEY_F4:
            switch_workspace_to(key_to_workspace[scan_code]);
            return true;
        }
    }

    // Switch workspace taking the active window
    if (action == mir_keyboard_action_down &&
        modifiers == (mir_input_event_modifier_ctrl | mir_input_event_modifier_meta))
    {
        switch (scan_code)
        {
        case KEY_F1:
        case KEY_F2:
        case KEY_F3:
        case KEY_F4:
            switch_workspace_to(key_to_workspace[scan_code], tools.active_window());
            return true;
        }
    }

    if (action != mir_keyboard_action_repeat)
        end_resize();

    if (action == mir_keyboard_action_down && scan_code == KEY_F11)
    {
        switch (modifiers)
        {
        case mir_input_event_modifier_alt:
            toggle(mir_window_state_maximized);
            return true;

        case mir_input_event_modifier_shift:
            toggle(mir_window_state_vertmaximized);
            return true;

        case mir_input_event_modifier_ctrl:
            toggle(mir_window_state_horizmaximized);
            return true;

        case mir_input_event_modifier_meta:
            toggle(mir_window_state_fullscreen);
            return true;

        default:
            break;
        }
    }
    else if (action == mir_keyboard_action_down && scan_code == KEY_F4)
    {
        switch (modifiers)
        {
        case mir_input_event_modifier_alt|mir_input_event_modifier_shift:
            if (auto const& window = tools.active_window())
                kill(window.application(), SIGTERM);
            return true;

        case mir_input_event_modifier_alt:
            tools.ask_client_to_close(tools.active_window());;
            return true;

        default:
            break;
        }
    }
    else if (action == mir_keyboard_action_down &&
             modifiers == mir_input_event_modifier_alt &&
             scan_code == KEY_TAB)
    {
        tools.focus_next_application();

        return true;
    }
    else if (action == mir_keyboard_action_down &&
             modifiers == mir_input_event_modifier_alt &&
             scan_code == KEY_GRAVE)
    {
        tools.focus_next_within_application();

        return true;
    }
    else if (action == mir_keyboard_action_down &&
             modifiers == (mir_input_event_modifier_alt | mir_input_event_modifier_shift) &&
             scan_code == KEY_GRAVE)
    {
        tools.focus_prev_within_application();

        return true;
    }
    else if (action == mir_keyboard_action_down &&
             modifiers == (mir_input_event_modifier_ctrl|mir_input_event_modifier_meta))
    {
        if (auto active_window = tools.active_window())
        {
            auto active_display = tools.active_display();
            auto& window_info = tools.info_for(active_window);
            bool consume{true};
            WindowSpecification modifications;

            switch (scan_code)
            {
            case KEY_LEFT:
                modifications.top_left() = Point{active_display.top_left.x, active_window.top_left().y};
                break;

            case KEY_RIGHT:
                modifications.top_left() = Point{
                    (active_display.bottom_right() - as_displacement(active_window.size())).x,
                    active_window.top_left().y};
                break;

            case KEY_UP:
                if (window_info.state() != mir_window_state_vertmaximized &&
                    window_info.state() != mir_window_state_maximized)
                {
                    modifications.top_left() =
                        Point{active_window.top_left().x, active_display.top_left.y} + DeltaY{title_bar_height};
                }
                break;

            case KEY_DOWN:
                modifications.top_left() = Point{
                    active_window.top_left().x,
                    (active_display.bottom_right() - as_displacement(active_window.size())).y};
                break;

            default:
                consume = false;
            }

            if (modifications.top_left().is_set())
                tools.modify_window(window_info, modifications);

            if (consume)
                return true;
        }
    }

    return false;
}

void FloatingWindowManagerPolicy::toggle(MirWindowState state)
{
    if (auto const window = tools.active_window())
    {
        auto& info = tools.info_for(window);

        WindowSpecification modifications;

        modifications.state() = (info.state() == state) ? mir_window_state_restored : state;
        tools.place_and_size_for_state(modifications, info);
        tools.modify_window(info, modifications);
    }
}

bool FloatingWindowManagerPolicy::resize(Window const& window, Point cursor, Point old_cursor)
{
    if (!window)
        return false;

    auto& window_info = tools.info_for(window);

    auto const top_left = window.top_left();
    Rectangle const old_pos{top_left, window.size()};

    if (!resizing)
    {
        auto anchor = old_pos.bottom_right();

        for (auto const& corner : {
            old_pos.top_right(),
            old_pos.bottom_left(),
            top_left})
        {
            if ((old_cursor - anchor).length_squared() <
                (old_cursor - corner).length_squared())
            {
                anchor = corner;
            }
        }

        left_resize = anchor.x != top_left.x;
        top_resize  = anchor.y != top_left.y;
    }

    int const x_sign = left_resize? -1 : 1;
    int const y_sign = top_resize?  -1 : 1;

    auto delta = cursor-old_cursor;

    auto new_width = old_pos.size.width + x_sign * delta.dx;
    auto new_height = old_pos.size.height + y_sign * delta.dy;

    keep_size_within_limits(window_info, delta, new_width, new_height);

    Size new_size{new_width, new_height};
    Point new_pos = top_left + left_resize*delta.dx + top_resize*delta.dy;

    WindowSpecification modifications;
    modifications.top_left() = new_pos;
    modifications.size() = new_size;
    tools.modify_window(window, modifications);

    return true;
}

void FloatingWindowManagerPolicy::keep_size_within_limits(
    WindowInfo const& window_info, Displacement& delta, Width& new_width, Height& new_height) const
{
    auto const min_width  = std::max(window_info.min_width(), Width{5});
    auto const min_height = std::max(window_info.min_height(), Height{5});

    if (new_width < min_width)
    {
        new_width = min_width;
        if (delta.dx > DeltaX{0})
            delta.dx = DeltaX{0};
    }

    if (new_height < min_height)
    {
        new_height = min_height;
        if (delta.dy > DeltaY{0})
            delta.dy = DeltaY{0};
    }

    auto const max_width  = window_info.max_width();
    auto const max_height = window_info.max_height();

    if (new_width > max_width)
    {
        new_width = max_width;
        if (delta.dx < DeltaX{0})
            delta.dx = DeltaX{0};
    }

    if (new_height > max_height)
    {
        new_height = max_height;
        if (delta.dy < DeltaY{0})
            delta.dy = DeltaY{0};
    }
}

WindowSpecification FloatingWindowManagerPolicy::place_new_window(
    ApplicationInfo const& app_info, WindowSpecification const& request_parameters)
{
    auto parameters = CanonicalWindowManagerPolicy::place_new_window(app_info, request_parameters);

    bool const needs_titlebar = WindowInfo::needs_titlebar(parameters.type().value());

    if (parameters.state().value() != mir_window_state_fullscreen && needs_titlebar)
        parameters.top_left() = Point{parameters.top_left().value().x, parameters.top_left().value().y + DeltaY{title_bar_height}};

    if (app_info.application() == decoration_provider->session())
        decoration_provider->place_new_decoration(parameters);

    parameters.userdata() = std::make_shared<PolicyData>();
    return parameters;
}

void FloatingWindowManagerPolicy::advise_adding_to_workspace(
    std::shared_ptr<Workspace> const& workspace, std::vector<Window> const& windows)
{
    if (windows.empty())
        return;

    for (auto const& window : windows)
    {
        if (workspace == active_workspace)
        {
            apply_workspace_visible_to(window);
        }
        else
        {
            apply_workspace_hidden_to(window);
        }
    }
}

void FloatingWindowManagerPolicy::switch_workspace_to(
    std::shared_ptr<Workspace> const& workspace,
    Window const& window)
{
    if (workspace == active_workspace)
        return;

    auto const old_active = active_workspace;
    active_workspace = workspace;

    auto const old_active_window = tools.active_window();

    if (!old_active_window)
    {
        // If there's no active window, the first shown grabs focus: get the right one
        if (auto const ww = workspace_to_active[workspace])
        {
            tools.for_each_workspace_containing(ww, [&](std::shared_ptr<miral::Workspace> const& ws)
                {
                    if (ws == workspace)
                    {
                        apply_workspace_visible_to(ww);
                    }
                });
        }
    }

    tools.remove_tree_from_workspace(window, old_active);
    tools.add_tree_to_workspace(window, active_workspace);

    tools.for_each_window_in_workspace(active_workspace, [&](Window const& window)
        {
            if (decoration_provider->is_decoration(window))
                return; // decorations are taken care of automatically

        apply_workspace_visible_to(window);
        });

    bool hide_old_active = false;
    tools.for_each_window_in_workspace(old_active, [&](Window const& window)
        {
            if (decoration_provider->is_decoration(window))
                return; // decorations are taken care of automatically

            if (window == old_active_window)
            {
                // If we hide the active window focus will shift: do that last
                hide_old_active = true;
                return;
            }

        apply_workspace_hidden_to(window);
        });

    if (hide_old_active)
    {
        apply_workspace_hidden_to(old_active_window);

        // Remember the old active_window when we switch away
        workspace_to_active[old_active] = old_active_window;
    }
}

void FloatingWindowManagerPolicy::apply_workspace_hidden_to(Window const& window)
{
    auto const& window_info = tools.info_for(window);
    auto& pdata = policy_data_for(window_info);
    if (!pdata.in_hidden_workspace)
    {
        pdata.in_hidden_workspace = true;
        pdata.old_state = window_info.state();

        WindowSpecification modifications;
        modifications.state() = mir_window_state_hidden;
        tools.place_and_size_for_state(modifications, window_info);
        tools.modify_window(window_info.window(), modifications);
    }
}

void FloatingWindowManagerPolicy::apply_workspace_visible_to(Window const& window)
{
    auto const& window_info = tools.info_for(window);
    auto& pdata = policy_data_for(window_info);
    if (pdata.in_hidden_workspace)
    {
        pdata.in_hidden_workspace = false;
        WindowSpecification modifications;
        modifications.state() = pdata.old_state;
        tools.place_and_size_for_state(modifications, window_info);
        tools.modify_window(window_info.window(), modifications);
    }
}

void FloatingWindowManagerPolicy::handle_modify_window(WindowInfo& window_info, WindowSpecification const& modifications)
{
    auto mods = modifications;

    auto& pdata = policy_data_for(window_info);

    if (pdata.in_hidden_workspace && mods.state().is_set())
        pdata.old_state = mods.state().consume();

    CanonicalWindowManagerPolicy::handle_modify_window(window_info, mods);
}

