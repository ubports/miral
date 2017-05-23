/*
 * Copyright © 2013, 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "eglapp.h"

#include "miregl.h"


float mir_eglapp_background_opacity = 1.0f;


namespace
{
template<typename ActiveOutputHandler>
void for_each_active_output(
    MirConnection* const connection, ActiveOutputHandler const& handler)
{
    /* eglapps are interested in the screen size, so
    use mir_connection_create_display_config */
    MirDisplayConfig* display_config =
        mir_connection_create_display_configuration(connection);

    int const n = mir_display_config_get_num_outputs(display_config);

    for (int i = 0; i != n; ++i)
    {
        MirOutput const *const output = mir_display_config_get_output(display_config, i);
        if (mir_output_is_enabled(output) &&
            mir_output_get_connection_state(output) == mir_output_connection_state_connected &&
            mir_output_get_num_modes(output) &&
            mir_output_get_current_mode_index(output) < (size_t)mir_output_get_num_modes(output))
        {
            handler(output);
        }
    }
    mir_display_config_release(display_config);
}

MirPixelFormat select_pixel_format(MirConnection* connection)
{
    unsigned int format[mir_pixel_formats];
    unsigned int nformats;

    mir_connection_get_available_surface_formats(
        connection,
        (MirPixelFormat*) format,
        mir_pixel_formats,
        &nformats);

    auto const pixel_format = (MirPixelFormat) format[0];

    printf("Server supports %d of %d surface pixel formats. Using format: %d\n",
           nformats, mir_pixel_formats, pixel_format);

    return pixel_format;
}
}

std::vector<std::shared_ptr<MirEglSurface>> mir_eglapp_init(MirConnection* const connection)
{
    MirWindowParameters surfaceparm =
        {
            "eglappsurface",
            0, 0,
            mir_pixel_format_xbgr_8888,
            mir_buffer_usage_hardware,
            mir_display_output_id_invalid
        };

    EGLint swapinterval = 1;

    if (!mir_connection_is_valid(connection))
        throw std::runtime_error("Can't get connection");

    auto const pixel_format = select_pixel_format(connection);
    surfaceparm.pixel_format = pixel_format;

    auto const mir_egl_app = make_mir_eglapp(connection, pixel_format);

    std::vector<std::shared_ptr<MirEglSurface>> result;

    // If a size has been specified just do that
    if (surfaceparm.width && surfaceparm.height)
    {
        result.push_back(std::make_shared<MirEglSurface>(mir_egl_app, surfaceparm, swapinterval));
        return result;
    }

    // If an output has been specified just do that
    if (surfaceparm.output_id != mir_display_output_id_invalid)
    {
        result.push_back(std::make_shared<MirEglSurface>(mir_egl_app, surfaceparm, swapinterval));
        return result;
    }

    // but normally, we're fullscreen on every active output
    for_each_active_output(connection, [&](MirOutput const* output)
        {
            auto const& mode = mir_output_get_current_mode(output);

            printf("Active output [%u] at (%d, %d) is %dx%d\n",
                   mir_output_get_id(output),
                   mir_output_get_position_x(output), mir_output_get_position_y(output),
                   mir_output_mode_get_width(mode), mir_output_mode_get_height(mode));

            surfaceparm.output_id = mir_output_get_id(output);
            result.push_back(std::make_shared<MirEglSurface>(mir_egl_app, surfaceparm, swapinterval));
        });

    if (result.empty())
        throw std::runtime_error("No active outputs found.");

    return result;
}
