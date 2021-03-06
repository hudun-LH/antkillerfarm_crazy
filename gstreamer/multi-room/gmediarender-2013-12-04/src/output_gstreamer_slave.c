#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/net/gstnetclientclock.h>
#include <gst/net/gstnettimeprovider.h>

#include "logging.h"
#include "upnp_control_point.h"
#include "output_module.h"
#include "output_gstreamer.h"

typedef struct{
  GSocketService *service;
  GString *cmd_buf;
}ControlServiceData;

typedef struct{
  gchar cmd_name[10];
  gint arg_num;
  void (*cmd_handler)(gchar **arg_strv, gint arg_num);
}CommandFormat;

ControlServiceData control_service_data;

void pad_added_handler (GstElement *src, GstPad *new_pad, gpointer data)
{
	GstElement *sink = (GstElement *)data;
	GstPad *sink_pad = gst_element_get_static_pad (sink, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;
	guint caps_size = 0, i;

	g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
	g_print ("sink_pad: '%s'\n", GST_PAD_NAME (sink_pad));

	if (gst_pad_is_linked (sink_pad)) {
		g_print ("We are already linked. Ignoring.\n");
		goto exit;
	}

	new_pad_caps = gst_pad_get_current_caps(new_pad);
	caps_size = gst_caps_get_size(new_pad_caps);
	g_print ("caps_size : %d\n", caps_size);
	for (i = 0; i < caps_size; i++)
	{
		new_pad_struct = gst_caps_get_structure(new_pad_caps, i);
		new_pad_type = gst_structure_get_name(new_pad_struct);
		g_print ("new_pad_type %d: '%s'\n", i, new_pad_type);
		if (strstr(new_pad_type, "audio/x-raw"))
		{
			ret = gst_pad_link (new_pad, sink_pad);
			if (GST_PAD_LINK_FAILED (ret)) {
				g_print ("Type is '%s' but link failed.\n", new_pad_type);
			} else {
				g_print ("Link succeeded (type '%s').\n", new_pad_type);
			}
			break;
		}
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref (new_pad_caps);
   
	/* Unreference the sink pad */
	gst_object_unref (sink_pad);
}

#if (TRANS_TYPE==TRANS_TYPE_RTP)
void output_gstreamer_pipeline_init_slave(GstElement *player, gchar* ip_addr)
{
        GstElement *source;
	GstElement *rtpjitterbuffer;
	GstElement *rtpdepay;
	GstElement *decode_bin;
	GstElement *audio_sink0;
	GstElement *convert;

	source = gst_element_factory_make ("udpsrc", "source");
	rtpjitterbuffer = gst_element_factory_make ("rtpjitterbuffer", "rtpjitterbuffer");
	rtpdepay = gst_element_factory_make ("rtpgstdepay", "rtpdepay");
	decode_bin = gst_element_factory_make ("decodebin", "decode_bin");
        convert = gst_element_factory_make("audioconvert", "convert");
        audio_sink0 = gst_element_factory_make ("autoaudiosink", "audio_sink");
	
	if (!player || !source || !rtpjitterbuffer || !rtpdepay || !decode_bin || !convert || !audio_sink0)
	{
		g_print ("Not all elements could be created.\n");
	}

	g_object_set(rtpjitterbuffer, "latency", 500, NULL);

	gst_bin_add_many (GST_BIN (player), source, rtpjitterbuffer, rtpdepay, decode_bin, convert, audio_sink0, NULL);

	if (gst_element_link_many (source, rtpjitterbuffer, rtpdepay, decode_bin, NULL) != TRUE)
	{
		g_print ("Elements could not be linked.\n");
		//gst_object_unref (player);
	}
	if (gst_element_link_many (convert, audio_sink0, NULL) != TRUE)
	{
		g_print ("Elements could not be linked. 1\n");
		//gst_object_unref (player);
	}

	GstCaps *caps = gst_caps_new_simple ("application/x-rtp",
					     "media", G_TYPE_STRING, "application",
					     "payload", G_TYPE_INT, 96,
					     "clock-rate", G_TYPE_INT, 90000,
					     "encoding-name", G_TYPE_STRING, "X-GST",
					     NULL);
	g_object_set(source, "caps", caps, NULL);
	gst_caps_unref(caps);

	g_object_set(source, "address", ip_addr, NULL);
	g_object_set(source, "port", MEDIA_PORT, NULL);
	g_signal_connect(decode_bin, "pad-added", G_CALLBACK (pad_added_handler), convert);	
}
#else
void output_gstreamer_pipeline_init_slave(GstElement *player, gchar* ip_addr)
{
	GstElement *source;
	GstElement *decode_bin;
	GstElement *audio_sink0;

	source = gst_element_factory_make ("tcpserversrc", "source");
	decode_bin = gst_element_factory_make ("decodebin", "decode_bin");
        convert = gst_element_factory_make("audioconvert", "convert");
        audio_sink0 = gst_element_factory_make ("autoaudiosink", "audio_sink");
	
	if (!player || !source || !decode_bin || !gst_data.convert || !audio_sink0)
	{
		g_print ("Not all elements could be created.\n");
	}

	gst_bin_add_many (GST_BIN (player), source, decode_bin, gst_data.convert, audio_sink0, NULL);

	if (gst_element_link_many (source, decode_bin, NULL) != TRUE)
	{
		g_print ("Elements could not be linked. 1\n");
		//gst_object_unref (player);
	}

	if (gst_element_link_many (gst_data.convert, audio_sink0, NULL) != TRUE)
	{
		g_print ("Elements could not be linked. 2\n");
		//gst_object_unref (player);
	}

	g_object_set(source, "host", ip_addr, NULL);
	g_print("tcpserversrc %s\n", ip_addr);
	g_object_set(source, "port", MEDIA_PORT, NULL);

	g_signal_connect(decode_bin, "pad-added", G_CALLBACK (pad_added_handler), convert);
}
#endif

int output_gstreamer_init_slave(void)
{
	GstBus *bus;
	output_gstreamer_control_init_slave();

	player_ = gst_pipeline_new("audio_player_slave");
	output_gstreamer_pipeline_init_slave(player_, UpnpGetServerIpAddress());
	
	bus = gst_pipeline_get_bus(GST_PIPELINE(player_));
	gst_bus_add_watch(bus, my_bus_callback, NULL);
	gst_object_unref(bus);

	if (audio_sink != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting audio sink to %s; device=%s\n",
			 audio_sink, audio_device ? audio_device : "");
		sink = gst_element_factory_make (audio_sink, "sink");
		if (sink == NULL) {
		  Log_error("gstreamer", "Couldn't create sink '%s'",
			    audio_sink);
		} else {
		  if (audio_device != NULL) {
		    g_object_set (G_OBJECT(sink), "device", audio_device, NULL);
		  }
		  g_object_set (G_OBJECT (player_), "audio-sink", sink, NULL);
		}
	}
	if (videosink != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting video sink to %s", videosink);
		sink = gst_element_factory_make (videosink, "sink");
		g_object_set (G_OBJECT (player_), "video-sink", sink, NULL);
	}
	
	if (gst_element_set_state(player_, GST_STATE_READY) ==
	    GST_STATE_CHANGE_FAILURE) {
		Log_error("gstreamer", "Error: pipeline doesn't become ready.");
	}
	gstreamer_output.get_volume = NULL;
	gstreamer_output.set_volume = NULL;
	gst_element_set_state (player_, GST_STATE_PLAYING);

	return 0;
}

static void cmd_do_play(gchar **arg_strv, gint arg_num)
{
	g_print ("%s\n", __func__);
	/*if (gst_element_set_state(gst_data.playbin, GST_STATE_PLAYING) ==
	  GST_STATE_CHANGE_FAILURE) {
	  g_print("gstreamer setting play state failed (2)\n");
	  }*/
}

static void cmd_do_pause(gchar **arg_strv, gint arg_num)
{
	g_print ("%s\n", __func__);
	/*if (gst_element_set_state(gst_data.playbin, GST_STATE_PAUSED) ==
	  GST_STATE_CHANGE_FAILURE) {
	  g_print("gstreamer setting play state failed (3)\n");
	  }*/
}

static void cmd_do_stop(gchar **arg_strv, gint arg_num)
{
	g_print ("%s\n", __func__);
	//gst_element_set_state (gst_data.playbin, GST_STATE_NULL);
	//gst_element_set_state (gst_data.playbin, GST_STATE_PLAYING);
}

static void cmd_do_eos(gchar **arg_strv, gint arg_num)
{
	//gst_element_send_event(gst_data.playbin, gst_event_new_eos());
	g_print ("%s\n", __func__);
}

static void cmd_do_clock(gchar **arg_strv, gint arg_num)
{
  //GstClock *client_clock;
  //g_print ("%s %s %s %s\n", __func__, arg_strv[0], arg_strv[1], arg_strv[2]);
  //client_clock = gst_net_client_clock_new (NULL, arg_strv[1], atoi(arg_strv[2]), 0);
  //g_usleep (G_USEC_PER_SEC / 2);

  //gst_pipeline_use_clock (GST_PIPELINE (player_), client_clock);
  //gst_element_set_start_time (gst_data.playbin, GST_CLOCK_TIME_NONE);
  //gst_pipeline_set_latency (GST_PIPELINE (gst_data.playbin), GST_SECOND / 2);
}

CommandFormat cmd_format[] =
{
	{"Play", 1, cmd_do_play},
	{"Pause", 1, cmd_do_pause},
	{"Stop", 1, cmd_do_stop},
	{"EOS", 1, cmd_do_eos},
	{"Clock", 3, cmd_do_clock}
};

static gboolean are_cmd_args_valid(gchar **arg_strv, gint arg_num)
{
	gint i;
	for (i = 0; i < arg_num; i++)
	{
		if (strlen(arg_strv[i]) == 0)
		{
			return FALSE;
		}
	}
	return TRUE;
}

#define ARG_PIECES 4

static void cmd_handler(gchar* cmd_str)
{
	gchar **arg_strv;
	gint strv_len, i, cmd_format_size;
	arg_strv = g_strsplit(cmd_str, "#", ARG_PIECES);
	strv_len = g_strv_length(arg_strv);
	if (strv_len == 0)
	{
		g_strfreev(arg_strv);
		return;
	}
	cmd_format_size = sizeof(cmd_format) / sizeof(CommandFormat);
	for (i = 0; i < cmd_format_size; i++)
	{
		if (g_strcmp0(arg_strv[0], cmd_format[i].cmd_name) == 0)
		{
			if (are_cmd_args_valid(arg_strv, cmd_format[i].arg_num))
			{
				cmd_format[i].cmd_handler(arg_strv, cmd_format[i].arg_num);
			}
		}
	}

	g_strfreev(arg_strv);
}

#define CMD_BUF_PIECES 4

static void cmd_buf_handler()
{
	gchar **cmd_strv;
	gint strv_len, i;

	do
	{
		cmd_strv = g_strsplit(control_service_data.cmd_buf->str, "\n", CMD_BUF_PIECES);
		strv_len = g_strv_length(cmd_strv);
		if (strv_len == 0)
		{
			g_strfreev(cmd_strv);
			return;
		}
		for (i = 0; i < strv_len - 1; i++)
		{
			if (strlen(cmd_strv[i]) > 0)
			{
				g_print("Cmd was : \"%s\"\n", cmd_strv[i]);
				cmd_handler(cmd_strv[i]);
			}
		}
		g_string_free(control_service_data.cmd_buf, TRUE);
		control_service_data.cmd_buf = g_string_new_len(cmd_strv[strv_len - 1], strlen(cmd_strv[strv_len -1]));
		g_strfreev(cmd_strv);
	} while (strv_len == CMD_BUF_PIECES);
}

static gboolean incoming_callback  (GSocketService *service,
                    GSocketConnection *connection,
                    GObject *source_object,
                    gpointer user_data)
{
	gint read_cnt;
	g_print("Received Connection from client!\n");
	GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
	gchar message[1024];
	do
	{
		memset(message, 0, sizeof(message));
		read_cnt = g_input_stream_read  (istream,
						 message,
						 1024,
						 NULL,
						 NULL);
		g_print("Message was %d: \"%s\"\n", read_cnt, message);
		if (read_cnt > 0)
		{
			if (control_service_data.cmd_buf == NULL)
			{
				control_service_data.cmd_buf = g_string_new_len(message, read_cnt);
			}
			else
			{
				control_service_data.cmd_buf = g_string_append_len(control_service_data.cmd_buf, message, read_cnt);
			}
			cmd_buf_handler();
		}
	} while (read_cnt > 0);
	g_print("Client disconnection!\n");
	if (control_service_data.cmd_buf != NULL)
	{
		g_string_free(control_service_data.cmd_buf, TRUE);
		control_service_data.cmd_buf = NULL;
	}
	return FALSE;
}

int output_gstreamer_control_init_slave(void)
{
	GError * error = NULL;
	control_service_data.cmd_buf = NULL;

	/* create the new socketservice */
	control_service_data.service = g_threaded_socket_service_new(1);

	GInetAddress *address = g_inet_address_new_from_string(UpnpGetServerIpAddress());
	GSocketAddress *socket_address = g_inet_socket_address_new(address, CONTROL_PORT);
	g_socket_listener_add_address(G_SOCKET_LISTENER(control_service_data.service), socket_address, G_SOCKET_TYPE_STREAM,
				      G_SOCKET_PROTOCOL_TCP, NULL, NULL, NULL);

	/* don't forget to check for errors */
	if (error != NULL)
	{
		g_error ("%s", error->message);
	}

	/* listen to the 'incoming' signal */
	g_signal_connect (control_service_data.service,
			  "run",
			  G_CALLBACK (incoming_callback),
			  NULL);

	/* start the socket service */
	g_socket_service_start (control_service_data.service);

	/* enter mainloop */
	g_print ("Waiting for client!\n");
	return 0;
}

