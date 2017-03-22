/*
 * Copyright © 2017 Canonical Ltd.
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

#include <miral/window_management_policy_addendum2.h>

#include <mir/client/blob.h>
#include <mir/client/cookie.h>
#include <mir/client/window.h>
#include <mir/client/window_spec.h>
#include <mir_toolkit/mir_buffer_stream.h>
#include <mir_toolkit/extensions/drag_and_drop.h>

#include <mir/geometry/displacement.h>
#include <mir/input/input_device_info.h>
#include <mir/input/device_capability.h>
#include <mir/shell/shell.h>

#include "test_server.h"
#include <mir_test_framework/fake_input_device.h>
#include <mir_test_framework/stub_server_platform_factory.h>
#include <mir/test/event_factory.h>
#include <mir/test/signal.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <linux/input.h>
#include <uuid/uuid.h>

#include <boost/throw_exception.hpp>
#include <atomic>

using namespace std::chrono_literals;
using namespace mir::client;
using namespace mir::geometry;
using namespace testing;
using mir::test::Signal;

namespace
{
struct MouseMoverAndFaker
{
    void start_dragging_mouse()
    {
        using namespace mir::input::synthesis;
        fake_mouse->emit_event(a_button_down_event().of_button(BTN_LEFT));
    }

    void move_mouse(Displacement const& displacement)
    {
        using mir::input::synthesis::a_pointer_event;
        fake_mouse->emit_event(a_pointer_event().with_movement(displacement.dx.as_int(), displacement.dy.as_int()));
    }

    void release_mouse()
    {
        using namespace mir::input::synthesis;
        fake_mouse->emit_event(a_button_up_event().of_button(BTN_LEFT));
    }

private:
    std::unique_ptr<mir_test_framework::FakeInputDevice> fake_mouse{
        mir_test_framework::add_fake_input_device(
            mir::input::InputDeviceInfo{"mouse", "mouse-uid", mir::input::DeviceCapability::pointer})};
};

Rectangle const screen_geometry{{0,0}, {800,600}};
auto const receive_event_timeout = 1s; //90s;

struct ConnectedClientWithAWindow : miral::TestServer
{
    Connection connection;
    Window window;

    void SetUp() override
    {
        miral::TestServer::SetUp();
        connection = connect_client(__func__);
        window = WindowSpec::for_normal_window(connection, surface_size.width.as_int(), surface_size.height.as_int())
            .set_pixel_format(mir_pixel_format_abgr_8888)
            .set_name("ConnectedClientWithAWindow")
            .set_buffer_usage(mir_buffer_usage_hardware)
            .create_window();
    }

    void TearDown() override
    {
        window.reset();
        connection.reset();
        miral::TestServer::TearDown();
    }

    mir::geometry::Size const surface_size {640, 480};
};

struct DragAndDrop : ConnectedClientWithAWindow,
                     MouseMoverAndFaker
{
    MirDragAndDropV1 const* dnd = nullptr;

    void SetUp() override
    {
        mir_test_framework::set_next_display_rects(std::unique_ptr<std::vector<Rectangle>>(new std::vector<Rectangle>({screen_geometry})));

        ConnectedClientWithAWindow::SetUp();
        dnd = mir_drag_and_drop_v1(connection);
        mir_window_set_event_handler(window, &window_event_handler, this);
        if (dnd) dnd->set_start_drag_and_drop_callback(window, &window_dnd_start_handler, this);

        create_target_window();

        paint_window(window);

        center_mouse();
    }

    void TearDown() override
    {
        reset_window_event_handler(target_window);
        reset_window_event_handler(window);
        target_window.reset();
        another_connection.reset();
        ConnectedClientWithAWindow::TearDown();
    }

    auto user_initiates_drag() -> Cookie;
    auto client_requests_drag(Cookie const& cookie) -> Blob;
    auto handle_from_mouse_move() -> Blob;
    auto handle_from_mouse_leave() -> Blob;
    auto handle_from_mouse_enter() -> Blob;
    auto handle_from_mouse_release() -> Blob;
    auto count_of_handles_when_moving_mouse() -> int;

private:
    auto build_window_manager_policy(miral::WindowManagerTools const& tools) -> std::unique_ptr<TestWindowManagerPolicy> override;
    void center_mouse();
    void paint_window(MirWindow* w);
    void set_window_event_handler(MirWindow* window, std::function<void(MirEvent const* event)> const& handler);
    void set_window_dnd_start_handler(MirWindow* window, std::function<void(MirDragAndDropEvent const*)> const& handler);
    void reset_window_event_handler(MirWindow* window);

    void create_target_window()
    {
        another_connection = connect_client("another_connection");
        target_window = WindowSpec::
            for_normal_window(connection, screen_geometry.size.width.as_int(), screen_geometry.size.height.as_int())
            .set_pixel_format(mir_pixel_format_abgr_8888)
        .set_name("target_window")
        .set_buffer_usage(mir_buffer_usage_hardware)
        .set_event_handler(&window_event_handler, this)
        .create_window();

        paint_window(target_window);
    }

    void invoke_window_event_handler(MirWindow* window, MirEvent const* event)
    {
        std::lock_guard<decltype(window_event_handler_mutex)> lock{window_event_handler_mutex};
        if (window == this->window) window_event_handler_(event);
        if (window == target_window) target_window_event_handler_(event);
    }

    void invoke_window_dnd_start_handler(MirWindow* window, MirDragAndDropEvent const* event)
    {
        std::lock_guard<decltype(window_event_handler_mutex)> lock{window_event_handler_mutex};
        if (window == this->window) window_dnd_start_(event);
    }

    std::mutex window_event_handler_mutex;
    std::function<void(MirDragAndDropEvent const* event)> window_dnd_start_ = [](MirDragAndDropEvent const*) {};
    std::function<void(MirEvent const* event)> window_event_handler_ = [](MirEvent const*) {};
    std::function<void(MirEvent const* event)> target_window_event_handler_ = [](MirEvent const*) {};

    static void window_event_handler(MirWindow* window, MirEvent const* event, void* context);
    static void window_dnd_start_handler(MirWindow* window, MirDragAndDropEvent const* event, void* context);

    Connection another_connection;
    Window     target_window;
};

void DragAndDrop::set_window_event_handler(MirWindow* window, std::function<void(MirEvent const* event)> const& handler)
{
    std::lock_guard<decltype(window_event_handler_mutex)> lock{window_event_handler_mutex};
    if (window == this->window) window_event_handler_ = handler;
    if (window == target_window) target_window_event_handler_ = handler;
}

void DragAndDrop::set_window_dnd_start_handler(MirWindow* window, std::function<void(MirDragAndDropEvent const*)> const& handler)
{
std::lock_guard<decltype(window_event_handler_mutex)> lock{window_event_handler_mutex};
if (window == this->window) window_dnd_start_ = handler;
}


void DragAndDrop::reset_window_event_handler(MirWindow* window)
{
    if (window == this->window) window_event_handler_ = [](MirEvent const*) {};
    if (window == target_window) target_window_event_handler_ = [](MirEvent const*) {};
}

void DragAndDrop::paint_window(MirWindow* w)
{
    Signal have_focus;

    set_window_event_handler(w, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_window)
            return;

        auto const window_event = mir_event_get_window_event(event);
        if (mir_window_event_get_attribute(window_event) != mir_window_attrib_focus)
            return;

        if (mir_window_event_get_attribute_value(window_event))
            have_focus.raise();
        });

    mir_buffer_stream_swap_buffers_sync(mir_window_get_buffer_stream(w));

    EXPECT_THAT(have_focus.wait_for(receive_event_timeout), Eq(true));

    reset_window_event_handler(w);
}

void DragAndDrop::center_mouse()
{
    Signal have_mouseover;

    set_window_event_handler(window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        if (mir_pointer_event_action(pointer_event) != mir_pointer_action_enter)
            return;

        have_mouseover.raise();
        });

    move_mouse(0.5 * as_displacement(screen_geometry.size));

// We miss the "mouseover" occasionally (with valgrind and heavy stress about 1/20).
// But it isn't essential for the test and we've probably waited long enough
// for the mouse-down needed by the test to reach the window.
//    EXPECT_THAT(have_mouseover.wait_for(receive_event_timeout), Eq(true));
    have_mouseover.wait_for(receive_event_timeout);

    reset_window_event_handler(window);
}

void DragAndDrop::window_event_handler(MirWindow* window, MirEvent const* event, void* context)
{
    static_cast<DragAndDrop*>(context)->invoke_window_event_handler(window, event);
}

void DragAndDrop::window_dnd_start_handler(MirWindow* window, MirDragAndDropEvent const* event, void* context)
{
    static_cast<DragAndDrop*>(context)->invoke_window_dnd_start_handler(window, event);
}


auto DragAndDrop::user_initiates_drag() -> Cookie
{
    Cookie cookie;
    Signal have_cookie;

    set_window_event_handler(window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        if (mir_pointer_event_action(pointer_event) != mir_pointer_action_button_down)
            return;

        cookie = Cookie{mir_input_event_get_cookie(input_event)};
        have_cookie.raise();
        });

    start_dragging_mouse();

    EXPECT_THAT(have_cookie.wait_for(receive_event_timeout), Eq(true));

    reset_window_event_handler(window);
    return cookie;
}

auto DragAndDrop::client_requests_drag(Cookie const& cookie) -> Blob
{
    Blob blob;
    Signal initiated;

    set_window_dnd_start_handler(window, [&](MirDragAndDropEvent const* event)
        {
        if (dnd)
            blob.reset(dnd->start_drag_and_drop(event));

        if (blob)
            initiated.raise();
        });

    EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

    if (dnd)
        dnd->request_drag_and_drop(window, cookie);

    EXPECT_TRUE(initiated.wait_for(receive_event_timeout));

    reset_window_event_handler(window);
    return blob;
}

auto DragAndDrop::handle_from_mouse_move() -> Blob
{
    Blob blob;
    Signal have_blob;

    set_window_event_handler(window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

        if (dnd)
            blob.reset(dnd->pointer_drag_and_drop(pointer_event));

        if (blob)
            have_blob.raise();
        });

    move_mouse({1,1});

    EXPECT_TRUE(have_blob.wait_for(receive_event_timeout));

    reset_window_event_handler(window);
    return blob;
}

auto DragAndDrop::handle_from_mouse_leave() -> Blob
{
    Blob blob;
    Signal have_blob;

    set_window_event_handler(window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        if (mir_pointer_event_action(pointer_event) != mir_pointer_action_leave)
            return;

        EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

        if (dnd)
            blob.reset(dnd->pointer_drag_and_drop(pointer_event));

        if (blob)
            have_blob.raise();
        });

    move_mouse({1,1});
    move_mouse(0.5 * as_displacement(surface_size));

    EXPECT_TRUE(have_blob.wait_for(receive_event_timeout));

    reset_window_event_handler(window);
    return blob;
}

auto DragAndDrop::handle_from_mouse_enter() -> Blob
{
    Blob blob;
    Signal have_blob;

    set_window_event_handler(target_window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        if (mir_pointer_event_action(pointer_event) != mir_pointer_action_enter)
            return;

        EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

        if (dnd)
            blob.reset(dnd->pointer_drag_and_drop(pointer_event));

        if (blob)
            have_blob.raise();
        });

    move_mouse({1,1});
    move_mouse(0.5 * as_displacement(surface_size));

    EXPECT_TRUE(have_blob.wait_for(receive_event_timeout));

    reset_window_event_handler(target_window);
    return blob;
}

auto DragAndDrop::handle_from_mouse_release() -> Blob
{
    Blob blob;
    Signal have_blob;

    set_window_event_handler(target_window, [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        if (mir_pointer_event_action(pointer_event) != mir_pointer_action_button_up)
            return;

        EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

        if (dnd)
            blob.reset(dnd->pointer_drag_and_drop(pointer_event));

        if (blob)
            have_blob.raise();
        });

    move_mouse({1,1});
    move_mouse(0.5 * as_displacement(surface_size));
    release_mouse();

    EXPECT_TRUE(have_blob.wait_for(receive_event_timeout));

    reset_window_event_handler(target_window);
    return blob;
}

auto DragAndDrop::count_of_handles_when_moving_mouse() -> int
{
    Signal have_3_events;
    std::atomic<int> events{0};
    std::atomic<int> handles{0};

    auto counter = [&](MirEvent const* event)
        {
        if (mir_event_get_type(event) != mir_event_type_input)
            return;

        auto const input_event = mir_event_get_input_event(event);

        if (mir_input_event_get_type(input_event) != mir_input_event_type_pointer)
            return;

        auto const pointer_event = mir_input_event_get_pointer_event(input_event);

        EXPECT_THAT(dnd, Ne(nullptr)) << "No Drag and Drop extension";

        Blob blob;
        if (dnd)
            blob.reset(dnd->pointer_drag_and_drop(pointer_event));

        if (blob)
            handles.fetch_add(1);

        if (events.fetch_add(1) == 2)
            have_3_events.raise();
        };

    set_window_event_handler(window, counter);
    set_window_event_handler(target_window, counter);

    start_dragging_mouse();
    move_mouse({1,1});
    release_mouse();

    EXPECT_TRUE(have_3_events.wait_for(receive_event_timeout));

    reset_window_event_handler(window);
    reset_window_event_handler(target_window);
    return handles;
}

auto DragAndDrop::build_window_manager_policy(miral::WindowManagerTools const& tools) -> std::unique_ptr<TestWindowManagerPolicy>
{
    struct DnDWindowManagerPolicy : miral::TestServer::TestWindowManagerPolicy, miral::WindowManagementPolicyAddendum2
    {
        using miral::TestServer::TestWindowManagerPolicy::TestWindowManagerPolicy;

        void handle_request_drag_and_drop(miral::WindowInfo& window_info) override
        {
            uuid_t uuid;
            uuid_generate(uuid);
            std::vector<uint8_t> const handle{std::begin(uuid), std::end(uuid)};

            tools.start_drag_and_drop(window_info, handle);
        }
    };

    return std::make_unique<DnDWindowManagerPolicy>(tools, *this);
}

MATCHER_P(BlobContentEq, p, "")
{
    if (!arg || !p)
        return false;
    if (mir_blob_size(arg) != mir_blob_size(p))
        return false;
    return !memcmp(mir_blob_data(arg), mir_blob_data(p), mir_blob_size(p));
}
}

TEST_F(DragAndDrop, when_user_initiates_drag_client_receives_cookie)
{
    auto const cookie = user_initiates_drag();

    EXPECT_THAT(cookie, NotNull());
}

TEST_F(DragAndDrop, when_client_requests_drags_it_receives_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());

    auto const handle = client_requests_drag(cookie);

    EXPECT_THAT(handle, NotNull());
}

TEST_F(DragAndDrop, during_drag_when_user_moves_mouse_client_receives_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());
    auto const handle_from_request = client_requests_drag(cookie);

    auto const handle = handle_from_mouse_move();

    EXPECT_THAT(handle, NotNull());
    EXPECT_THAT(handle, BlobContentEq(handle_from_request));
}

TEST_F(DragAndDrop, when_drag_moves_from_window_leave_event_contains_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());
    auto const handle_from_request = client_requests_drag(cookie);

    auto const handle = handle_from_mouse_leave();

    EXPECT_THAT(handle, NotNull());
    EXPECT_THAT(handle, BlobContentEq(handle_from_request));
}

TEST_F(DragAndDrop, when_drag_enters_target_window_enter_event_contains_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());
    auto const handle_from_request = client_requests_drag(cookie);

    auto const handle = handle_from_mouse_enter();

    EXPECT_THAT(handle, NotNull());
    EXPECT_THAT(handle, BlobContentEq(handle_from_request));
}

TEST_F(DragAndDrop, when_drag_releases_target_window_release_event_contains_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());
    auto const handle_from_request = client_requests_drag(cookie);

    auto const handle = handle_from_mouse_release();

    EXPECT_THAT(handle, NotNull());
    EXPECT_THAT(handle, BlobContentEq(handle_from_request));
}

TEST_F(DragAndDrop, after_drag_finishes_pointer_events_no_longer_contain_handle)
{
    auto const cookie = user_initiates_drag();
    ASSERT_THAT(cookie, NotNull());
    client_requests_drag(cookie);
    handle_from_mouse_release();

    invoke_tools([](miral::WindowManagerTools& tools) { tools.end_drag_and_drop(); });

    EXPECT_THAT(count_of_handles_when_moving_mouse(), Eq(0));
}
