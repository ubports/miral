/*
 * Copyright © 2016 Canonical Ltd.
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

#ifndef MIRAL_TEST_WINDOW_MANAGER_TOOLS_H
#define MIRAL_TEST_WINDOW_MANAGER_TOOLS_H

#include "../miral/basic_window_manager.h"

#include <miral/canonical_window_manager.h>

#include <mir/scene/surface_creation_parameters.h>
#include <mir/shell/display_layout.h>
#include <mir/shell/focus_controller.h>
#include <mir/shell/persistent_surface_store.h>

#include <mir/test/doubles/stub_session.h>
#include <mir/test/doubles/stub_surface.h>
#include <mir/test/fake_shared.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>

struct StubFocusController : mir::shell::FocusController
{
    void focus_next_session() override {}

    auto focused_session() const -> std::shared_ptr<mir::scene::Session> override { return {}; }

    void set_focus_to(
        std::shared_ptr<mir::scene::Session> const& /*focus_session*/,
        std::shared_ptr<mir::scene::Surface> const& /*focus_surface*/) override {}

    auto focused_surface() const -> std::shared_ptr<mir::scene::Surface> override { return {}; }

    void raise(mir::shell::SurfaceSet const& /*surfaces*/) override {}

    virtual auto surface_at(mir::geometry::Point /*cursor*/) const -> std::shared_ptr<mir::scene::Surface> override
        { return {}; }
};

struct StubDisplayLayout : mir::shell::DisplayLayout
{
    void clip_to_output(mir::geometry::Rectangle& /*rect*/) override {}

    void size_to_output(mir::geometry::Rectangle& /*rect*/) override {}

    bool place_in_output(mir::graphics::DisplayConfigurationOutputId /*id*/, mir::geometry::Rectangle& /*rect*/) override
        { return false; }
};

struct StubPersistentSurfaceStore : mir::shell::PersistentSurfaceStore
{
    Id id_for_surface(std::shared_ptr<mir::scene::Surface> const& /*surface*/) override { return {}; }

    auto surface_for_id(Id const& /*id*/) const -> std::shared_ptr<mir::scene::Surface> override { return {}; }
};

struct StubSurface : mir::test::doubles::StubSurface
{
    StubSurface(std::string name, MirSurfaceType type, mir::geometry::Point top_left, mir::geometry::Size size) :
        name_{name}, type_{type}, top_left_{top_left}, size_{size} {}

    std::string name() const override { return name_; };
    MirSurfaceType type() const { return type_; }

    mir::geometry::Point top_left() const override { return top_left_; }
    void move_to(mir::geometry::Point const& top_left) override { top_left_ = top_left; }

    mir::geometry::Size size() const override { return  size_; }
    void resize(mir::geometry::Size const& size) override { size_ = size; }

    auto state() const -> MirSurfaceState override { return state_; }
    auto configure(MirSurfaceAttrib attrib, int value) -> int override {
        switch (attrib)
        {
        case mir_surface_attrib_state:
            state_ = MirSurfaceState(value);
            return state_;
        default:
            return value;
        }
    }

    bool visible() const override { return  state() != mir_surface_state_hidden; }

    std::string name_;
    MirSurfaceType type_;
    mir::geometry::Point top_left_;
    mir::geometry::Size size_;
    MirSurfaceState state_ = mir_surface_state_restored;
};

struct StubStubSession : mir::test::doubles::StubSession
{
    mir::frontend::SurfaceId create_surface(
        mir::scene::SurfaceCreationParameters const& params,
        std::shared_ptr<mir::frontend::EventSink> const& /*sink*/) override
    {
        auto id = mir::frontend::SurfaceId{next_surface_id.fetch_add(1)};
        auto surface = std::make_shared<StubSurface>(params.name, params.type.value(), params.top_left, params.size);
        surfaces[id] = surface;
        return id;
    }

    std::shared_ptr<mir::scene::Surface> surface(mir::frontend::SurfaceId surface) const override
    {
        return surfaces.at(surface);
    }

private:
    std::atomic<int> next_surface_id;
    std::map<mir::frontend::SurfaceId, std::shared_ptr<mir::scene::Surface>> surfaces;
};

struct MockWindowManagerPolicy : miral::CanonicalWindowManagerPolicy
{
    using miral::CanonicalWindowManagerPolicy::CanonicalWindowManagerPolicy;

    bool handle_touch_event(MirTouchEvent const* /*event*/) override { return false; }
    bool handle_pointer_event(MirPointerEvent const* /*event*/) override { return false; }
    bool handle_keyboard_event(MirKeyboardEvent const* /*event*/) override { return false; }

    MOCK_METHOD1(advise_new_window, void (miral::WindowInfo const& window_info));
    MOCK_METHOD2(advise_move_to, void(miral::WindowInfo const& window_info, mir::geometry::Point top_left));
    MOCK_METHOD2(advise_resize, void(miral::WindowInfo const& window_info, mir::geometry::Size const& new_size));
};

struct TestWindowManagerTools : testing::Test
{
    StubFocusController focus_controller;
    StubDisplayLayout display_layout;
    StubPersistentSurfaceStore persistent_surface_store;
    std::shared_ptr<StubStubSession> session{std::make_shared<StubStubSession>()};

    MockWindowManagerPolicy* window_manager_policy{nullptr};
    miral::WindowManagerTools window_manager_tools{nullptr};

    miral::BasicWindowManager basic_window_manager{
        &focus_controller,
        mir::test::fake_shared(display_layout),
        mir::test::fake_shared(persistent_surface_store),
        [this](miral::WindowManagerTools const& tools) -> std::unique_ptr<miral::WindowManagementPolicy>
            {
                auto policy = std::make_unique<MockWindowManagerPolicy>(tools);
                window_manager_policy = policy.get();
                window_manager_tools = tools;
                return std::move(policy);
            }
    };

    static auto create_surface(
        std::shared_ptr<mir::scene::Session> const& session,
        mir::scene::SurfaceCreationParameters const& params) -> mir::frontend::SurfaceId
    {
        // This type is Mir-internal, I hope we don't need to create it here
        std::shared_ptr<mir::frontend::EventSink> const sink;
        return session->create_surface(params, sink);
    }
};

#endif //MIRAL_TEST_WINDOW_MANAGER_TOOLS_H