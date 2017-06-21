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

#include "miral/active_outputs.h"
#include "miral/output.h"

#include <mir/version.h>
#include <mir/graphics/display_configuration.h>
#include <mir/server.h>

#include <mir/graphics/display_configuration_observer.h>
#include <mir/observer_registrar.h>

#include <algorithm>
#include <mutex>
#include <vector>

void miral::ActiveOutputsListener::advise_output_begin() {}
void miral::ActiveOutputsListener::advise_output_end() {}
void miral::ActiveOutputsListener::advise_output_create(Output const& /*output*/) {}
void miral::ActiveOutputsListener::advise_output_update(Output const& /*updated*/, Output const& /*original*/) {}
void miral::ActiveOutputsListener::advise_output_delete(Output const& /*output*/) {}

struct miral::ActiveOutputsMonitor::Self : mir::graphics::DisplayConfigurationObserver
{
    void initial_configuration(std::shared_ptr<mir::graphics::DisplayConfiguration const> const& configuration) override;
    void configuration_applied(std::shared_ptr<mir::graphics::DisplayConfiguration const> const& config) override;

    void configuration_failed(
        std::shared_ptr<mir::graphics::DisplayConfiguration const> const&,
        std::exception const&) override
    {
    }

    void catastrophic_configuration_error(
        std::shared_ptr<mir::graphics::DisplayConfiguration const> const&,
        std::exception const&) override
    {
    }

    void base_configuration_updated(std::shared_ptr<mir::graphics::DisplayConfiguration const> const& ) override {}

    void session_configuration_applied(std::shared_ptr<mir::frontend::Session> const&,
                                               std::shared_ptr<mir::graphics::DisplayConfiguration> const&) override {}

    void session_configuration_removed(std::shared_ptr<mir::frontend::Session> const&) override {}

    std::mutex mutex;
    std::vector<ActiveOutputsListener*> listeners;
    std::vector<Output> outputs;
};

miral::ActiveOutputsMonitor::ActiveOutputsMonitor() :
    self{std::make_shared<Self>()}
{
}

miral::ActiveOutputsMonitor::~ActiveOutputsMonitor() = default;
miral::ActiveOutputsMonitor::ActiveOutputsMonitor(ActiveOutputsMonitor const&) = default;
miral::ActiveOutputsMonitor& miral::ActiveOutputsMonitor::operator=(ActiveOutputsMonitor const&) = default;

void miral::ActiveOutputsMonitor::add_listener(ActiveOutputsListener* listener)
{
    std::lock_guard<decltype(self->mutex)> lock{self->mutex};

    self->listeners.push_back(listener);
}

void miral::ActiveOutputsMonitor::delete_listener(ActiveOutputsListener* listener)
{
    std::lock_guard<decltype(self->mutex)> lock{self->mutex};

    auto const new_end = std::remove(self->listeners.begin(), self->listeners.end(), listener);
    self->listeners.erase(new_end, self->listeners.end());
}

void miral::ActiveOutputsMonitor::operator()(mir::Server& server)
{
    std::lock_guard<decltype(self->mutex)> lock{self->mutex};

    server.add_pre_init_callback([this, &server]
        { server.the_display_configuration_observer_registrar()->register_interest(self); });
}

void miral::ActiveOutputsMonitor::process_outputs(
    std::function<void(std::vector<Output> const& outputs)> const& functor) const
{
    std::lock_guard<decltype(self->mutex)> lock{self->mutex};
    functor(self->outputs);
}


void miral::ActiveOutputsMonitor::Self::initial_configuration(std::shared_ptr<mir::graphics::DisplayConfiguration const> const& configuration)
{
    configuration_applied(configuration);
}

void miral::ActiveOutputsMonitor::Self::configuration_applied(std::shared_ptr<mir::graphics::DisplayConfiguration const> const& config)
{
    std::lock_guard<decltype(mutex)> lock{mutex};

    decltype(outputs) current_outputs;

    for (auto const l : listeners)
        l->advise_output_begin();

    config->for_each_output(
        [&current_outputs, this](mir::graphics::DisplayConfigurationOutput const& output)
            {
            Output o{output};

            if (!o.connected() || !o.valid()) return;

            auto op = find_if(
                begin(outputs), end(outputs), [&](Output const& oo)
                    { return oo.is_same_output(o); });

            if (op == end(outputs))
            {
                for (auto const l : listeners)
                    l->advise_output_create(o);
            }
            else if (!equivalent_display_area(o, *op))
            {
                for (auto const l : listeners)
                    l->advise_output_update(o, *op);
            }

            current_outputs.push_back(o);
            });

    for (auto const& o : outputs)
    {
        auto op = find_if(
            begin(current_outputs), end(current_outputs), [&](Output const& oo)
                { return oo.is_same_output(o); });

        if (op == end(current_outputs))
            for (auto const l : listeners)
                l->advise_output_delete(o);
    }

    current_outputs.swap(outputs);
    for (auto const l : listeners)
        l->advise_output_end();
}
