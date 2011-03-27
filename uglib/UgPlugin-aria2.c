/*
 *
 *   Copyright (C) 2005-2011 by C.H. Huang
 *   plushuang.tw@gmail.com
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ---
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU Lesser General Public License in all respects
 *  for all of the code used other than OpenSSL.  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so.  If you
 *  do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_PLUGIN_ARIA2

#include <string.h>
#include <stdlib.h>
// uglib
#include <UgStdio.h>
#include <UgPlugin-aria2.h>

#ifdef HAVE_LIBPWMD
#include "pwmd.h"
#endif

#define ARIA2_XMLRPC_URI		"http://localhost:6800/rpc"

// functions for UgPluginInterface
static gboolean	ug_plugin_aria2_global_init		(void);
static void		ug_plugin_aria2_global_finalize	(void);
static UgResult	ug_plugin_aria2_global_set		(guint parameter, gpointer data);

static gboolean	ug_plugin_aria2_init		(UgPluginAria2* plugin, UgDataset* dataset);
static void		ug_plugin_aria2_finalize	(UgPluginAria2* plugin);

static UgResult	ug_plugin_aria2_set_state	(UgPluginAria2* plugin, UgState  state);
static UgResult	ug_plugin_aria2_get_state	(UgPluginAria2* plugin, UgState* state);
static UgResult	ug_plugin_aria2_set			(UgPluginAria2* plugin, guint parameter, gpointer data);
static UgResult	ug_plugin_aria2_get			(UgPluginAria2* plugin, guint parameter, gpointer data);

// thread function
static gpointer	ug_plugin_aria2_thread		(UgPluginAria2* plugin);

// aria2 methods
static gboolean	ug_plugin_aria2_add_uri		(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_add_torrent	(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_add_metalink(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_remove		(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_get_servers	(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_tell_status	(UgPluginAria2* plugin);
static gboolean	ug_plugin_aria2_change_option(UgPluginAria2* plugin, UgXmlrpcValue* options);
static gboolean	ug_plugin_aria2_response	(UgPluginAria2* plugin, UgXmlrpcResponse response, const gchar* method);

// setup functions
static void		ug_plugin_aria2_set_scheme	(UgPluginAria2* plugin, UgXmlrpcValue* options);
static void		ug_plugin_aria2_set_common	(UgPluginAria2* plugin, UgXmlrpcValue* options);
static void		ug_plugin_aria2_set_http	(UgPluginAria2* plugin, UgXmlrpcValue* options);
static gboolean	ug_plugin_aria2_set_proxy	(UgPluginAria2* plugin, UgXmlrpcValue* options);
#ifdef HAVE_LIBPWMD
static gboolean	ug_plugin_aria2_set_proxy_pwmd (UgPluginAria2 *plugin, UgXmlrpcValue* options);
#endif

// utility
static gint64	ug_xmlrpc_value_get_int64	(UgXmlrpcValue* value);
static int		ug_xmlrpc_value_get_int		(UgXmlrpcValue* value);
static gpointer	ug_load_binary				(const gchar* file, guint* size);


enum Aria2Status
{
	ARIA2_WAITING,
	ARIA2_PAUSED,
	ARIA2_ACTIVE,
	ARIA2_ERROR,
	ARIA2_COMPLETE,
	ARIA2_REMOVED,
};

enum Aria2ErrorCode
{
	ARIA2_ERROR_NONE,
	ARIA2_ERROR_UNKNOW,
	ARIA2_ERROR_TIME_OUT,
	ARIA2_ERROR_RESOURCE_NOT_FOUND,
	ARIA2_ERROR_TOO_MANY_RESOURCE_NOT_FOUND,
	ARIA2_ERROR_SPEED_TOO_SLOW,
	ARIA2_ERROR_NETWORK,
};

// static data for UgPluginInterface
static const char*	supported_schemes[]   = {"http", "https", "ftp", "ftps", NULL};
static const char*	supported_filetypes[] = {"torrent", "metalink", "meta4", NULL};
static char*		xmlrpc_uri;
// extern
const	UgPluginInterface	ug_plugin_aria2_iface =
{
	sizeof (UgPluginAria2),										// instance_size
	"aria2",													// name

	supported_schemes,											// schemes
	supported_filetypes,										// file_types

	(UgGlobalInitFunc)		ug_plugin_aria2_global_init,		// global_init
	(UgGlobalFinalizeFunc)	ug_plugin_aria2_global_finalize,	// global_finalize
	(UgGlobalSetFunc)		ug_plugin_aria2_global_set,			// global_set
	(UgGlobalGetFunc)		NULL,								// global_get

	(UgPluginInitFunc)		ug_plugin_aria2_init,				// init
	(UgFinalizeFunc)		ug_plugin_aria2_finalize,			// finalize

	(UgSetStateFunc)		ug_plugin_aria2_set_state,			// set_state
	(UgGetStateFunc)		ug_plugin_aria2_get_state,			// get_state
	(UgSetFunc)				ug_plugin_aria2_set,				// set
	(UgGetFunc)				ug_plugin_aria2_get,				// get
};


// ----------------------------------------------------------------------------
// functions for UgPluginInterface

static gboolean	ug_plugin_aria2_global_init (void)
{
	xmlrpc_uri = g_strdup (ARIA2_XMLRPC_URI);

	return TRUE;
}

static void	ug_plugin_aria2_global_finalize (void)
{
	g_free (xmlrpc_uri);
}

static UgResult	ug_plugin_aria2_global_set (guint parameter, gpointer data)
{
	if (parameter != UG_DATA_STRING)
		return UG_RESULT_UNSUPPORT;

	g_free (xmlrpc_uri);
	xmlrpc_uri = g_strdup (data);
	return UG_RESULT_OK;
}

static gboolean	ug_plugin_aria2_init (UgPluginAria2* plugin, UgDataset* dataset)
{
	UgDataCommon*	common;
	UgDataHttp*		http;

	// get data
	common = ug_dataset_get (dataset, UG_DATA_COMMON_I, 0);
	http   = ug_dataset_get (dataset, UG_DATA_HTTP_I, 0);
	// check data
	if (common == NULL)
		return FALSE;
	// reset data
	if (common)
		common->retry_count = 0;
	if (http)
		http->redirection_count = 0;
	// copy supported data
	plugin->common = ug_datalist_copy (ug_dataset_get (dataset, UG_DATA_COMMON_I, 0));
	plugin->proxy  = ug_datalist_copy (ug_dataset_get (dataset, UG_DATA_PROXY_I, 0));
	plugin->http   = ug_datalist_copy (ug_dataset_get (dataset, UG_DATA_HTTP_I, 0));
	plugin->ftp    = ug_datalist_copy (ug_dataset_get (dataset, UG_DATA_FTP_I, 0));
	// xmlrpc
	ug_xmlrpc_init (&plugin->xmlrpc);
	ug_xmlrpc_use_client (&plugin->xmlrpc, xmlrpc_uri, NULL);
	// others
	plugin->string = g_string_sized_new (128);
	plugin->chunk  = g_string_chunk_new (512);

	return TRUE;
}

static void	ug_plugin_aria2_finalize (UgPluginAria2* plugin)
{
	// free data
	ug_datalist_free (plugin->common);
	ug_datalist_free (plugin->proxy);
	ug_datalist_free (plugin->http);
	ug_datalist_free (plugin->ftp);
	// free filename
	g_free (plugin->local_file);
	// xmlrpc
	ug_xmlrpc_finalize (&plugin->xmlrpc);
	// others
	g_string_free (plugin->string, TRUE);
	g_string_chunk_free (plugin->chunk);
}

static UgResult	ug_plugin_aria2_set_state (UgPluginAria2* plugin, UgState  state)
{
	UgState		old_state;

	old_state		= plugin->state;
	plugin->state	= state;
	// change plug-in status
	if (state != old_state) {
		if (state == UG_STATE_ACTIVE && old_state < UG_STATE_ACTIVE) {
			// call ug_plugin_unref () by ug_plugin_aria2_thread ()
			ug_plugin_ref ((UgPlugin*) plugin);
			g_thread_create ((GThreadFunc) ug_plugin_aria2_thread, plugin, FALSE, NULL);
		}

		ug_plugin_post ((UgPlugin*) plugin, ug_message_new_state (state));
	}

	return UG_RESULT_OK;
}

static UgResult	ug_plugin_aria2_get_state (UgPluginAria2* plugin, UgState* state)
{
	if (state) {
		*state = plugin->state;
		return UG_RESULT_OK;
	}

	return UG_RESULT_ERROR;
}

UgResult	ug_plugin_aria2_set (UgPluginAria2* plugin, guint parameter, gpointer data)
{
	UgXmlrpcValue*	options;
	UgXmlrpcValue*	member;
	gint64			speed_limit;

	if (parameter != UG_DATA_INT64)
		return UG_RESULT_UNSUPPORT;

	speed_limit = *(gint64*)data;
	options = ug_xmlrpc_value_new_struct (2);
	// max-download-limit
	member = ug_xmlrpc_value_alloc (options);
	member->name = "max-download-limit";
	member->type = UG_XMLRPC_STRING;
	member->c.string = g_strdup_printf ("%d", (int) speed_limit);
	// max-upload-limit
	member = ug_xmlrpc_value_alloc (options);
	member->name = "max-upload-limit";
	member->type = UG_XMLRPC_STRING;
	member->c.string = g_strdup_printf ("%d", (int) speed_limit);

	ug_plugin_aria2_change_option (plugin, options);

	g_free (ug_xmlrpc_value_at (options, 0)->c.string);
	g_free (ug_xmlrpc_value_at (options, 1)->c.string);
	ug_xmlrpc_value_free (options);

	return UG_RESULT_OK;
}

static UgResult	ug_plugin_aria2_get (UgPluginAria2* plugin, guint parameter, gpointer data)
{
	UgProgress*	progress;

	if (parameter != UG_DATA_INSTANCE)
		return UG_RESULT_UNSUPPORT;
	if (data == NULL || ((UgData*)data)->iface != UG_PROGRESS_I)
		return UG_RESULT_UNSUPPORT;

	progress = data;
	progress->download_speed = (gdouble) plugin->downloadSpeed;
	progress->upload_speed = (gdouble) plugin->uploadSpeed;
	progress->complete = plugin->completedLength;
	progress->total = plugin->totalLength;
	progress->consume_time = plugin->consumeTime;
	// If total size is unknown, don't calculate percent.
	if (progress->total)
		progress->percent = (gdouble) (progress->complete * 100 / progress->total);
	else
		progress->percent = 0;
	// If total size and average speed is unknown, don't calculate remain time.
	if (progress->download_speed > 0 && progress->total > 0)
		progress->remain_time = (progress->total - progress->complete) / progress->download_speed;

	return UG_RESULT_OK;
}

// ----------------------------------------------------------------------------
// thread function
//
static gpointer	ug_plugin_aria2_thread (UgPluginAria2* plugin)
{
	UgMessage*	message;
	char*		string;
	const char*	temp;
	char		ext;
	time_t		startingTime;
	gboolean	redirection;

	g_string_chunk_clear (plugin->chunk);
	startingTime = time (NULL);
	redirection  = TRUE;

	string = plugin->common->url;
	if (strncmp (string, "file:", 5) == 0) {
		string = g_filename_from_uri (string, NULL, NULL);
		temp = strrchr (string, G_DIR_SEPARATOR);
		if (temp == NULL)
			temp = string;
		temp = strrchr (temp, '.');
		ext = temp ? temp[1] : 0;
		plugin->local_file = string;

		switch (ext) {
		// torrent
		case 'T':
		case 't':
			if (ug_plugin_aria2_add_torrent (plugin) == FALSE)
				goto exit;
			break;

		// metalink
		case 'M':
		case 'm':
			if (ug_plugin_aria2_add_metalink (plugin) == FALSE)
				goto exit;
			break;

		default:
			message = ug_message_new_error (UG_MESSAGE_ERROR_CUSTOM, "unsupported file type");
			ug_plugin_post ((UgPlugin*) plugin, message);
			goto exit;
		}
	}
	else {
		if (ug_plugin_aria2_add_uri (plugin) == FALSE)
			goto exit;
	}

	for (;;) {
		if (plugin->state != UG_STATE_ACTIVE) {
			ug_plugin_aria2_remove (plugin);
			break;
		}

		plugin->consumeTime = (double) (time(NULL) - startingTime);
		ug_plugin_aria2_tell_status (plugin);

		if (plugin->totalLength > 0)
			ug_plugin_post ((UgPlugin*)plugin, ug_message_new_progress ());
		if (redirection && plugin->followedBy == NULL) {
			if (plugin->completedLength)
				redirection = FALSE;
			ug_plugin_aria2_get_servers (plugin);
		}

		switch (plugin->aria2Status) {
		case ARIA2_COMPLETE:
			// if current download is metalink or torrent file
			if (plugin->gid != plugin->followedBy  &&  plugin->followedBy) {
				plugin->gid = plugin->followedBy;
				break;
			}
			// post completed message
			ug_plugin_post ((UgPlugin*) plugin,
					ug_message_new_info (UG_MESSAGE_INFO_COMPLETE, NULL));
			ug_plugin_post ((UgPlugin*) plugin,
					ug_message_new_info (UG_MESSAGE_INFO_FINISH, NULL));
			goto break_while;

		case ARIA2_ERROR:
			ug_plugin_aria2_remove (plugin);
			string = g_strdup_printf ("aria2 error code: %u", plugin->errorCode);
			message = ug_message_new_error (UG_MESSAGE_ERROR_CUSTOM, string);
			g_free (string);
			ug_plugin_post ((UgPlugin*) plugin, message);
			goto break_while;
		}

		// delay 0.5 seconds
		ug_plugin_delay ((UgPlugin*) plugin, 500);
	}
break_while:

exit:
	if (plugin->state == UG_STATE_ACTIVE)
		ug_plugin_aria2_set_state (plugin, UG_STATE_READY);
	// call ug_plugin_ref () by ug_plugin_aria2_set_state ()
	ug_plugin_unref ((UgPlugin*) plugin);
	return NULL;
}


// ----------------------------------------------------------------------------
// aria2 methods
//
static gboolean	ug_plugin_aria2_add_uri	(UgPluginAria2* plugin)
{
	UgXmlrpcValue*		uris;		// UG_XMLRPC_ARRAY
	UgXmlrpcValue*		options;	// UG_XMLRPC_STRUCT
	UgXmlrpcValue*		value;
	UgDataCommon*		common;
	UgXmlrpcResponse	response;

	common = plugin->common;
	// URIs array
	uris = ug_xmlrpc_value_new_array (1);
	value = ug_xmlrpc_value_alloc (uris);
	value->type = UG_XMLRPC_STRING;
	value->c.string = common->url;
	// options struct
	options = ug_xmlrpc_value_new_struct (16);
	ug_plugin_aria2_set_scheme (plugin, options);
	ug_plugin_aria2_set_common (plugin, options);
	ug_plugin_aria2_set_proxy (plugin, options);
	ug_plugin_aria2_set_http (plugin, options);

	// aria2.addUri
	response = ug_xmlrpc_call (&plugin->xmlrpc,
			"aria2.addUri",
			UG_XMLRPC_ARRAY,  uris,
			UG_XMLRPC_STRUCT, options,
			UG_XMLRPC_NONE);
	// clear uris, options, and string chunk
	ug_xmlrpc_value_free (uris);
	ug_xmlrpc_value_free (options);
	g_string_chunk_clear (plugin->chunk);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.addUri") == FALSE)
		return FALSE;

	// get gid
	value = ug_xmlrpc_get_value (&plugin->xmlrpc);
	plugin->gid = g_string_chunk_insert (plugin->chunk, value->c.string);

	return TRUE;
}

static gboolean	ug_plugin_aria2_add_torrent (UgPluginAria2* plugin)
{
	UgXmlrpcValue*		torrent;
	UgXmlrpcValue*		options;
	UgXmlrpcValue*		uris;
	UgXmlrpcValue*		value;
	UgXmlrpcResponse	response;

	// options struct
	options = ug_xmlrpc_value_new_struct (16);
	ug_plugin_aria2_set_common (plugin, options);
	ug_plugin_aria2_set_proxy (plugin, options);
	// uri array
	uris = ug_xmlrpc_value_new_array (1);
	value = ug_xmlrpc_value_alloc (uris);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "";
	// torrent binary
	torrent = ug_xmlrpc_value_new ();
	torrent->type = UG_XMLRPC_BINARY;
	torrent->c.binary = ug_load_binary (plugin->local_file, &torrent->len);

	if (torrent->c.binary == NULL)
		response = UG_XMLRPC_ERROR;
	else {
		response = ug_xmlrpc_call (&plugin->xmlrpc,
				"aria2.addTorrent",
				UG_XMLRPC_BINARY, torrent,
				UG_XMLRPC_ARRAY,  uris,
				UG_XMLRPC_STRUCT, options,
				UG_XMLRPC_NONE);
	}
	// free resource
	g_free (torrent->c.binary);
	ug_xmlrpc_value_free (torrent);
	ug_xmlrpc_value_free (options);
	ug_xmlrpc_value_free (uris);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.addTorrent") == FALSE)
		return FALSE;

	// get gid
	torrent = ug_xmlrpc_get_value (&plugin->xmlrpc);
	plugin->gid = g_string_chunk_insert (plugin->chunk, torrent->c.string);
	return TRUE;
}

static gboolean	ug_plugin_aria2_add_metalink (UgPluginAria2* plugin)
{
	UgXmlrpcValue*		meta;
	UgXmlrpcValue*		options;
	UgXmlrpcResponse	response;

	// options struct
	options = ug_xmlrpc_value_new_struct (16);
	ug_plugin_aria2_set_scheme (plugin, options);
	ug_plugin_aria2_set_common (plugin, options);
	ug_plugin_aria2_set_proxy (plugin, options);
	ug_plugin_aria2_set_http (plugin, options);
	// metalink binary
	meta = ug_xmlrpc_value_new ();
	meta->type = UG_XMLRPC_BINARY;
	meta->c.binary = ug_load_binary (plugin->local_file, &meta->len);

	if (meta->c.binary == NULL)
		response = UG_XMLRPC_ERROR;
	else {
		response = ug_xmlrpc_call (&plugin->xmlrpc,
				"aria2.addMetalink",
				UG_XMLRPC_BINARY, meta,
				UG_XMLRPC_STRUCT, options,
				UG_XMLRPC_NONE);
	}
	// free resource
	g_free (meta->c.binary);
	ug_xmlrpc_value_free (meta);
	ug_xmlrpc_value_free (options);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.addMetalink") == FALSE)
		return FALSE;

	// get gid
	meta = ug_xmlrpc_get_value (&plugin->xmlrpc);
	meta = ug_xmlrpc_value_at (meta, 0);
	plugin->gid = g_string_chunk_insert (plugin->chunk, meta->c.string);
	return TRUE;
}

static gboolean	ug_plugin_aria2_remove (UgPluginAria2* plugin)
{
	UgXmlrpcResponse	response;

	response = ug_xmlrpc_call (&plugin->xmlrpc, "aria2.remove",
			UG_XMLRPC_STRING, plugin->gid,
			UG_XMLRPC_NONE);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.remove") == FALSE)
		return FALSE;
	return TRUE;
}

static gboolean	ug_plugin_aria2_get_servers (UgPluginAria2* plugin)
{
	UgXmlrpcValue*		array;		// UG_XMLRPC_ARRAY
	UgXmlrpcValue*		servers;	// UG_XMLRPC_STRUCT
	UgXmlrpcValue*		member;
	UgXmlrpcResponse	response;

	response = ug_xmlrpc_call (&plugin->xmlrpc, "aria2.getServers",
			UG_XMLRPC_STRING, plugin->gid,
			UG_XMLRPC_NONE);
	// message
	if (response != UG_XMLRPC_OK)
		return FALSE;

	// get servers
	array = ug_xmlrpc_get_value (&plugin->xmlrpc);
	if (array->type == UG_XMLRPC_ARRAY && array->len) {
		servers = ug_xmlrpc_value_at (array, 0);
		member = ug_xmlrpc_value_find (servers, "currentUri");
		if (member && strcmp (plugin->common->url, member->c.string) != 0) {
			g_free (plugin->common->url);
			plugin->common->url = g_strdup (member->c.string);
			ug_plugin_post ((UgPlugin*) plugin,
					ug_message_new_data (UG_MESSAGE_DATA_URL_CHANGED,
						member->c.string));
		}
	}

	return TRUE;
}

static gboolean	ug_plugin_aria2_tell_status (UgPluginAria2* plugin)
{
	UgXmlrpcValue*		keys;		// UG_XMLRPC_ARRAY
	UgXmlrpcValue*		progress;	// UG_XMLRPC_STRUCT
	UgXmlrpcValue*		value;
	UgXmlrpcResponse	response;
	gchar*				string;

	// set keys array
	keys = ug_xmlrpc_value_new_array (5);
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "status";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "totalLength";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "completedLength";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "downloadSpeed";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "uploadSpeed";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "errorCode";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "files";
	value = ug_xmlrpc_value_alloc (keys);
	value->type = UG_XMLRPC_STRING;
	value->c.string = "followedBy";

	response = ug_xmlrpc_call (&plugin->xmlrpc,
			"aria2.tellStatus",
			UG_XMLRPC_STRING, plugin->gid,
			UG_XMLRPC_ARRAY,  keys,
			UG_XMLRPC_NONE);
	// clear keys array
	ug_xmlrpc_value_free (keys);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.tellStatus") == FALSE)
		return FALSE;

	// get progress struct
	progress = ug_xmlrpc_get_value (&plugin->xmlrpc);
	if (progress->type != UG_XMLRPC_STRUCT)
		return FALSE;
	// status
	value = ug_xmlrpc_value_find (progress, "status");
	if (value && value->c.string) {
		switch (value->c.string[0]) {
		case 'a':	// active
			plugin->aria2Status = ARIA2_ACTIVE;
			break;

		case 'p':	// paused
			plugin->aria2Status = ARIA2_PAUSED;
			break;

		default:
		case 'w':	// waiting
			plugin->aria2Status = ARIA2_WAITING;
			break;

		case 'e':	// error
			plugin->aria2Status = ARIA2_ERROR;
			break;

		case 'c':	// complete
			plugin->aria2Status = ARIA2_COMPLETE;
			break;

		case 'r':	// removed
			plugin->aria2Status = ARIA2_REMOVED;
			break;
		}
	}
	// errorCode
	value = ug_xmlrpc_value_find (progress, "errorCode");
	plugin->errorCode = ug_xmlrpc_value_get_int (value);
	// totalLength
	value = ug_xmlrpc_value_find (progress, "totalLength");
	plugin->totalLength = ug_xmlrpc_value_get_int64 (value);
	// completedLength
	value = ug_xmlrpc_value_find (progress, "completedLength");
	plugin->completedLength = ug_xmlrpc_value_get_int64 (value);
	// downloadSpeed
	value = ug_xmlrpc_value_find (progress, "downloadSpeed");
	plugin->downloadSpeed = ug_xmlrpc_value_get_int (value);
	// uploadSpeed
	value = ug_xmlrpc_value_find (progress, "uploadSpeed");
	plugin->uploadSpeed = ug_xmlrpc_value_get_int (value);
	// followedBy
	value = ug_xmlrpc_value_find (progress, "followedBy");
	if (value  &&  value->len > 0) {
		keys = ug_xmlrpc_value_at (value, 0);
		plugin->followedBy = g_string_chunk_insert (plugin->chunk, keys->c.string);
	}
	// files
	value = ug_xmlrpc_value_find (progress, "files");
	if (plugin->local_file == FALSE  &&  plugin->followedBy == NULL) {
		keys = ug_xmlrpc_value_at (value, 0);
		keys = ug_xmlrpc_value_find (keys, "path");		// UG_XMLRPC_STRUCT
		if (keys  &&  g_strcmp0 (keys->c.string, plugin->path)) {
			plugin->path = g_string_chunk_insert (plugin->chunk, keys->c.string);
			string = strrchr (plugin->path, '/');
			string = string ? string+1 : plugin->path;
			ug_plugin_post ((UgPlugin*) plugin,
					ug_message_new_data (UG_MESSAGE_DATA_FILE_CHANGED, string));
		}
	}

	return TRUE;
}

static gboolean	ug_plugin_aria2_change_option (UgPluginAria2* plugin, UgXmlrpcValue* options)
{
	UgXmlrpcResponse	response;

	response = ug_xmlrpc_call (&plugin->xmlrpc,
			"aria2.changeOption",
			UG_XMLRPC_STRING, plugin->gid,
			UG_XMLRPC_STRUCT, options,
			UG_XMLRPC_NONE);
	// message
	if (ug_plugin_aria2_response (plugin, response, "aria2.changeOption") == FALSE)
		return FALSE;

	return TRUE;
}

static gboolean	ug_plugin_aria2_response (UgPluginAria2* plugin, UgXmlrpcResponse response, const gchar* method)
{
	UgMessage*			message;
	gchar*				temp;

	switch (response) {
	case UG_XMLRPC_ERROR:
		temp = g_strconcat (method, " result error", NULL);
		message = ug_message_new_error (UG_MESSAGE_ERROR_CUSTOM, temp);
		g_free (temp);
		ug_plugin_post ((UgPlugin*)plugin, message);
		return FALSE;

	case UG_XMLRPC_FAULT:
		temp = g_strconcat (method, " response fault", NULL);
		message = ug_message_new_error (UG_MESSAGE_ERROR_CUSTOM, temp);
		g_free (temp);
		ug_plugin_post ((UgPlugin*)plugin, message);
		return FALSE;

	case UG_XMLRPC_OK:
		break;
	}

	return TRUE;
}


// ----------------------------------------------------------------------------
// aria2 setup functions
//
static void	ug_plugin_aria2_set_scheme (UgPluginAria2* plugin, UgXmlrpcValue* options)
{
	UgXmlrpcValue*	value;
	UgDataCommon*	common;
	UgDataHttp*		http;
	UgDataFtp*		ftp;
	gchar*			user;
	gchar*			password;

	common = plugin->common;
	http   = plugin->http;
	ftp    = plugin->ftp;
	user     = NULL;
	password = NULL;

	if (http && (http->user || http->password) ) {
		user     = http->user     ? http->user     : "";
		password = http->password ? http->password : "";
		// http-user
		value = ug_xmlrpc_value_alloc (options);
		value->name = "http-user";
		value->type = UG_XMLRPC_STRING;
		value->c.string = user;
		// http-password
		value = ug_xmlrpc_value_alloc (options);
		value->name = "http-password";
		value->type = UG_XMLRPC_STRING;
		value->c.string = password;
	}

	if (ftp && (ftp->user || ftp->password)) {
		user     = ftp->user     ? ftp->user     : "";
		password = ftp->password ? ftp->password : "";
		// ftp-user
		value = ug_xmlrpc_value_alloc (options);
		value->name = "ftp-user";
		value->type = UG_XMLRPC_STRING;
		value->c.string = user;
		// ftp-password
		value = ug_xmlrpc_value_alloc (options);
		value->name = "ftp-password";
		value->type = UG_XMLRPC_STRING;
		value->c.string = password;
	}

	if (user == NULL) {
		if (common->user || common->password) {
			user     = common->user     ? common->user     : "";
			password = common->password ? common->password : "";
			if (g_ascii_strncasecmp (common->url, "http", 4) == 0) {
				// http-user
				value = ug_xmlrpc_value_alloc (options);
				value->name = "http-user";
				value->type = UG_XMLRPC_STRING;
				value->c.string = user;
				// http-password
				value = ug_xmlrpc_value_alloc (options);
				value->name = "http-password";
				value->type = UG_XMLRPC_STRING;
				value->c.string = password;
			}
			else if (g_ascii_strncasecmp (common->url, "ftp", 3) == 0) {
				// ftp-user
				value = ug_xmlrpc_value_alloc (options);
				value->name = "ftp-user";
				value->type = UG_XMLRPC_STRING;
				value->c.string = user;
				// ftp-password
				value = ug_xmlrpc_value_alloc (options);
				value->name = "ftp-password";
				value->type = UG_XMLRPC_STRING;
				value->c.string = password;
			}
		}
	}
}

static void	ug_plugin_aria2_set_common	(UgPluginAria2* plugin, UgXmlrpcValue* options)
{
	UgXmlrpcValue*	value;
	UgDataCommon*	common;
	GString*		string;

	common = plugin->common;
	string = plugin->string;

	if (common->folder) {
		value = ug_xmlrpc_value_alloc (options);
		value->name = "dir";
		value->type = UG_XMLRPC_STRING;
		value->c.string = common->folder;
	}
	// max-tries
	value = ug_xmlrpc_value_alloc (options);
	value->name = "max-tries";
	value->type = UG_XMLRPC_STRING;
	g_string_printf (string, "%u", common->retry_limit);
	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
	// max-download-limit
	value = ug_xmlrpc_value_alloc (options);
	value->name = "max-download-limit";
	value->type = UG_XMLRPC_STRING;
	g_string_printf (string, "%u", (guint) common->max_download_speed);
	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
	// max-upload-limit
	value = ug_xmlrpc_value_alloc (options);
	value->name = "max-upload-limit";
	value->type = UG_XMLRPC_STRING;
	g_string_printf (string, "%u", (guint) common->max_upload_speed);
	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
	// lowest-speed-limit
	value = ug_xmlrpc_value_alloc (options);
	value->name = "lowest-speed-limit";
	value->type = UG_XMLRPC_STRING;
	g_string_printf (string, "%u", 512);
	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
	// max-concurrent-downloads
//	value = ug_xmlrpc_value_alloc (options);
//	value->name = "max-concurrent-downloads";
//	value->type = UG_XMLRPC_STRING;
//	g_string_printf (string, "%u", common->segments_per_download);
//	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
	// max-connection-per-server
	value = ug_xmlrpc_value_alloc (options);
	value->name = "max-connection-per-server";
	value->type = UG_XMLRPC_STRING;
	g_string_printf (string, "%u", common->max_connections);
	value->c.string = g_string_chunk_insert (plugin->chunk, string->str);
}

static void	ug_plugin_aria2_set_http (UgPluginAria2* plugin, UgXmlrpcValue* options)
{
	UgXmlrpcValue*	value;
	UgDataHttp*		http;

	http = plugin->http;

	if (http) {
		if (http->referrer) {
			value = ug_xmlrpc_value_alloc (options);
			value->name = "referer";
			value->type = UG_XMLRPC_STRING;
			value->c.string = http->referrer;
		}
		if (http->cookie_file) {
			value = ug_xmlrpc_value_alloc (options);
			value->name = "load-cookies";
			value->type = UG_XMLRPC_STRING;
			value->c.string = http->cookie_file;
		}
		if (http->user_agent) {
			value = ug_xmlrpc_value_alloc (options);
			value->name = "user-agent";
			value->type = UG_XMLRPC_STRING;
			value->c.string = http->user_agent;
		}
	}
}

static gboolean	ug_plugin_aria2_set_proxy (UgPluginAria2* plugin, UgXmlrpcValue* options)
{
	UgXmlrpcValue*	value;
	UgDataProxy*	proxy;

	proxy = plugin->proxy;
	if (proxy == NULL)
		return TRUE;

#ifdef HAVE_LIBPWMD
	if (proxy->type == UG_DATA_PROXY_PWMD) {
		if (ug_plugin_aria2_set_proxy_pwmd (plugin, options) == FALSE)
			return FALSE;
	}
#endif

	// host
	if (proxy->host) {
		value = ug_xmlrpc_value_alloc (options);
		value->name = "all-proxy";
		value->type = UG_XMLRPC_STRING;
		if (proxy->port)
			value->c.string = proxy->host;
		else {
			g_string_printf (plugin->string, "%s:%u", proxy->host, proxy->port);
			value->c.string = g_string_chunk_insert (plugin->chunk, plugin->string->str);
		}

		// proxy user and password
		if (proxy->user || proxy->password) {
			// user
			value = ug_xmlrpc_value_alloc (options);
			value->name = "all-proxy-user";
			value->type = UG_XMLRPC_STRING;
			value->c.string = proxy->user ? proxy->user : "";
			// password
			value = ug_xmlrpc_value_alloc (options);
			value->name = "all-proxy-password";
			value->type = UG_XMLRPC_STRING;
			value->c.string = proxy->password ? proxy->password : "";
		}
	}

	return TRUE;
}


// ----------------------------------------------------------------------------
// PWMD
//
#ifdef HAVE_LIBPWMD
static gboolean	ug_plugin_aria2_set_proxy_pwmd (UgPluginAria2 *plugin, UgXmlrpcValue* options)
{
       struct pwmd_proxy_s pwmd;
       gpg_error_t rc;
       UgMessage *message;

       memset(&pwmd, 0, sizeof(pwmd));
       rc = ug_set_pwmd_proxy_options(&pwmd, plugin->proxy);

       if (rc)
               goto fail;

       // proxy host and port
	// host
	UgXmlrpcValue *value = ug_xmlrpc_value_alloc (options);
	value->name = "all-proxy";
	value->type = UG_XMLRPC_STRING;
       if (pwmd.port == 0)
               value->c.string = g_string_chunk_insert (plugin->chunk,
                               pwmd.hostname);
	else {
               g_string_printf (plugin->string, "%s:%u", pwmd.hostname,
                               pwmd.port);
		value->c.string = g_string_chunk_insert (plugin->chunk, plugin->string->str);
	}

	// proxy user and password
       if (pwmd.username || pwmd.password) {
		// user
		value = ug_xmlrpc_value_alloc (options);
		value->name = "all-proxy-user";
		value->type = UG_XMLRPC_STRING;
               value->c.string = g_string_chunk_insert (plugin->chunk,
                               pwmd.username ? pwmd.username : "");
		// password
		value = ug_xmlrpc_value_alloc (options);
		value->name = "all-proxy-password";
		value->type = UG_XMLRPC_STRING;
               value->c.string = g_string_chunk_insert (plugin->chunk,
                               pwmd.password ? pwmd.password : "");
	}

       ug_close_pwmd(&pwmd);
       return TRUE;

fail:
       ug_close_pwmd(&pwmd);
       gchar *e = g_strdup_printf("Pwmd ERR %i: %s", rc, pwmd_strerror(rc));
       message = ug_message_new_error (UG_MESSAGE_ERROR_CUSTOM, e);
       ug_plugin_post ((UgPlugin*) plugin, message);
       fprintf(stderr, "%s\n", e);
       g_free(e);
       return FALSE;
}

#endif	// HAVE_LIBPWMD


// ----------------------------------------------------------------------------
// utility
//
static gint64	ug_xmlrpc_value_get_int64 (UgXmlrpcValue* value)
{
	if (value) {
		switch (value->type) {
		case UG_XMLRPC_STRING:
#ifdef _WIN32
			return _atoi64 (value->c.string);
#else
			return atoll (value->c.string);
#endif
		case UG_XMLRPC_INT64:
			return value->c.int64;

		case UG_XMLRPC_DOUBLE:
			return (gint64) value->c.double_;

		default:
			break;
		}
	}
	return 0;
}

static int	ug_xmlrpc_value_get_int (UgXmlrpcValue* value)
{
	if (value) {
		switch (value->type) {
		case UG_XMLRPC_STRING:
			return atoi (value->c.string);

		case UG_XMLRPC_INT:
			return value->c.int_;

		case UG_XMLRPC_DOUBLE:
			return (int) value->c.double_;

		default:
			break;
		}
	}
	return 0;
}

static gpointer	ug_load_binary	(const gchar* file, guint* filesize)
{
	int			fd;
	int			size;
	gpointer	buffer;

//	fd = open (file, O_RDONLY | O_BINARY, S_IREAD);
	fd = ug_fd_open (file, UG_FD_O_READONLY | UG_FD_O_BINARY, UG_FD_S_IREAD);
	if (fd == -1)
		return NULL;
//	lseek (fd, 0, SEEK_END);
	ug_fd_seek (fd, 0, SEEK_END);
//	size = tell (fd);
	size = (int) ug_fd_tell (fd);
	buffer = g_malloc (size);
//	lseek (fd, 0, SEEK_SET);
	ug_fd_seek (fd, 0, SEEK_SET);
//	if (read (fd, buffer, size) != size)
	if (ug_fd_read (fd, buffer, size) != size) {
		g_free (buffer);
		buffer = NULL;
		size = 0;
	}
	// close (fd);
	ug_fd_close (fd);
	*filesize = size;
	return buffer;
}

#endif	// HAVE_PLUGIN_ARIA2

