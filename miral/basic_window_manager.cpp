/*
 * Copyright © 2015-2016 Canonical Ltd.
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

#include "basic_window_manager.h"

#include <mir/scene/session.h>
#include <mir/scene/surface.h>
#include <mir/scene/surface_creation_parameters.h>
#include <mir/shell/display_layout.h>
#include <mir/shell/surface_ready_observer.h>

using namespace mir;

miral::BasicWindowManager::BasicWindowManager(
    shell::FocusController* focus_controller,
    std::shared_ptr<shell::DisplayLayout> const& display_layout,
    WindowManagementPolicyBuilder const& build) :
    focus_controller(focus_controller),
    display_layout(display_layout),
    policy(build(this)),
    surface_builder([](std::shared_ptr<scene::Session> const&, scene::SurfaceCreationParameters const&) -> Window
        { throw std::logic_error{"Can't create a window yet"};})
{
}

auto miral::BasicWindowManager::build_window(
    std::shared_ptr<scene::Session> const& session, WindowSpecification const& spec)
-> WindowInfo&
{
    scene::SurfaceCreationParameters parameters;
    spec.update(parameters);

    auto result = surface_builder(session, parameters);
    auto& info = window_info.emplace(result, WindowInfo{result, parameters}).first->second;
    if (auto const parent = parameters.parent.lock())
        info.parent = info_for(parent).window;
    return info;
}

void miral::BasicWindowManager::add_session(std::shared_ptr<scene::Session> const& session)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    app_info[session] = ApplicationInfo();
    policy->handle_app_info_updated(displays);
}

void miral::BasicWindowManager::remove_session(std::shared_ptr<scene::Session> const& session)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    app_info.erase(session);
    policy->handle_app_info_updated(displays);
}

auto miral::BasicWindowManager::add_surface(
    std::shared_ptr<scene::Session> const& session,
    scene::SurfaceCreationParameters const& params,
    std::function<frontend::SurfaceId(std::shared_ptr<scene::Session> const& session, scene::SurfaceCreationParameters const& params)> const& build)
-> frontend::SurfaceId
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    surface_builder = [build](std::shared_ptr<scene::Session> const& session, scene::SurfaceCreationParameters const& params)
        { return Window{session, build(session, params)}; };

    auto& session_info = info_for(session);
    auto& window_info = build_window(session, policy->handle_place_new_surface(session_info, params));

    auto const window = window_info.window;

    session_info.windows.push_back(window);

    if (auto const parent = window_info.parent)
        info_for(parent).children.push_back(window);

    policy->handle_new_window(window_info);
    policy->generate_decorations_for(window_info);

    if (window_info.can_be_active())
    {
        std::shared_ptr<scene::Surface> const scene_surface = window_info.window;
        scene_surface->add_observer(std::make_shared<shell::SurfaceReadyObserver>(
            [this, &window_info](std::shared_ptr<scene::Session> const&, std::shared_ptr<scene::Surface> const&)
                { policy->handle_window_ready(window_info); },
            session,
            scene_surface));
    }

    return window_info.window.surface_id();
}

void miral::BasicWindowManager::modify_surface(
    std::shared_ptr<scene::Session> const& /*session*/,
    std::shared_ptr<scene::Surface> const& surface,
    shell::SurfaceSpecification const& modifications)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    policy->handle_modify_window(info_for(surface), modifications);
}

void miral::BasicWindowManager::remove_surface(
    std::shared_ptr<scene::Session> const& session,
    std::weak_ptr<scene::Surface> const& surface)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    bool const is_active_window{surface.lock() == focus_controller->focused_surface()};

    auto& info = info_for(surface);

    if (auto const parent = info.parent)
    {
        auto& siblings = info_for(parent).children;

        for (auto i = begin(siblings); i != end(siblings); ++i)
        {
            if (info.window == *i)
            {
                siblings.erase(i);
                break;
            }
        }
    }

    auto& windows = info_for(session).windows;

    for (auto i = begin(windows); i != end(windows); ++i)
    {
        if (info.window == *i)
        {
            windows.erase(i);
            break;
        }
    }

    policy->handle_delete_window(info);

    session->destroy_surface(surface);

    auto const parent = info.parent;

    // NB this invalidates info, but we want to keep access to "parent".
    window_info.erase(surface);

    if (is_active_window)
    {
        // Try to make the parent active
        if (parent)
        {
            if (policy->select_active_window(parent))
                return;
        }

        // Ought to find top window of same application, but we don't
        // have the API (yet), so find any suitable top-level-window
        for (auto const& tlw : windows)
        {
            if (policy->select_active_window(tlw))
                return;
        }

        focus_next_application();
        policy->select_active_window(focused_window());
    }
}

void miral::BasicWindowManager::destroy(Window& window)
{
    window.session()->destroy_surface(window.surface_id());
    window_info.erase(window);
}

void miral::BasicWindowManager::add_display(geometry::Rectangle const& area)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    displays.add(area);
    policy->handle_displays_updated(displays);
}

void miral::BasicWindowManager::remove_display(geometry::Rectangle const& area)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    displays.remove(area);
    policy->handle_displays_updated(displays);
}

bool miral::BasicWindowManager::handle_keyboard_event(MirKeyboardEvent const* event)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    update_event_timestamp(event);
    return policy->handle_keyboard_event(event);
}

bool miral::BasicWindowManager::handle_touch_event(MirTouchEvent const* event)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    update_event_timestamp(event);
    return policy->handle_touch_event(event);
}

bool miral::BasicWindowManager::handle_pointer_event(MirPointerEvent const* event)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    update_event_timestamp(event);

    cursor = {
        mir_pointer_event_axis_value(event, mir_pointer_axis_x),
        mir_pointer_event_axis_value(event, mir_pointer_axis_y)};

    return policy->handle_pointer_event(event);
}

void miral::BasicWindowManager::handle_raise_surface(
    std::shared_ptr<scene::Session> const& /*session*/,
    std::shared_ptr<scene::Surface> const& surface,
    uint64_t timestamp)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    if (timestamp >= last_input_event_timestamp)
        policy->handle_raise_window(info_for(surface));
}

int miral::BasicWindowManager::set_surface_attribute(
    std::shared_ptr<scene::Session> const& /*session*/,
    std::shared_ptr<scene::Surface> const& surface,
    MirSurfaceAttrib attrib,
    int value)
{
    std::lock_guard<decltype(mutex)> lock(mutex);
    switch (attrib)
    {
    case mir_surface_attrib_state:
    {
        return policy->handle_set_state(info_for(surface), MirSurfaceState(value));
    }
    default:
        return surface->configure(attrib, value);
    }
}

auto miral::BasicWindowManager::count_applications() const
-> unsigned int
{
    return app_info.size();
}


void miral::BasicWindowManager::for_each_application(std::function<void(ApplicationInfo& info)> const& functor)
{
    for(auto& info : app_info)
    {
        functor(info.second);
    }
}

auto miral::BasicWindowManager::find_application(std::function<bool(ApplicationInfo const& info)> const& predicate)
-> Application
{
    for(auto& info : app_info)
    {
        if (predicate(info.second))
        {
            return Application{info.first};
        }
    }

    return Application{};
}

auto miral::BasicWindowManager::info_for(std::weak_ptr<scene::Session> const& session) const
-> ApplicationInfo&
{
    return const_cast<ApplicationInfo&>(app_info.at(session));
}

auto miral::BasicWindowManager::info_for(std::weak_ptr<scene::Surface> const& surface) const
-> WindowInfo&
{
    return const_cast<WindowInfo&>(window_info.at(surface));
}

auto miral::BasicWindowManager::info_for(Window const& window) const
-> WindowInfo&
{
    return info_for(std::weak_ptr<mir::scene::Surface>(window));
}

auto miral::BasicWindowManager::focused_application() const
-> Application
{
    return focus_controller->focused_session();
}

auto miral::BasicWindowManager::focused_window() const
-> Window
{
    auto focussed_surface = focus_controller->focused_surface();
    return focussed_surface ? info_for(focussed_surface).window :Window{};
}

void miral::BasicWindowManager::focus_next_application()
{
    focus_controller->focus_next_session();
}

void miral::BasicWindowManager::set_focus_to(Window const& window)
{
    focus_controller->set_focus_to(window.session(), window);
}

auto miral::BasicWindowManager::window_at(geometry::Point cursor) const
-> Window
{
    auto surface_at = focus_controller->surface_at(cursor);
    return surface_at ? info_for(surface_at).window : Window{};
}

auto miral::BasicWindowManager::active_display()
-> geometry::Rectangle const
{
    geometry::Rectangle result;

    // 1. If a window has input focus, whichever display contains the largest
    //    proportion of the area of that window.
    if (auto const surface = focus_controller->focused_surface())
    {
        auto const surface_rect = surface->input_bounds();
        int max_overlap_area = -1;

        for (auto const& display : displays)
        {
            auto const intersection = surface_rect.intersection_with(display).size;
            if (intersection.width.as_int()*intersection.height.as_int() > max_overlap_area)
            {
                max_overlap_area = intersection.width.as_int()*intersection.height.as_int();
                result = display;
            }
        }
        return result;
    }

    // 2. Otherwise, if any window previously had input focus, for the window that had
    //    it most recently, the display that contained the largest proportion of the
    //    area of that window at the moment it closed, as long as that display is still
    //    available.

    // 3. Otherwise, the display that contains the pointer, if there is one.
    for (auto const& display : displays)
    {
        if (display.contains(cursor))
        {
            // Ignore the (unspecified) possiblity of overlapping displays
            return display;
        }
    }

    // 4. Otherwise, the primary display, if there is one (for example, the laptop display).

    // 5. Otherwise, the first display.
    if (displays.size())
        result = *displays.begin();

    return result;
}

void miral::BasicWindowManager::raise_tree(Window const& root)
{
    SurfaceSet windows;
    std::function<void(std::weak_ptr<scene::Surface> const& surface)> const add_children =
        [&,this](std::weak_ptr<scene::Surface> const& surface)
            {
            auto const& info = info_for(surface);
            windows.insert(begin(info.children), end(info.children));
            for (auto const& child : info.children)
                add_children(child);
            };

    windows.insert(root);
    add_children(root);

    focus_controller->raise(windows);
}

void miral::BasicWindowManager::update_event_timestamp(MirKeyboardEvent const* kev)
{
    auto iev = mir_keyboard_event_input_event(kev);
    last_input_event_timestamp = mir_input_event_get_event_time(iev);
}

void miral::BasicWindowManager::update_event_timestamp(MirPointerEvent const* pev)
{
    auto iev = mir_pointer_event_input_event(pev);
    auto pointer_action = mir_pointer_event_action(pev);

    if (pointer_action == mir_pointer_action_button_up ||
        pointer_action == mir_pointer_action_button_down)
    {
        last_input_event_timestamp = mir_input_event_get_event_time(iev);
    }
}

void miral::BasicWindowManager::update_event_timestamp(MirTouchEvent const* tev)
{
    auto iev = mir_touch_event_input_event(tev);
    auto touch_count = mir_touch_event_point_count(tev);
    for (unsigned i = 0; i < touch_count; i++)
    {
        auto touch_action = mir_touch_event_action(tev, i);
        if (touch_action == mir_touch_action_up ||
            touch_action == mir_touch_action_down)
        {
            last_input_event_timestamp = mir_input_event_get_event_time(iev);
            break;
        }
    }
}

void miral::BasicWindowManager::size_to_output(mir::geometry::Rectangle& rect)
{
    display_layout->size_to_output(rect);
}

bool miral::BasicWindowManager::place_in_output(int id, mir::geometry::Rectangle& rect)
{
    return display_layout->place_in_output(mir::graphics::DisplayConfigurationOutputId{id}, rect);
}
