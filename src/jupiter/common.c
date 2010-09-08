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

#include "common.h"

typedef struct
{
  GIOStream *stream1;
  GIOStream *stream2;
  GCancellable *op1_cancellable;
  GCancellable *op2_cancellable;
  gboolean completed:1;
} SpliceContext;

static void
splice_context_free (SpliceContext *ctx)
{
  g_object_unref (ctx->stream1);
  g_object_unref (ctx->stream2);
  g_object_unref (ctx->op1_cancellable);
  g_object_unref (ctx->op2_cancellable);
  g_slice_free (SpliceContext, ctx);
}

static void
splice_cb (GObject *ostream,
    GAsyncResult *res,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = user_data;
  SpliceContext *ctx;
  GError *error = NULL;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (ostream), res, &error);

  ctx = g_simple_async_result_get_op_res_gpointer (simple);
  if (!ctx->completed)
    {
      ctx->completed = TRUE;

      if (error != NULL)
        g_simple_async_result_set_from_error (simple, error);
      g_simple_async_result_complete_in_idle (simple);

      g_cancellable_cancel (ctx->op1_cancellable);
      g_cancellable_cancel (ctx->op2_cancellable);
    }

  g_clear_error (&error);
  g_object_unref (simple);
}

void
_g_io_stream_splice_async (GIOStream *stream1,
    GIOStream *stream2,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  SpliceContext *ctx;
  GInputStream *istream;
  GOutputStream *ostream;

  ctx = g_slice_new0 (SpliceContext);
  ctx->stream1 = g_object_ref (stream1);
  ctx->stream2 = g_object_ref (stream2);
  ctx->op1_cancellable = g_cancellable_new ();
  ctx->op2_cancellable = g_cancellable_new ();

  simple = g_simple_async_result_new (NULL, callback, user_data,
      _g_io_stream_splice_finish);
  g_simple_async_result_set_op_res_gpointer (simple, ctx,
      (GDestroyNotify) splice_context_free);

  istream = g_io_stream_get_input_stream (stream1);
  ostream = g_io_stream_get_output_stream (stream2);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, ctx->op1_cancellable, splice_cb,
      g_object_ref (simple));
  
  istream = g_io_stream_get_input_stream (stream2);
  ostream = g_io_stream_get_output_stream (stream1);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, ctx->op2_cancellable, splice_cb,
      g_object_ref (simple));

  g_object_unref (simple);
}

gboolean
_g_io_stream_splice_finish (GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
      _g_io_stream_splice_finish), FALSE);

  return TRUE;
}

