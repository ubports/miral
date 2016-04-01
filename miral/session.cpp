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

#include "miral/session.h"
#include "miral/surface.h"
#include "mir/al/basic_window_manager.h"

#include "mir/scene/session.h"

auto miral::Session::process_id() const
-> pid_t
{
    return scene_session.lock()->process_id();
}

auto miral::Session::default_surface() const
-> Surface
{
    return tools->info_for(scene_session.lock()->default_surface()).surface;
}

auto miral::Session::surface_after(Surface const& surface) const
-> Surface
{
    return tools->info_for(scene_session.lock()->surface_after(surface)).surface;
}

bool miral::operator==(Session const& lhs, Session const& rhs)
{
    return !lhs.scene_session.owner_before(rhs.scene_session) && !rhs.scene_session.owner_before(lhs.scene_session);
}

bool miral::operator==(std::shared_ptr<mir::scene::Session> const& lhs, Session const& rhs)
{
    return !lhs.owner_before(rhs.scene_session) && !rhs.scene_session.owner_before(lhs);
}

bool miral::operator==(Session const& lhs, std::shared_ptr<mir::scene::Session> const& rhs)
{
    return rhs == lhs;
}