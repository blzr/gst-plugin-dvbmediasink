#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gst/gst.h>

#include "gstaactranscode.h"

enum {
	CONV_AC3, CONV_EAC3, CONV_OFF
};

#define BASE_CAPS \
	"audio/mpeg, "  \
	"mpegversion = (int) 2;" \
	"audio/mpeg, " \
	"mpegversion = (int) 4, " \
	"stream-format = (string) { raw, adts }; " \

GST_DEBUG_CATEGORY_STATIC(aactranscode_debug);
#define GST_CAT_DEFAULT (aactranscode_debug)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (BASE_CAPS));

static GstElementClass *parent_class = NULL;

static int get_transcoding_setting(int *conv, int *multich_only,
		long int *bitrate);
static int gst_aactranscode_get_channels_count_from_caps(GstAacTranscode *aacts,
		GstCaps *caps, gint *channels);
static gboolean gst_aactranscode_sink_event(GstPad *pad, GstObject *parent,
		GstEvent *event);
static gboolean gst_aactranscode_sink_query(GstPad *pad, GstObject * parent,
		GstQuery * query);
static GstStateChangeReturn gst_aactranscode_change_state(GstElement * element,
		GstStateChange transition);

static void gst_aactranscode_base_init(gpointer g_class) {
	GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_add_pad_template(element_class,
			gst_static_pad_template_get(&sink_factory));
	gst_element_class_set_static_metadata(element_class,
			"AAC transcoder dvb audio sink", "Sink/Audio",
			"AAC to AC3 to DVBAudioSink", "mx3L");
}

static void gst_aactranscode_class_init(GstAacTranscodeClass *klass) {
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstelement_class->change_state = GST_DEBUG_FUNCPTR(
			gst_aactranscode_change_state);
	parent_class = g_type_class_ref(GST_TYPE_BIN);
}

static void gst_aactranscode_init(GstAacTranscode *aacts,
		GstAacTranscodeClass * g_class) {
	aacts->conv = CONV_OFF;
	aacts->multich_only = 0;
	aacts->bitrate = 0;

	int ret = get_transcoding_setting(&(aacts->conv), &(aacts->multich_only),
			&(aacts->bitrate));
	if (ret == -1)
		GST_DEBUG_OBJECT(aacts, "cannot open settings file!");

	char *conv_str;
	if (aacts->conv == CONV_AC3)
		conv_str = "ac3";
	else if (aacts->conv == CONV_EAC3)
		conv_str = "eac3";
	else
		conv_str = "off";
	GST_DEBUG_OBJECT(aacts, "conv = %s, multich_only = %d, bitrate = %ld",
			conv_str, aacts->multich_only, aacts->bitrate);
}

GType gst_aactranscode_get_type(void) {
	static GType aactranscode_type = 0;

	if (!aactranscode_type) {
		static const GTypeInfo aactranscode_info = {
				sizeof(GstAacTranscodeClass),
				(GBaseInitFunc) gst_aactranscode_base_init,
				NULL, (GClassInitFunc) gst_aactranscode_class_init,
				NULL,
				NULL, sizeof(GstAacTranscode), 0,
				(GInstanceInitFunc) gst_aactranscode_init, };

		aactranscode_type = g_type_register_static(GST_TYPE_BIN,
				"GstAacTranscode", &aactranscode_info, 0);

		GST_DEBUG_CATEGORY_INIT(aactranscode_debug, "aactranscode", 0,
				"AAC transcoder dvb audio sink");
	}
	return aactranscode_type;
}

static int get_transcoding_setting(int *conv, int *multich_only,
		long int *bitrate) {
	char *filepath = (char *) malloc(sizeof(char) * (strlen(SYSCONFDIR) + 24));
	sprintf(filepath, "%s/gstreamer/aactranscode", SYSCONFDIR);

	FILE *f;
	f = fopen(filepath, "r");
	free(filepath);
	if (!f)
		return -1;

	char setting[32] = { 0 };
	fread(setting, sizeof(setting), 1, f);

	/* set defaults */
	*conv = CONV_OFF;
	*multich_only = 0;
	*bitrate = 0;

	int x;
	char *p;
	char *p_setting = setting;
	for (x = 0; (p = strsep(&p_setting, ",")) != NULL; x++) {
		if (x == 0) {
			if (!strcmp(p, "ac3"))
				*conv = CONV_AC3;
			else if (!strcmp(p, "eac3"))
				*conv = CONV_EAC3;
			else
				break;
		} else if (x == 1 && !strcmp(p, "1"))
			*multich_only = 1;
		else if (x == 2) {
			long ret;
			char *tail;
			ret = strtol(p, &tail, 10);
			if (*tail == '\0')
				*bitrate = ret;
		}
	}
	fclose(f);
	return 0;
}

/* channel count detection is borrowed from faad element */
static int gst_aactranscode_get_channels_count_from_caps(GstAacTranscode *aacts,
		GstCaps *caps, gint *channels) {
	GstStructure *str = gst_caps_get_structure(caps, 0);
	GstBuffer *buf;
	GstMapInfo map;
	guint8 *cdata;
	gsize csize;
	const GValue *value;
	if ((value = gst_structure_get_value(str, "codec_data"))) {
		buf = gst_value_get_buffer(value);
		if (buf)

			gst_buffer_map(buf, &map, GST_MAP_READ);
		cdata = map.data;
		csize = map.size;

		if (csize < 2)
			goto wrong_length;

		GST_DEBUG_OBJECT(aacts,
				"codec_data: object_type=%d, sample_rate=%d, channels=%d",
				((cdata[0] & 0xf8) >> 3),
				(((cdata[0] & 0x07) << 1) | ((cdata[1] & 0x80) >> 7)),
				((cdata[1] & 0x78) >> 3));

		*channels = ((cdata[1] & 0x78) >> 3);
		gst_buffer_unmap(buf, &map);
		return 0;

		wrong_length: {
			GST_DEBUG_OBJECT(aacts, "codec_data less than 2 bytes long");
			gst_buffer_unmap(buf, &map);
			return -1;
		}
	}
	return -1;
}

static gboolean gst_aactranscode_sink_query(GstPad *pad, GstObject *parent,
		GstQuery *query) {
	GstAacTranscode *aacts = GST_AACTRANSCODE(parent);
	gboolean ret = FALSE;
	GST_LOG_OBJECT(aacts, "%s query", GST_QUERY_TYPE_NAME(query));

	switch (GST_QUERY_TYPE(query)) {
	case GST_QUERY_CAPS: {
		GstCaps *filter;
		GstCaps *caps = NULL;

		gst_query_parse_caps(query, &filter);
		caps = gst_caps_from_string(BASE_CAPS);
		if (filter) {
			GstCaps *intersection;
			intersection = gst_caps_intersect_full(filter, caps,
					GST_CAPS_INTERSECT_FIRST);
			gst_caps_unref(caps);
			caps = intersection;
		}
		if (caps != NULL) {
			GST_DEBUG_OBJECT(aacts, "returning caps %" GST_PTR_FORMAT, caps);
			gst_query_set_caps_result(query, caps);
			gst_caps_unref(caps);
			ret = TRUE;
		}
		break;
	}
	case GST_QUERY_ACCEPT_CAPS: {
		GstCaps *caps, *allowed;
		gboolean result;

		gst_query_parse_accept_caps(query, &caps);
		allowed = gst_caps_from_string(BASE_CAPS);
		result = gst_caps_is_subset(caps, allowed);
		GST_DEBUG_OBJECT(aacts,
				"Checking if requested caps %" GST_PTR_FORMAT " are a subset of pad caps %" GST_PTR_FORMAT " result %d",
				caps, allowed, result);
		gst_caps_unref(allowed);
		if (result && aacts->multich_only) {
			gint channels;
			/* we have to detect channels count from codec-data */
			if (!gst_aactranscode_get_channels_count_from_caps(aacts, caps,
					&channels) && (channels > 2 && channels < 9))
				result = TRUE;
			else
				result = FALSE;
		}
		gst_query_set_accept_caps_result(query, result);
		ret = TRUE;
		break;
	}
	default:
		ret = gst_pad_query_default(pad, parent, query);
		break;
	}
	return ret;
}

static gboolean gst_aactranscode_sink_event(GstPad *pad, GstObject *parent,
		GstEvent * event) {
	GstAacTranscode *aacts = GST_AACTRANSCODE(gst_pad_get_parent(pad));
	gboolean ret = FALSE;

	GST_LOG_OBJECT(aacts, "%s event", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event)) {
	default:
		ret = gst_pad_event_default(pad, parent, event);
		break;
	}
	return ret;
}

static GstStateChangeReturn gst_aactranscode_change_state(GstElement *element,
		GstStateChange transition) {
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstAacTranscode *aacts = GST_AACTRANSCODE(element);

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_NULL_TO_READY");

		if (aacts->conv == CONV_OFF) {
			GST_DEBUG_OBJECT(aacts, "transcoding turned off");
			return GST_STATE_CHANGE_FAILURE;
		}

		GstElement *aacdecoder = gst_element_factory_make("faad", NULL);
		GstElement *audioconvert = gst_element_factory_make("audioconvert",
				NULL);
		GstElement *audioresample = gst_element_factory_make("audioresample",
				NULL);

		GstElement *encoder = NULL;
		if (aacts->conv == CONV_EAC3) {
			GST_DEBUG_OBJECT(aacts, "transcoding AAC to Enhanced-AC3");
			encoder = gst_element_factory_make("avenc_eac3", NULL);
		} else {
			GST_DEBUG_OBJECT(aacts, "transcoding AAC to AC3");
			encoder = gst_element_factory_make("avenc_ac3", NULL);
		}
		if (encoder)
			g_object_set(G_OBJECT(encoder), "bitrate", aacts->bitrate, NULL);

		GstElement *dvbaudiosink = gst_element_factory_make("dvbaudiosink",
				NULL);

		if (!aacdecoder || !audioconvert || !encoder || !dvbaudiosink) {
			GST_ERROR_OBJECT(aacts,
					"missing some element(s), check if the following are installed "
							"{ faad, audioconvert, audioresample, avenc_ac3, avenc_eac3, dvbaudiosink }");
			return GST_STATE_CHANGE_FAILURE;
		}
		gst_bin_add_many(GST_BIN(aacts), aacdecoder, audioconvert,
				audioresample, encoder, dvbaudiosink, NULL);

		if (!gst_element_link_many(aacdecoder, audioconvert, audioresample,
				encoder, dvbaudiosink, NULL)) {
			GST_ERROR_OBJECT(aacts, "cannot link some elements!");
			return GST_STATE_CHANGE_FAILURE;
		}

		/* aacdecoder's sink pad is bin's sink pad */
		GstPad *aacsinkpad = gst_element_get_static_pad(aacdecoder, "sink");
		GstPad *gsinkpad = gst_ghost_pad_new("sink", aacsinkpad);
		gst_pad_set_event_function(gsinkpad,
				GST_DEBUG_FUNCPTR(gst_aactranscode_sink_event));
		gst_pad_set_query_function(gsinkpad,
				GST_DEBUG_FUNCPTR(gst_aactranscode_sink_query));
		gst_pad_set_active(gsinkpad, TRUE);
		gst_element_add_pad(GST_ELEMENT(aacts), gsinkpad);
		gst_object_unref(aacsinkpad);
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_READY_TO_PAUSED");
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

	switch (transition) {
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_PLAYING_TO_PAUSED");
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_PAUSED_TO_READY");
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG_OBJECT(aacts, "GST_STATE_CHANGE_READY_TO_NULL");
		break;
	default:
		break;
	}
	return ret;
}

static gboolean plugin_init(GstPlugin * plugin) {
	/* FIXME make sure that this plugin has higher rank then dvbaudiosink */
	if (!gst_element_register(plugin, "aactranscode", GST_RANK_PRIMARY + 2,
	GST_TYPE_AACTRANSCODE))
		return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, aactranscode,
		"AAC to AC3 to DVBAudioSink", plugin_init, VERSION, "LGPL", "GStreamer",
		"http://gstreamer.net/");
