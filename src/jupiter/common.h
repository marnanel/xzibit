/*
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

#define TUBE_SERVICE "x-xzibit"

G_BEGIN_DECLS

void _g_io_stream_splice_async (GIOStream *stream1, GIOStream *stream2,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean _g_io_stream_splice_finish (GAsyncResult *result, GError **error);

G_END_DECLS

#endif /* #ifndef __COMMON_H__*/
