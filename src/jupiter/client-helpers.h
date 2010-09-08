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

#ifndef __CLIENT_HELPERS_H__
#define __CLIENT_HELPERS_H__

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

void _client_create_tube_async (const gchar *account_path,
    const gchar *contact_id, GCancellable *cancellable,
    GAsyncReadyCallback callback, gpointer user_data);

GSocketConnection *_client_create_tube_finish (GAsyncResult *res,
    TpChannel **channel,
    GError **error);

GSocket * _client_create_local_socket (GError **error);

GStrv  _client_create_exec_args (GSocket *socket,
    const gchar *contact_id, const gchar *username);

gboolean _capabilities_has_stream_tube (TpCapabilities *caps);

G_END_DECLS

#endif /* #ifndef __CLIENT_HELPERS_H__*/
