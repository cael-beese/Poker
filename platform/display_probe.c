/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* display_probe.c - find the display's native (preferred) mode before raylib
 * opens it.
 *
 * raylib's DRM backend, given InitWindow(0, 0), reuses whatever mode the CRTC
 * currently has, which is whatever EmulationStation or the last emulator left
 * behind, and fails outright when the CRTC has no mode. Asking the connector
 * for its preferred mode and passing that size to InitWindow makes the game
 * come up at the panel's native resolution (3440x1440 on the cabinet) every
 * time. The device and connector are chosen exactly the way raylib 5.5 chooses
 * them, so the probe and raylib agree on which output they mean. */
#include "platform/display_probe.h"

#include <stdio.h>
#include <string.h>

#if defined(BPL_PLATFORM_DRM)
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static int open_card(void)
{
    static const char *const paths[] = {
        "/dev/dri/by-path/platform-gpu-card", "/dev/dri/card1", "/dev/dri/card0"
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        int fd = open(paths[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        drmModeRes *res = drmModeGetResources(fd);
        if (res) {
            drmModeFreeResources(res);
            return fd;
        }
        close(fd);
    }
    return -1;
}

int display_probe_native(DisplayInfo *out)
{
    memset(out, 0, sizeof *out);
    int fd = open_card();
    if (fd < 0) return -1;

    int rc = -1;
    drmModeRes *res = drmModeGetResources(fd);
    for (int i = 0; res && i < res->count_connectors && rc != 0; i++) {
        drmModeConnector *con = drmModeGetConnector(fd, res->connectors[i]);
        if (!con) continue;
        if ((con->connection == DRM_MODE_CONNECTED || con->connection == DRM_MODE_UNKNOWNCONNECTION) &&
            con->encoder_id && con->count_modes > 0) {
            int pick = 0;
            for (int m = 0; m < con->count_modes; m++) {
                if (con->modes[m].type & DRM_MODE_TYPE_PREFERRED) { pick = m; break; }
            }
            out->width = con->modes[pick].hdisplay;
            out->height = con->modes[pick].vdisplay;
            out->refresh = (int)con->modes[pick].vrefresh;
            const char *type = drmModeGetConnectorTypeName(con->connector_type);
            snprintf(out->connector, sizeof out->connector, "%s-%u", type ? type : "?", con->connector_type_id);
            rc = 0;
        }
        drmModeFreeConnector(con);
    }
    if (res) drmModeFreeResources(res);
    close(fd);
    return rc;
}

#else

int display_probe_native(DisplayInfo *out)
{
    /* On the desktop the window size comes from the command line or config. */
    memset(out, 0, sizeof *out);
    return -1;
}

#endif
