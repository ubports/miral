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

#ifndef MIRAL_DEBUGEXTENSIONS_H
#define MIRAL_DEBUGEXTENSIONS_H

#include <memory>

namespace mir { class Server; }

namespace miral
{
/// Allow debug extension APIs to be enabled and disabled
class DebugExtension
{
public:
    DebugExtension();
    DebugExtension(DebugExtension const&);
    DebugExtension& operator=(DebugExtension const&);

    void enable();
    void disable();

    void operator()(mir::Server& server) const;

private:
    struct Self;
    std::shared_ptr<Self> self;
};
}

#endif //MIRAL_DEBUGEXTENSIONS_H
