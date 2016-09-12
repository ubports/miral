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

#include "qtmir_test.h"

#include <QLoggingCategory>
#include <QTest>
#include <QSignalSpy>

#include "mirqtconversion.h"
#include "windowmodelnotifier.h"
#include "Unity/Application/mirsurface.h"
#include "Unity/Application/windowmodel.h"

#include <mir/test/doubles/stub_surface.h>
#include <mir/test/doubles/stub_session.h>

#include <mir/scene/surface_creation_parameters.h>

using namespace qtmir;

namespace ms = mir::scene;
namespace mg = mir::graphics;
using StubSession = mir::test::doubles::StubSession;
using namespace testing;

struct SizedStubSurface : public mir::test::doubles::StubSurface
{
    mir::geometry::Size size() const override { return toMirSize(m_size); }

    void setSize(QSize size) { m_size = size; }

private:
    QSize m_size;
};


class WindowModelTest : public ::testing::Test
{
public:
    WindowModelTest()
    {
        // We don't want the logging spam cluttering the test results
        QLoggingCategory::setFilterRules(QStringLiteral("qtmir.surfaces=false"));
    }

    NewWindow createNewWindow(QPoint position = {160, 320}, QSize size = {100, 200})
    {
        const miral::Application app{stubSession};
        stubSurface->setSize(size);
        const miral::Window window{app, stubSurface};

        ms::SurfaceCreationParameters windowSpec;
//        windowSpec.of_size(toMirSize(size)); // useless, Window/Surface has the size actually used
        windowSpec.of_position(toMirPoint(position));
        miral::WindowInfo windowInfo{window, windowSpec};
        return NewWindow{windowInfo, ""};
    }

    NewWindow createNewWindowForInputMethod()
    {
        const miral::Application app{stubSession};
        const miral::Window window{app, stubSurface};

        ms::SurfaceCreationParameters windowSpec;
        windowSpec.of_type(mir_surface_type_inputmethod);
        miral::WindowInfo windowInfo{window, windowSpec};
        return NewWindow{windowInfo, ""};
    }

    MirSurface *getMirSurfaceFromModel(const WindowModel &model, int index)
    {
        flushEvents();
        return model.data(model.index(index, 0), WindowModel::SurfaceRole).value<MirSurface*>();
    }

    miral::Window getMirALWindowFromModel(const WindowModel &model, int index)
    {
        return getMirSurfaceFromModel(model, index)->window();
    }

    void SetUp() override
    {
        int argc = 0;
        char* argv[0];
        qtApp = new QCoreApplication(argc, argv); // needed for event loop
    }

    void TearDown() override
    {
        delete qtApp;
    }

    void flushEvents()
    {
        qtApp->sendPostedEvents();
    }

    const std::shared_ptr<StubSession> stubSession{std::make_shared<StubSession>()};
    const std::shared_ptr<SizedStubSurface> stubSurface{std::make_shared<SizedStubSurface>()};
    QCoreApplication *qtApp;
};

/*
 * Test: that the WindowModelNotifier.windowAdded causes the Qt-side WindowModel to
 * increment model count
 */
TEST_F(WindowModelTest, WhenAddWindowNotifiedModelCountIncrements)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindow();

    notifier.windowAdded(newWindow);
    flushEvents();

    EXPECT_EQ(1, model.count());
}

/*
 * Test: that the WindowModelNotifier.windowAdded causes the Qt-side WindowModel to
 * emit the countChanged signal.
 */
TEST_F(WindowModelTest, WhenAddWindowNotifiedModelEmitsCountChangedSignal)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindow();

    QSignalSpy spyCountChanged(&model, SIGNAL(countChanged()));

    notifier.windowAdded(newWindow);
    flushEvents();

    EXPECT_EQ(1, spyCountChanged.count());
}

/*
 * Test: that the WindowModelNotifier.windowAdded causes the Qt-side WindowModel to
 * gain an entry which has the correct miral::Window
 */
TEST_F(WindowModelTest, WhenAddWindowNotifiedNewModelEntryHasCorrectWindow)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindow();

    notifier.windowAdded(newWindow);
    flushEvents();

    auto miralWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow.windowInfo.window(), miralWindow);
}

/*
 * Test: that the WindowModelNotifier.windowRemoved causes the Qt-side WindowModel to
 * remove the Window from the model, and emit the countChanged signal.
 */
TEST_F(WindowModelTest, WhenRemoveWindowNotifiedModelCountDecrements)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindow();
    notifier.windowAdded(newWindow);

    // Test removing the window
    notifier.windowRemoved(newWindow.windowInfo);
    flushEvents();

    EXPECT_EQ(0, model.count());
}

/*
 * Test: that the WindowModelNotifier.windowRemoved causes the Qt-side WindowModel to
 * emit the countChanged signal.
 */
TEST_F(WindowModelTest, WhenRemoveWindowNotifiedModelEmitsCountChangedSignal)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindow();
    notifier.windowAdded(newWindow);
    flushEvents();

    // Test removing the window
    QSignalSpy spyCountChanged(&model, SIGNAL(countChanged()));

    notifier.windowRemoved(newWindow.windowInfo);
    flushEvents();

    EXPECT_EQ(1, spyCountChanged.count());
}

/*
 * Test: that calling WindowModelNotifier.windowAdded causes Qt-side WindowModel to
 * have 2 windows in the correct order.
 */
TEST_F(WindowModelTest, WhenAddingTwoWindowsModelHasCorrectOrder)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();

    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);
    flushEvents();

    ASSERT_EQ(2, model.count());
    auto miralWindow1 = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow1.windowInfo.window(), miralWindow1);
    auto miralWindow2 = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow2.windowInfo.window(), miralWindow2);
}

/*
 * Test: that adding 2 windows, then removing the second, leaves the first.
 */
TEST_F(WindowModelTest, WhenAddingTwoWindowsAndRemoveSecondModelPreservesFirst)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();

    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    // Remove second window
    notifier.windowRemoved(newWindow2.windowInfo);
    flushEvents();

    ASSERT_EQ(1, model.count());
    auto miralWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow1.windowInfo.window(), miralWindow);
}

/*
 * Test: that adding 2 windows, then removing the first, leaves the second.
 */
TEST_F(WindowModelTest, WhenAddingTwoWindowsAndRemoveFirstModelPreservesSecond)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();

    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    // Remove first window
    notifier.windowRemoved(newWindow1.windowInfo);
    flushEvents();

    ASSERT_EQ(1, model.count());
    auto miralWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow2.windowInfo.window(), miralWindow);
}

/*
 * Test: add 2 windows, remove first, add another window - ensure model order correct
 */
TEST_F(WindowModelTest, WhenAddingTwoWindowsRemoveFirstAddAnotherResultsInCorrectModel)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    auto newWindow3 = createNewWindow();

    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);
    notifier.windowRemoved(newWindow1.windowInfo);

    notifier.windowAdded(newWindow3);
    flushEvents();

    ASSERT_EQ(2, model.count());
    auto miralWindow2 = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow2.windowInfo.window(), miralWindow2);
    auto miralWindow3 = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow3.windowInfo.window(), miralWindow3);
}

/*
 * Test: add 3 windows, remove second - ensure model order correct
 */
TEST_F(WindowModelTest, WhenAddingThreeWindowsRemoveSecondResultsInCorrectModel)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    auto newWindow3 = createNewWindow();

    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);
    notifier.windowAdded(newWindow3);

    notifier.windowRemoved(newWindow2.windowInfo);
    flushEvents();

    ASSERT_EQ(2, model.count());
    auto miralWindow1 = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow1.windowInfo.window(), miralWindow1);
    auto miralWindow3 = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow3.windowInfo.window(), miralWindow3);
}

/*
 * Test: with 1 window, raise does nothing
 */
TEST_F(WindowModelTest, RaisingOneWindowDoesNothing)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    notifier.windowAdded(newWindow1);

    // Raise first window
    notifier.windowsRaised({newWindow1.windowInfo.window()});
    flushEvents();

    ASSERT_EQ(1, model.count());
    auto topWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow1.windowInfo.window(), topWindow);
}

/*
 * Test: with 2 window, raising top window does nothing
 */
TEST_F(WindowModelTest, RaisingTopWindowDoesNothing)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    // Raise second window (currently on top)
    notifier.windowsRaised({newWindow2.windowInfo.window()});
    flushEvents();

    // Check second window still on top
    ASSERT_EQ(2, model.count());
    auto topWindow = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow2.windowInfo.window(), topWindow);
}

/*
 * Test: with 2 window, raising bottom window brings it to the top
 */
TEST_F(WindowModelTest, RaisingBottomWindowBringsItToTheTop)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    // Raise first window (currently at bottom)
    notifier.windowsRaised({newWindow1.windowInfo.window()});
    flushEvents();

    // Check first window now on top
    ASSERT_EQ(2, model.count());
    auto topWindow = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow1.windowInfo.window(), topWindow);
}

/*
 * Test: with 3 windows, raising bottom 2 windows brings them to the top in order
 */
TEST_F(WindowModelTest, Raising2BottomWindowsBringsThemToTheTop)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    auto newWindow3 = createNewWindow();
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);
    notifier.windowAdded(newWindow3);

    // Current model state
    // 2:   Window3
    // 1:   Window2
    // 0:   Window1

    // Raise windows 1 & 2 (currently at bottom)
    notifier.windowsRaised({newWindow1.windowInfo.window(), newWindow2.windowInfo.window()});

    // Model should now be like this:
    // 2:   Window1
    // 1:   Window2
    // 0:   Window3
    flushEvents();

    ASSERT_EQ(3, model.count());
    auto topWindow = getMirALWindowFromModel(model, 2);
    EXPECT_EQ(newWindow1.windowInfo.window(), topWindow);
    auto middleWindow = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow2.windowInfo.window(), middleWindow);
    auto bottomWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow3.windowInfo.window(), bottomWindow);
}

/*
 * Test: with 2 window, raise the 2 windows in swapped order reorders the model
 */
TEST_F(WindowModelTest, Raising2WindowsInSwappedOrderReordersTheModel)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    // Current model state
    // 1:   Window2
    // 0:   Window1

    // Raise windows 1 & 2 (in opposite order)
    notifier.windowsRaised({newWindow1.windowInfo.window(), newWindow2.windowInfo.window()});

    // Model should now be like this:
    // 1:   Window1
    // 0:   Window2
    flushEvents();

    ASSERT_EQ(2, model.count());
    auto topWindow = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow1.windowInfo.window(), topWindow);
    auto bottomWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow2.windowInfo.window(), bottomWindow);
}

/*
 * Test: with 3 windows, raise the bottom 2 windows in swapped order reorders the model
 * so that the bottom window is at the top, and middle window remains in place.
 */
TEST_F(WindowModelTest, With3WindowsRaising2BottomWindowsInSwappedOrderReordersTheModel)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow1 = createNewWindow();
    auto newWindow2 = createNewWindow();
    auto newWindow3 = createNewWindow();
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);
    notifier.windowAdded(newWindow3);

    // Current model state
    // 2:   Window3
    // 1:   Window2
    // 0:   Window1

    // Raise windows 2 & 1 (i.e. bottom two, but in opposite order)
    notifier.windowsRaised({newWindow2.windowInfo.window(), newWindow1.windowInfo.window()});

    // Model should now be like this:
    // 2:   Window2
    // 1:   Window1
    // 0:   Window3
    flushEvents();

    ASSERT_EQ(3, model.count());
    auto topWindow = getMirALWindowFromModel(model, 2);
    EXPECT_EQ(newWindow2.windowInfo.window(), topWindow);
    auto middleWindow = getMirALWindowFromModel(model, 1);
    EXPECT_EQ(newWindow1.windowInfo.window(), middleWindow);
    auto bottomWindow = getMirALWindowFromModel(model, 0);
    EXPECT_EQ(newWindow3.windowInfo.window(), bottomWindow);
}

/*
 * Test: MirSurface has inital position set correctly from miral::WindowInfo
 */
TEST_F(WindowModelTest, DISABLED_MirSurfacePositionSetCorrectlyAtCreation)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QPoint position(100, 200);

    auto newWindow = createNewWindow(position);
    notifier.windowAdded(newWindow);
    flushEvents();

    auto surface = getMirSurfaceFromModel(model, 0);
    EXPECT_EQ(position, surface->position());
}

/*
 * Test: Mir moving a window updates MirSurface position
 */
TEST_F(WindowModelTest, WindowMoveUpdatesMirSurface)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QPoint oldPosition(100, 200),
           newPosition(150, 220);

    auto newWindow = createNewWindow(oldPosition);
    notifier.windowAdded(newWindow);

    auto surface = getMirSurfaceFromModel(model, 0);

    // Move window, check new position set
    notifier.windowMoved(newWindow.windowInfo, newPosition);
    flushEvents();

    EXPECT_EQ(newPosition, surface->position());
}

/*
 * Test: with 2 windows, ensure window move impacts the correct MirSurface
 */
TEST_F(WindowModelTest, WindowMoveUpdatesCorrectMirSurface)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QPoint oldPosition(100, 200),
           newPosition(150, 220);

    auto newWindow1 = createNewWindow(oldPosition);
    auto newWindow2 = createNewWindow(QPoint(300, 400));
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    auto surface = getMirSurfaceFromModel(model, 0); // will be MirSurface for newWindow1

    // Move window, check new position set
    notifier.windowMoved(newWindow1.windowInfo, newPosition);
    flushEvents();

    EXPECT_EQ(newPosition, surface->position());
}

/*
 * Test: with 2 windows, ensure window move does not impact other MirSurfaces
 */
TEST_F(WindowModelTest, DISABLED_WindowMoveDoesNotTouchOtherMirSurfaces)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QPoint fixedPosition(300, 400);

    auto newWindow1 = createNewWindow(QPoint(100, 200));
    auto newWindow2 = createNewWindow(fixedPosition);
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    auto surface = getMirSurfaceFromModel(model, 1); // will be MirSurface for newWindow2

    // Move window, check new position set
    notifier.windowMoved(newWindow1.windowInfo, QPoint(350, 420));
    flushEvents();

    // Ensure other window untouched
    EXPECT_EQ(fixedPosition, surface->position());
}

/*
 * Test: MirSurface has inital size set correctly from miral::WindowInfo
 */
TEST_F(WindowModelTest, DISABLED_MirSurfaceSizeSetCorrectlyAtCreation)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QSize size(300, 200);

    auto newWindow1 = createNewWindow(QPoint(), size);
    notifier.windowAdded(newWindow1);
    flushEvents();

    auto surface = getMirSurfaceFromModel(model, 0);
    EXPECT_EQ(size, surface->size());
}

/*
 * Test: Mir resizing a window updates MirSurface size
 */
TEST_F(WindowModelTest, WindowResizeUpdatesMirSurface)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QSize newSize(150, 220);

    auto newWindow1 = createNewWindow(QPoint(), QSize(300, 200));
    notifier.windowAdded(newWindow1);

    auto surface = getMirSurfaceFromModel(model, 0);

    // Resize window, check new size set
    notifier.windowResized(newWindow1.windowInfo, newSize);
    flushEvents();

    EXPECT_EQ(newSize, surface->size());
}

/*
 * Test: with 2 windows, ensure window resize impacts the correct MirSurface
 */
TEST_F(WindowModelTest, WindowResizeUpdatesCorrectMirSurface)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QSize newSize(150, 220);

    auto newWindow1 = createNewWindow(QPoint(), QSize(100, 200));
    auto newWindow2 = createNewWindow(QPoint(), QSize(300, 400));
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    auto surface = getMirSurfaceFromModel(model, 0);

    // Resize window, check new size set
    notifier.windowResized(newWindow1.windowInfo, newSize);
    flushEvents();

    EXPECT_EQ(newSize, surface->size());
}

/*
 * Test: with 2 windows, ensure window resize does not impact other MirSurfaces
 */
TEST_F(WindowModelTest, WindowResizeDoesNotTouchOtherMirSurfaces)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    QSize fixedSize(300, 400);

    auto newWindow1 = createNewWindow(QPoint(), QSize(100, 200));
    auto newWindow2 = createNewWindow(QPoint(), fixedSize);
    notifier.windowAdded(newWindow1);
    notifier.windowAdded(newWindow2);

    auto surface = getMirSurfaceFromModel(model, 1);

    // Resize window
    notifier.windowResized(newWindow1.windowInfo, QSize(150, 220));
    flushEvents();

    // Ensure other window untouched
    EXPECT_EQ(fixedSize, surface->size());
}

/*
 * Test: that the WindowModelNotifier.windowAdded for an Input Method Window causes
 * the Qt-side WindowModel to register the input method surface
 */
TEST_F(WindowModelTest, WhenAddInputMethodWindowNotifiedModelEmitsInputMethodChangedSignal)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindowForInputMethod();

    QSignalSpy spyCountChanged(&model, SIGNAL(inputMethodSurfaceChanged(MirSurfaceInterface*)));

    notifier.windowAdded(newWindow);
    flushEvents();

    EXPECT_EQ(1, spyCountChanged.count());
}

/*
 * Test: that the WindowModelNotifier.windowAdded for an Input Method Window causes
 * the Qt-side WindowModel::inputMethodSurface property to be correctly set
 */
TEST_F(WindowModelTest, WhenAddInputMethodWindowNotifiedModelPropertyHasCorrectWindow)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindowForInputMethod();

    notifier.windowAdded(newWindow);
    flushEvents();

    auto miralWindow = static_cast<MirSurface*>(model.inputMethodSurface())->window();
    EXPECT_EQ(newWindow.windowInfo.window(), miralWindow);
}

/*
 * Test: that the WindowModelNotifier.windowRemoved for an Input Method Window causes
 * the Qt-side WindowModel to reset the WindowModel::inputMethodSurface property to null
 */
TEST_F(WindowModelTest, WhenRemoveInputMethodWindowNotifiedModelPropertyReset)
{
    WindowModelNotifier notifier;
    WindowModel model(&notifier, nullptr); // no need for controller in this testcase

    auto newWindow = createNewWindowForInputMethod();
    notifier.windowAdded(newWindow);

    // Test removing the window
    notifier.windowRemoved(newWindow.windowInfo);
    flushEvents();

    EXPECT_EQ(nullptr, model.inputMethodSurface());
}
