#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>
#include <telepathy-glib/telepathy-glib.h>

#include "list-contacts.h"

typedef struct _ClientContext {
  guint n_readying_connections;
  list_contacts_cb *callback;
  gchar *wanted_service;
  GList *accounts;
} ClientContext;

typedef struct _AccountFindingContacts {
  gchar *source_account;
  ClientContext *context;
} AccountFindingContacts;

static gboolean
_capabilities_has_stream_tube (TpCapabilities *caps,
			       gchar *wanted_service)
{
  GPtrArray *classes;
  guint i;

  if (caps == NULL)
    return FALSE;

  classes = tp_capabilities_get_channel_classes (caps);
  for (i = 0; i < classes->len; i++)
    {
      GValueArray *arr = g_ptr_array_index (classes, i);
      GHashTable *fixed;
      const gchar *chan_type;
      const gchar *service;
      TpHandleType handle_type;

      fixed = g_value_get_boxed (g_value_array_get_nth (arr, 0));
      chan_type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);
      service = tp_asv_get_string (fixed,
          TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE);
      handle_type = tp_asv_get_uint32 (fixed,
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

      if (!tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAM_TUBE) &&
          handle_type == TP_HANDLE_TYPE_CONTACT &&
          (!tp_capabilities_is_specific_to_contact (caps) ||
	   !wanted_service ||
           !tp_strdiff (service, wanted_service)))
        return TRUE;
    }

  return FALSE;
}

static void
got_contacts_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  AccountFindingContacts *afc = user_data;
  guint i;
  GList *candidates = NULL, *l;
  guint count = 0;
  gchar buffer[10];
  gchar *str;

  if (error != NULL)
    {
      g_warning ("Error: %s\n", error->message);
      /* not freeing because it's const */
      return;
    }

  /* Build a list of all contacts supporting StreamTube */
  for (i = 0; i < n_contacts; i++)
    {
      if (_capabilities_has_stream_tube (tp_contact_get_capabilities (contacts[i]),
					 afc->context->wanted_service))
	candidates = g_list_prepend (candidates, contacts[i]);
    }

  if (candidates == NULL)
    {
      g_warning ("No suitable contact\n");
      return;
    }

  for (l = candidates; l != NULL; l = l->next)
    {
      TpContact *contact = l->data;

      afc->context->callback (afc->source_account,
			      tp_contact_get_identifier (contact));
    }

  g_list_free (candidates);

  /* FIXME: and free afc */
}

static void
stored_channel_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  AccountFindingContacts *afc = user_data;
  TpChannel *channel = TP_CHANNEL (object);
  TpConnection *connection;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_CAPABILITIES };
  const TpIntSet *set;
  GArray *handles;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (channel, res, &error))
    {
      g_warning ("Error finishing proxy: %s",
		 error->message);
      g_clear_error (&error);
      return;
    }

  connection = tp_channel_borrow_connection (channel);
  set = tp_channel_group_get_members (channel);
  handles = tp_intset_to_array (set);

  tp_connection_get_contacts_by_handle (connection, handles->len,
      (TpHandle *) handles->data, G_N_ELEMENTS (features), features,
      got_contacts_cb, afc, NULL, NULL);

  g_array_unref (handles);
}

static void
ensure_stored_channel_cb (TpConnection *connection,
    gboolean yours,
    const gchar *channel_path,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  AccountFindingContacts *afc = user_data;
  TpChannel *channel;
  GQuark features[] = { TP_CHANNEL_FEATURE_GROUP, 0 };
  GError *err = NULL;

  if (error != NULL)
    {
      g_warning ("Error ensuring stored channel: %s",
		 err->message);
      g_clear_error (&err);
      return;
    }

  channel = tp_channel_new_from_properties (connection, channel_path,
      properties, &err);
  if (channel == NULL)
    {
      g_warning ("Error creating channel: %s",
		 err->message);
      g_clear_error (&err);
      return;
    }

  tp_proxy_prepare_async (TP_PROXY (channel), features,
      stored_channel_prepare_cb, afc);

  g_object_unref (channel);
}

static void
connection_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  ClientContext *context = user_data;

  if (!tp_proxy_prepare_finish (TP_PROXY (connection), res, NULL) ||
      !_capabilities_has_stream_tube (tp_connection_get_capabilities (connection),
				      context->wanted_service))
    {
      GList *l;

      /* Remove the account that has that connection from the list */
      for (l = context->accounts; l != NULL; l = l->next)
        if (tp_account_get_connection (l->data) == connection)
          {
            g_object_unref (l->data);
            context->accounts = g_list_delete_link (context->accounts, l);
            break;
          }
    }

  /* Are we done? */
  if (--context->n_readying_connections == 0)
    {

      /*
       * Yes.  Go through all the accounts and find
       * their contacts.
       */

      GList *cursor = context->accounts;
      TpConnection *associated_connection;
      GHashTable *request;
      AccountFindingContacts *afc;

      while (cursor)
	{
	  afc = g_malloc (sizeof (AccountFindingContacts));
	  afc->source_account = g_strdup (tp_proxy_get_object_path (cursor->data));
	  afc->context = context;

	  request = tp_asv_new (
				TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
				TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
				TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
				TP_HANDLE_TYPE_LIST,
				TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
				"stored",
				NULL);

	  connection = tp_account_get_connection (cursor->data);

	  tp_cli_connection_interface_requests_call_ensure_channel (connection, -1,
								    request,
								    ensure_stored_channel_cb,
								    afc, NULL, NULL);

	  g_hash_table_unref (request);

	  cursor = cursor->next;
	}

      /* FIXME: and free stuff */
    }
}


static void
account_manager_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (object);
  ClientContext *context = user_data;
  GList *l, *next;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (TP_PROXY (manager), res, &error))
    {
      g_warning ("Cannot prepare account manager: %s",
		 error->message);
      g_clear_error (&error);
      return;
    }

  /* We want to list all accounts which has a connection that have StreamTube
   * support. So first we prepare all connections, and we keep in
   * context->accounts those that are suitable. */

  context->accounts = tp_account_manager_get_valid_accounts (manager);
  g_list_foreach (context->accounts, (GFunc) g_object_ref, NULL);

  for (l = context->accounts; l != NULL; l = next)
    {
      GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };
      TpAccount *account = l->data;
      TpConnection *connection;

      next = l->next;

      connection = tp_account_get_connection (account);
      if (connection == NULL)
        {
          g_object_unref (account);
          context->accounts = g_list_delete_link (context->accounts,
						  l);
          continue;
        }

      context->n_readying_connections++;
      tp_proxy_prepare_async (TP_PROXY (connection), features,
          connection_prepare_cb, context);
    }

  if (context->n_readying_connections == 0)
    {
      g_warning ("There were no connections to call");
    }
}

void
list_contacts (list_contacts_cb *callback,
	       gchar *wanted_service)
{
  TpDBusDaemon *dbus = NULL;
  GError *error = NULL;
  TpAccountManager *manager;
  ClientContext *context;

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus==NULL)
    {
      g_warning ("Couldn't get a handle on DBus: %s",
		 error->message);
      g_clear_error (&error);
      return;
    }

  context = g_malloc (sizeof (ClientContext));
  context->n_readying_connections = 0;
  context->callback = callback;
  context->wanted_service = wanted_service;

  manager = tp_account_manager_new (dbus);
  tp_proxy_prepare_async (TP_PROXY (manager), NULL,
			  account_manager_prepare_cb, context);
  
  g_object_unref (manager);

}

void
dumper (const gchar *source, const gchar *target)
{
  g_warning ("Mapping: %s -> %s",
	     source, target);
}

int
main(int argc, char **argv)
{
  GMainLoop *loop;

  g_type_init ();

  loop = g_main_loop_new (NULL, FALSE);

  list_contacts (dumper,
		 "x-xzibit");

  g_main_loop_run (loop);
}
