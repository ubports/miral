/*
 * Copyright (C) 2016 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "windowmodel.h"

#include "mirqtconversion.h"
#include <QDebug>

/*
 * WindowModel - tracks Mir Window Manager operations and duplicates the window stack
 * that Mir has created internally. Any changes to this model are emitted as change
 * signals to the Qt GUI thread which will effectively duplicate this model again.
 *
 * Use a window ID as a shared identifier between this Mir-side model and the Qt-side model
 */

using namespace qtmir;

WindowModel::WindowModel()
{
    qDebug("WindowModel::WindowModel");
    qRegisterMetaType<qtmir::NumberedWindow>();
    qRegisterMetaType<qtmir::DirtiedWindow>();
}

WindowModel::~WindowModel()
{

}

void WindowModel::addWindow(const miral::WindowInfo &windowInfo)
{
    qDebug("WindowModel::addWindow");
    auto stackPosition = static_cast<unsigned int>(m_windowIdStack.count());
    m_windowIdStack.push_back(windowInfo.window().surface_id()); // ASSUMPTION: Mir should tell us where in stack

    QSize size = toQSize(windowInfo.window().size());
    QPoint position = toQPoint(windowInfo.window().top_left());

    WindowInfo info{ size, position, false, windowInfo.window() };
    NumberedWindow window{ stackPosition, info };
    Q_EMIT windowAdded(window);
}

void WindowModel::removeWindow(const miral::WindowInfo &windowInfo)
{
    qDebug("WindowModel::removeWindow");
    const int pos = m_windowIdStack.indexOf(windowInfo.window().surface_id());
    if (pos < 0) {
        qDebug("Unknown window removed");
        return;
    }
    m_windowIdStack.removeAt(pos);
    auto upos = static_cast<unsigned int>(pos);
    Q_EMIT windowRemoved(upos);
}

void WindowModel::focusWindow(const miral::WindowInfo &windowInfo, const bool focus)
{
    const int pos = m_windowIdStack.indexOf(windowInfo.window().surface_id());
    if (pos < 0) {
        qDebug("Unknown window focused");
        return;
    }
    auto upos = static_cast<unsigned int>(pos);
    m_focusedWindowIndex = upos;
    QSize size = toQSize(windowInfo.window().size());
    QPoint position = toQPoint(windowInfo.window().top_left());

    WindowInfo info{ size, position, focus, windowInfo.window() };
    DirtiedWindow window{ upos, info, WindowInfo::DirtyStates::Focus};
    Q_EMIT windowChanged(window);
}

void WindowModel::moveWindow(const miral::WindowInfo &windowInfo, mir::geometry::Point topLeft)
{
    const int pos = m_windowIdStack.indexOf(windowInfo.window().surface_id());
    if (pos < 0) {
        qDebug("Unknown window moved");
        return;
    }
    auto upos = static_cast<unsigned int>(pos);
    const bool focused = (m_focusedWindowIndex == upos);
    QSize size = toQSize(windowInfo.window().size());
    QPoint position = toQPoint(topLeft);

    WindowInfo info{ size, position, focused, windowInfo.window() };
    DirtiedWindow window{ upos, info, WindowInfo::DirtyStates::Position};
    Q_EMIT windowChanged(window);
}

void WindowModel::resizeWindow(const miral::WindowInfo &windowInfo, mir::geometry::Size newSize)
{
    const int pos = m_windowIdStack.indexOf(windowInfo.window().surface_id());
    if (pos < 0) {
        qDebug("Unknown window resized");
        return;
    }
    auto upos = static_cast<unsigned int>(pos);
    const bool focused = (m_focusedWindowIndex == upos);
    QSize size = toQSize(newSize);
    QPoint position = toQPoint(windowInfo.window().top_left());

    WindowInfo info{ size, position, focused, windowInfo.window() };
    DirtiedWindow window{ upos, info, WindowInfo::DirtyStates::Size};
    Q_EMIT windowChanged(window);
}

void WindowModel::raiseWindows(const std::vector<miral::Window> &/*windows*/)
{

}