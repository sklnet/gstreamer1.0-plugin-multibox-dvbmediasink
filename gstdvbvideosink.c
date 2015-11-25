/*
 * GStreamer DVB Media Sink
 *
 * Copyright 2011 <slashdev@gmx.net>
 *
 * based on code by:
 * Copyright 2006 Felix Domke <tmbinc@elitedvb.net>
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-plugin
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m audiotestsrc ! plugin ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __sh__
#include <linux/dvb/stm_ioctls.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#define PACK_UNPACKED_XVID_DIVX5_BITSTREAM

#include "common.h"
#include "gstdvbvideosink.h"
#include "gstdvbsink-marshal.h"

#ifndef VIDEO_SET_CODEC_DATA
typedef struct video_codec_data
{
	int length;
	guint8 *data;
} video_codec_data_t;
#define VIDEO_SET_CODEC_DATA _IOW('o', 80, video_codec_data_t)
#endif

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
struct bitstream
{
	guint8 *data;
	guint8 last;
	int avail;
};

void bitstream_init(struct bitstream *bit, const void *buffer, gboolean wr)
{
	bit->data = (guint8*) buffer;
	if (wr) {
		bit->avail = 0;
		bit->last = 0;
	}
	else {
		bit->avail = 8;
		bit->last = *bit->data++;
	}
}

unsigned long bitstream_get(struct bitstream *bit, int bits)
{
	unsigned long res = 0;
	while (bits)
	{
		unsigned int d = bits;
		if (!bit->avail)
		{
			bit->last = *bit->data++;
			bit->avail = 8;
		}
		if (d > bit->avail)
			d=bit->avail;
		res<<=d;
		res|=(bit->last>>(bit->avail-d))&~(-1<<d);
		bit->avail -= d;
		bits -= d;
	}
	return res;
}

void bitstream_put(struct bitstream *bit, unsigned long val, int bits)
{
	while (bits)
	{
		bit->last |= ((val & (1 << (bits-1))) ? 1 : 0) << (7 - bit->avail);
		if (++bit->avail == 8)
		{
			*bit->data = bit->last;
			++bit->data;
			bit->last = 0;
			bit->avail = 0;
		}
		--bits;
	}
}
#endif

GST_DEBUG_CATEGORY_STATIC (dvbvideosink_debug);
#define GST_CAT_DEFAULT dvbvideosink_debug

#define VIDEO_CAPS \
  "width = (int) [ 16, 4096 ], " \
  "height = (int) [ 16, 4096 ] "

#define MPEG4V2_LIMITED_CAPS \
  "width = (int) [ 16, 800 ], " \
  "height = (int) [ 16, 600 ], " \
  "framerate = (fraction) [ 0, MAX ]"

enum
{
	PROP_0,
	PROP_SYNC,
	PROP_LAST,
};

enum
{
	SIGNAL_GET_DECODER_TIME,
	LAST_SIGNAL
};

static guint gst_dvb_videosink_signals[LAST_SIGNAL] = { 0 };

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS (
#ifdef HAVE_MPEG4
	"video/mpeg, "
		"mpegversion = (int) 4, "
		VIDEO_CAPS "; "
#endif
	"video/mpeg, "
		"mpegversion = (int) { 1, 2 }, "
		VIDEO_CAPS "; "
#ifdef HAVE_H265
	"video/x-h265, "
		VIDEO_CAPS "; "
#endif
#ifdef HAVE_H264
	"video/x-h264, "
		VIDEO_CAPS "; "
	"video/x-h264, "
		"parsed = (boolean)true; "
#endif
#ifdef HAVE_H263
	"video/x-h263, "
		VIDEO_CAPS "; "
#endif
#ifdef HAVE_MPEG4V2
	"video/x-msmpeg, "
#ifdef HAVE_LIMITED_MPEG4V2
		MPEG4V2_LIMITED_CAPS
#else
		VIDEO_CAPS 
#endif
		", msmpegversion = (int) 43; "
	"video/x-divx, "
#ifdef HAVE_LIMITED_MPEG4V2
		MPEG4V2_LIMITED_CAPS
#else
		VIDEO_CAPS 
#endif
		", divxversion = (int) 3;"
	"video/x-divx, "
#ifdef HAVE_LIMITED_MPEG4V2
		MPEG4V2_LIMITED_CAPS
#else
		VIDEO_CAPS
#endif
		", divxversion = (int) [4, 6];"
	"video/x-xvid, "
#ifdef HAVE_LIMITED_MPEG4V2
		MPEG4V2_LIMITED_CAPS
#else
		VIDEO_CAPS 
#endif
		"; "
	"video/x-3ivx, "
#ifdef HAVE_LIMITED_MPEG4V2
		MPEG4V2_LIMITED_CAPS
#else
		VIDEO_CAPS 
#endif
		"; "
#endif
#ifdef HAVE_WMV
	"video/x-wmv, "
		VIDEO_CAPS ", wmvversion = (int) 3; "
#endif
	"video/mpegts, systemstream=(boolean)true, "
		VIDEO_CAPS "; "
	)
);

#define VIDEO_ENCODING_UNKNOWN  0xFF

unsigned int streamtype_to_encoding(unsigned int streamtype)
{
#ifdef VIDEO_SET_ENCODING
	switch(streamtype)
	{
	case STREAMTYPE_MPEG2:
		return VIDEO_ENCODING_AUTO;
	case STREAMTYPE_MPEG4_H264:
		return VIDEO_ENCODING_H264;
	case STREAMTYPE_H263:
		return VIDEO_ENCODING_H263;
	case STREAMTYPE_MPEG4_Part2:
		return VIDEO_ENCODING_MPEG4P2;
	case STREAMTYPE_MPEG1:
		return VIDEO_ENCODING_AUTO;
	case STREAMTYPE_XVID:
		return VIDEO_ENCODING_MPEG4P2;
	case STREAMTYPE_DIVX311:
		return VIDEO_ENCODING_MPEG4P2;
	case STREAMTYPE_DIVX4:
		return VIDEO_ENCODING_MPEG4P2;
	case STREAMTYPE_DIVX5:
		return VIDEO_ENCODING_MPEG4P2;
	case STREAMTYPE_VC1:
		return VIDEO_ENCODING_VC1;
	case STREAMTYPE_VC1_SM:
		return VIDEO_ENCODING_WMV;
	default:
		return VIDEO_ENCODING_UNKNOWN;
	}
#endif
	return VIDEO_ENCODING_UNKNOWN;
}

static void gst_dvbvideosink_init(GstDVBVideoSink *self);
static void gst_dvbvideosink_dispose(GObject *obj);
static void gst_dvbvideosink_reset(GObject *obj);
static void gst_dvbvideosink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dvbvideosink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

#define DEBUG_INIT \
	GST_DEBUG_CATEGORY_INIT(dvbvideosink_debug, "dvbvideosink", 0, "dvbvideosink element");

static GstBaseSinkClass *parent_class = NULL;
G_DEFINE_TYPE_WITH_CODE(GstDVBVideoSink, gst_dvbvideosink, GST_TYPE_BASE_SINK, DEBUG_INIT);

static gboolean gst_dvbvideosink_start (GstBaseSink * sink);
static gboolean gst_dvbvideosink_stop (GstBaseSink * sink);
static gboolean gst_dvbvideosink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_dvbvideosink_render (GstBaseSink * sink, GstBuffer * buffer);
static gboolean gst_dvbvideosink_set_caps (GstBaseSink * sink, GstCaps * caps);
static gboolean gst_dvbvideosink_unlock (GstBaseSink * basesink);
static gboolean gst_dvbvideosink_unlock_stop (GstBaseSink * basesink);
static GstStateChangeReturn gst_dvbvideosink_change_state (GstElement * element, GstStateChange transition);
static gint64 gst_dvbvideosink_get_decoder_time (GstDVBVideoSink *self);

/* initialize the plugin's class */
static void gst_dvbvideosink_class_init(GstDVBVideoSinkClass *self)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (self);
	GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (self);
	GstElementClass *element_class = GST_ELEMENT_CLASS (self);

	parent_class = g_type_class_peek_parent(self);
	gobject_class->finalize = gst_dvbvideosink_reset;
	gobject_class->dispose = gst_dvbvideosink_dispose;

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));
	gst_element_class_set_static_metadata(element_class,
		"DVB video sink",
		"Generic/DVBVideoSink",
		"Outputs PES into a linuxtv dvb video device",
		"PLi team");

	gobject_class->set_property = gst_dvbvideosink_set_property;
	gobject_class->get_property = gst_dvbvideosink_get_property;

	g_object_class_install_property (gobject_class, PROP_SYNC,
			g_param_spec_boolean ("sync", "Sync", "Sync on the clock", FALSE,
					G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_dvbvideosink_start);
	gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_dvbvideosink_stop);
	gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_dvbvideosink_render);
	gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_dvbvideosink_event);
	gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_dvbvideosink_unlock);
	gstbasesink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_dvbvideosink_unlock_stop);
	gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_dvbvideosink_set_caps);

	element_class->change_state = GST_DEBUG_FUNCPTR (gst_dvbvideosink_change_state);

	gst_dvb_videosink_signals[SIGNAL_GET_DECODER_TIME] =
		g_signal_new ("get-decoder-time",
		G_TYPE_FROM_CLASS (self),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (GstDVBVideoSinkClass, get_decoder_time),
		NULL, NULL, gst_dvbsink_marshal_INT64__VOID, G_TYPE_INT64, 0);

	self->get_decoder_time = gst_dvbvideosink_get_decoder_time;
}

#define H264_BUFFER_SIZE (64*1024+2048)

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void gst_dvbvideosink_init(GstDVBVideoSink *self)
{
	self->must_send_header = TRUE;
	self->h264_nal_len_size = 0;
	self->pesheader_buffer = NULL;
	self->codec_data = NULL;
	self->codec_type = CT_H264;
	self->stream_type = STREAMTYPE_UNKNOWN;
#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	self->must_pack_bitstream = FALSE;
	self->num_non_keyframes = 0;
	self->prev_frame = NULL;
#endif
	self->use_dts = FALSE;
	self->paused = self->playing = self->unlocking = self->flushing = FALSE;
	self->pts_written = self->using_dts_downmix = FALSE;
	self->lastpts = 0;
	self->timestamp_offset = 0;
	self->queue = NULL;
	self->fd = -1;
	self->unlockfd[0] = self->unlockfd[1] = -1;
	self->saved_fallback_framerate[0] = 0;
	self->rate = 1.0;
#ifdef VIDEO_SET_ENCODING
	self->use_set_encoding      = TRUE;
#else
	self->use_set_encoding      = FALSE;
#endif

	gst_base_sink_set_sync(GST_BASE_SINK(self), FALSE);
	gst_base_sink_set_async_enabled(GST_BASE_SINK(self), TRUE);
}

static void gst_dvbvideosink_dispose(GObject *obj)
{
	G_OBJECT_CLASS(parent_class)->dispose(obj);
	GST_DEBUG("GstDVBVideoSink DISPOSED");
}

static void gst_dvbvideosink_reset(GObject *obj)
{
	G_OBJECT_CLASS(parent_class)->finalize(obj);
	GST_DEBUG("GstDVBVideoSink RESET");
}

static void gst_dvbvideosink_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK(object);

	switch (prop_id)
	{
	/* sink should only work with sync turned off, ignore all attempts to change it */
	case PROP_SYNC:
		GST_INFO_OBJECT(self, "ignoring attempt to change 'sync' to '%d'", g_value_get_boolean(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void gst_dvbvideosink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK(object);

	switch (prop_id)
	{
	case PROP_SYNC:
		g_value_set_boolean(value, gst_base_sink_get_sync(GST_BASE_SINK(object)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gint64 gst_dvbvideosink_get_decoder_time(GstDVBVideoSink *self)
{
	gint64 cur = 0;
	if (self->fd < 0 || !self->playing || !self->pts_written) return GST_CLOCK_TIME_NONE;

	ioctl(self->fd, VIDEO_GET_PTS, &cur);
	if (cur)
	{
		self->lastpts = cur;
	}
	else
	{
		cur = self->lastpts;
	}
	cur *= 11111;
	cur -= self->timestamp_offset;

	return cur;
}

static gboolean gst_dvbvideosink_unlock(GstBaseSink *basesink)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK (basesink);
	self->unlocking = TRUE;
	/* wakeup the poll */
	write(self->unlockfd[1], "\x01", 1);
	GST_DEBUG_OBJECT(basesink, "unlock");
	return TRUE;
}

static gboolean gst_dvbvideosink_unlock_stop(GstBaseSink *basesink)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK (basesink);
	self->unlocking = FALSE;
	GST_DEBUG_OBJECT(basesink, "unlock_stop");
	return TRUE;
}

static gboolean gst_dvbvideosink_event(GstBaseSink *sink, GstEvent *event)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK (sink);
	GST_INFO_OBJECT (self, "EVENT %s", gst_event_type_get_name(GST_EVENT_TYPE (event)));
	int ret = TRUE;

	switch (GST_EVENT_TYPE (event))
	{
	case GST_EVENT_FLUSH_START:
		if(self->flushed && !self->playing && self->using_dts_downmix)
		{ 
			self->playing = TRUE;
			self->ok_to_write = 1;
		}
		self->flushed = FALSE;
		self->flushing = TRUE;
		/* wakeup the poll */
		write(self->unlockfd[1], "\x01", 1);
		if(self->paused) ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	case GST_EVENT_FLUSH_STOP:
		if (self->fd >= 0) ioctl(self->fd, VIDEO_CLEAR_BUFFER);
		GST_OBJECT_LOCK(self);
		self->must_send_header = TRUE;
		while (self->queue)
		{
			queue_pop(&self->queue);
		}
		self->flushing = FALSE;
		GST_OBJECT_UNLOCK(self);
		if(self->paused) ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		/* flush while media is playing requires a delay before rendering */
		if (self->using_dts_downmix)
		{
			self->ok_to_write = 0;
			self->playing = FALSE;
		}
		self->flushed = TRUE;
		break;
	case GST_EVENT_EOS:
	{
		gboolean pass_eos = FALSE;
		struct pollfd pfd[2];
		pfd[0].fd = self->unlockfd[0];
		pfd[0].events = POLLIN;
		pfd[1].fd = self->fd;
		pfd[1].events = POLLIN;

#ifdef VIDEO_FLUSH
		ioctl(self->fd, VIDEO_FLUSH, 1/*NONBLOCK*/); //Notify the player that no addionional data will be injected
#endif

		GST_BASE_SINK_PREROLL_UNLOCK(sink);
		while (1)
		{
			int retval = poll(pfd, 2, 250);
			if (retval < 0)
			{
				perror("poll in EVENT_EOS");
				ret = FALSE;
				break;
			}

			if (pfd[0].revents & POLLIN)
			{
				GST_DEBUG_OBJECT (self, "wait EOS aborted!!\n");
				ret = FALSE;
				break;
			}

			if (pfd[1].revents & POLLIN)
			{
				GST_DEBUG_OBJECT (self, "got buffer empty from driver!\n");
				break;
			}

			if (sink->flushing)
			{
				GST_DEBUG_OBJECT (self, "wait EOS flushing!!\n");
				ret = FALSE;
				break;
			}
		}
		GST_BASE_SINK_PREROLL_LOCK(sink);
		if (ret) ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	}
	case GST_EVENT_SEGMENT:
	{
		const GstSegment *segment;
		GstFormat format;
		gdouble rate;
		guint64 start, end, pos;
		gint64 start_dvb;
		gst_event_parse_segment(event, &segment);
		format = segment->format;
		rate = segment->rate;
		start = segment->start;
		end = segment->stop;
		pos = segment->position;
		start_dvb = start / 11111LL;
		GST_INFO_OBJECT(self, "SEGMENT rate=%f format=%d start=%"G_GUINT64_FORMAT " pos=%"G_GUINT64_FORMAT, rate, format, start, pos);
		GST_INFO_OBJECT(self, "SEGMENT DVB TIMESTAMP=%"G_GINT64_FORMAT " HEXFORMAT %#"G_GINT64_MODIFIER "x", start_dvb, start_dvb);
		if (format == GST_FORMAT_TIME)
		{
			self->timestamp_offset = start - pos;
			if (rate != self->rate)
			{
				int skip = 0, repeat = 0;
				if (rate > 1.0)
				{
					skip = (int)rate;
				}
				else if (rate < 1.0)
				{
					repeat = 1.0 / rate;
				}
				ioctl(self->fd, VIDEO_SLOWMOTION, repeat);
				ioctl(self->fd, VIDEO_FAST_FORWARD, skip);
				ioctl(self->fd, VIDEO_CONTINUE);
				self->rate = rate;
			}
		}
		ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	}
	case GST_EVENT_TAG:
	{
		GstTagList *taglist;
		gst_event_parse_tag(event, &taglist);
		GST_INFO_OBJECT(self,"TAG %"GST_PTR_FORMAT, taglist);
		ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	}
	case GST_EVENT_TOC:
	{
		GstToc *toc;
		gboolean updated;
		gst_event_parse_toc (event, &toc, &updated);
		/* get toc entries info if updated */
		GList *i = NULL;
		for (i = gst_toc_get_entries(toc); i; i = i->next)
		{
			GstTocEntry *entry = (GstTocEntry*)(i->data);
			GST_INFO_OBJECT(self,"Toc entry_type %s", gst_toc_entry_type_get_nick(gst_toc_entry_get_entry_type (entry)));
			GList *x = NULL;
			for (x = gst_toc_entry_get_sub_entries (entry); x; x = x->next)
			{
				GstTocEntry *sub_entry = (GstTocEntry*)(x->data);
				GstTagList *tags = gst_toc_entry_get_tags(sub_entry);
				gint64 start = 0;
				gint64 stop = 0;
				gst_toc_entry_get_start_stop_times(sub_entry, &start, &stop);
				gchar *title;
				gst_tag_list_get_string (tags, "title", &title);
				GST_INFO_OBJECT(self,"%s start=%"G_GINT64_FORMAT " stop=%"G_GINT64_FORMAT,
								 title, start, stop);
				g_free(title);
			}
		}
		GstTocScope scope = gst_toc_get_scope(toc);
		GST_INFO_OBJECT(self,"TOC  scope=%d", scope);
		ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	}
	default:
		ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
		break;
	}

	return ret;
}

static int video_write(GstBaseSink *sink, GstDVBVideoSink *self, GstBuffer *buffer, size_t start, size_t end)
{
	size_t written = start;
	size_t len = end;
	struct pollfd pfd[2];
	guint8 *data;
	int retval = 0;
	GstMapInfo map;
	gst_buffer_map(buffer, &map, GST_MAP_READ);
	data = map.data;

	pfd[0].fd = self->unlockfd[0];
	pfd[0].events = POLLIN;
	pfd[1].fd = self->fd;
	pfd[1].events = POLLOUT | POLLPRI;

	do
	{
		if (self->flushing)
		{
			GST_INFO_OBJECT(self, "flushing, skip %d bytes", len - written);
			break;
		}
		else if (self->paused || self->unlocking)
		{
			GST_OBJECT_LOCK(self);
			queue_push(&self->queue, buffer, written, end);
			GST_OBJECT_UNLOCK(self);
			GST_DEBUG_OBJECT(self, "pushed %d bytes to queue", len - written);
			break;
		}
		else
		{
			GST_DEBUG_OBJECT (self, "going into poll, have %d bytes to write", len - written);
		}
		if (poll(pfd, 2, -1) < 0)
		{
			if (errno == EINTR) continue;
			retval = -1;
			break;
		}
		if (pfd[0].revents & POLLIN)
		{
			/* read all stop commands */
			while (1)
			{
				gchar command;
				int res = read(self->unlockfd[0], &command, 1);
				if (res < 0)
				{
					GST_DEBUG_OBJECT (self, "no more commands");
					/* no more commands */
					break;
				}
			}
		}
		if (pfd[1].revents & POLLPRI)
		{
			GstStructure *s;
			GstMessage *msg;
			struct video_event evt;
			if (ioctl(self->fd, VIDEO_GET_EVENT, &evt) < 0)
			{
				g_warning("failed to ioctl VIDEO_GET_EVENT!");
			}
			else
			{
				GST_INFO_OBJECT (self, "VIDEO_EVENT %d", evt.type);
				if (evt.type == VIDEO_EVENT_SIZE_CHANGED) {
					s = gst_structure_new ("eventSizeChanged",
						"aspect_ratio", G_TYPE_INT, evt.u.size.aspect_ratio == 0 ? 2 : 3,
						"width", G_TYPE_INT, evt.u.size.w,
						"height", G_TYPE_INT, evt.u.size.h, NULL);
					msg = gst_message_new_element (GST_OBJECT(sink), s);
					gst_element_post_message (GST_ELEMENT(sink), msg);
				}
				else if (evt.type == VIDEO_EVENT_FRAME_RATE_CHANGED)
				{
					s = gst_structure_new ("eventFrameRateChanged",
						"frame_rate", G_TYPE_INT, evt.u.frame_rate, NULL);
					msg = gst_message_new_element (GST_OBJECT(sink), s);
					gst_element_post_message (GST_ELEMENT(sink), msg);
				}
				else if (evt.type == 16 /*VIDEO_EVENT_PROGRESSIVE_CHANGED*/)
				{
					s = gst_structure_new ("eventProgressiveChanged",
						"progressive", G_TYPE_INT, evt.u.frame_rate, NULL);
					msg = gst_message_new_element (GST_OBJECT(sink), s);
					gst_element_post_message (GST_ELEMENT(sink), msg);
				}
				else
				{
					g_warning ("unhandled DVBAPI Video Event %d", evt.type);
				}
			}
		}
		if (pfd[1].revents & POLLOUT)
		{
			size_t queuestart, queueend;
			GstBuffer *queuebuffer;
			GST_OBJECT_LOCK(self);
			if (queue_front(&self->queue, &queuebuffer, &queuestart, &queueend) >= 0)
			{
				guint8 *queuedata;
				GstMapInfo queuemap;
				gst_buffer_map(queuebuffer, &queuemap, GST_MAP_READ);
				queuedata = queuemap.data;
				int wr = write(self->fd, queuedata + queuestart, queueend - queuestart);
				gst_buffer_unmap(queuebuffer, &queuemap);
				if (wr < 0)
				{
					switch (errno)
					{
						case EINTR:
						case EAGAIN:
							break;
						default:
							GST_OBJECT_UNLOCK(self);
							retval = -3;
							break;
					}
					if (retval < 0) break;
				}
				else if (wr >= queueend - queuestart)
				{
					queue_pop(&self->queue);
					GST_DEBUG_OBJECT (self, "written %d queue bytes... pop entry", wr);
				}
				else
				{
					self->queue->start += wr;
					GST_DEBUG_OBJECT (self, "written %d queue bytes... update offset", wr);
				}
				GST_OBJECT_UNLOCK(self);
				continue;
			}
			GST_OBJECT_UNLOCK(self);
			int wr = write(self->fd, data + written, len - written);
			if (wr < 0)
			{
				switch (errno)
				{
					case EINTR:
					case EAGAIN:
						continue;
					default:
						retval = -3;
						break;
				}
				if (retval < 0) break;
			}
			written += wr;
		}
	} while (written < len);

	gst_buffer_unmap(buffer, &map);
	return retval;
}

static GstFlowReturn gst_dvbvideosink_render(GstBaseSink *sink, GstBuffer *buffer)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK(sink);
	guint8 *pes_header;
	gsize pes_header_len = 0;
	gsize data_len;
	guint8 *data, *original_data;
	guint8 *codec_data = NULL;
	gsize codec_data_size = 0;
	gsize payload_len = 0;
	GstBuffer *tmpbuf = NULL;
	GstFlowReturn ret = GST_FLOW_OK;

	if (self->fd < 0)
	{
		return GST_FLOW_OK;
	}
	gint i = 0;
	/* WAIT 1 seconds after flush needed for enigma2 to be ready*/
	while (self->ok_to_write == 0)
	{
			self->flushed = FALSE;
			self->ok_to_write = 1;
			self->playing = TRUE;
			gst_sleepms(1000);
			GST_INFO_OBJECT(self,"RESUME PLAY AFTER FLUSH + 1 SECOND");
	}
	GstMapInfo map, pesheadermap, codecdatamap;
	gst_buffer_map(buffer, &map, GST_MAP_READ);
	original_data = data = map.data;
	data_len = map.size;
	gst_buffer_map(self->pesheader_buffer, &pesheadermap, GST_MAP_WRITE);
	pes_header = pesheadermap.data;
	if (self->codec_data)
	{
		gst_buffer_map(self->codec_data, &codecdatamap, GST_MAP_READ);
		codec_data = codecdatamap.data;
		codec_data_size = codecdatamap.size;
	}

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	gboolean commit_prev_frame_data = FALSE, cache_prev_frame = FALSE;
#endif

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (self->must_pack_bitstream)
	{
		cache_prev_frame = TRUE;
		unsigned int pos = 0;
		while (pos < data_len)
		{
			if (memcmp(&data[pos], "\x00\x00\x01", 3))
			{
				pos++;
				continue;
			}
			pos += 3;
			if ((data[pos++] & 0xF0) == 0x20)
			{ // we need time_inc_res
				gboolean low_delay=FALSE;
				unsigned int ver_id = 1, shape=0, time_inc_res=0, tmp=0;
				struct bitstream bit;
				bitstream_init(&bit, data+pos, 0);
				bitstream_get(&bit, 9);
				if (bitstream_get(&bit, 1))
				{
					ver_id = bitstream_get(&bit, 4); // ver_id
					bitstream_get(&bit, 3);
				}
				if ((tmp = bitstream_get(&bit, 4)) == 15)
				{ // Custom Aspect Ration
					bitstream_get(&bit, 8); // skip AR width
					bitstream_get(&bit, 8); // skip AR height
				}
				if (bitstream_get(&bit, 1))
				{
					bitstream_get(&bit, 2);
					low_delay = bitstream_get(&bit, 1) ? TRUE : FALSE;
					if (bitstream_get(&bit, 1))
					{
						bitstream_get(&bit, 32);
						bitstream_get(&bit, 32);
						bitstream_get(&bit, 15);
					}
				}
				shape = bitstream_get(&bit, 2);
				if (ver_id != 1 && shape == 3 /* Grayscale */) bitstream_get(&bit, 4);
				bitstream_get(&bit, 1);
				time_inc_res = bitstream_get(&bit, 16);
				self->time_inc_bits = 0;
				while (time_inc_res)
				{ // count bits
					++self->time_inc_bits;
					time_inc_res >>= 1;
				}
			}
		}
	}

	if (self->must_pack_bitstream)
	{
		int tmp1, tmp2;
		unsigned char c1, c2;
		unsigned int pos = 0;
		while (pos < data_len)
		{
			if (memcmp(&data[pos], "\x00\x00\x01\xb2", 4))
			{
				pos++;
				continue;
			}
			pos += 4;
			if (data_len - pos < 13) break;
			if (sscanf((char*)data+pos, "DivX%d%c%d%cp", &tmp1, &c1, &tmp2, &c2) == 4 && (c1 == 'b' || c1 == 'B') && (c2 == 'p' || c2 == 'P')) 
			{
				GST_INFO_OBJECT (self, "%s seen... already packed!", (char*)data+pos);
				self->must_pack_bitstream = FALSE;
				break;
			}
		}
	}
#endif
	pes_header[0] = 0;
	pes_header[1] = 0;
	pes_header[2] = 1;
	pes_header[3] = 0xE0;

	pes_header[6] = 0x81;
	pes_header[7] = 0; /* no pts */
	pes_header[8] = 0;
	pes_header_len = 9;

	if (self->codec_type == CT_VC1 || self->codec_type == CT_VC1_SM)
	{
		if (!(GST_BUFFER_FLAGS(buffer) & GST_BUFFER_FLAG_DELTA_UNIT))
		{
			pes_header[6] = 0x80;
		}
	}

	if (GST_BUFFER_PTS_IS_VALID(buffer) || (self->use_dts && GST_BUFFER_DTS_IS_VALID(buffer)))
	{
		pes_header[7] = 0x80; /* pts */
		pes_header[8] = 5; /* pts size */
		pes_header_len += 5;
		pes_set_pts(GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) : GST_BUFFER_DTS(buffer), pes_header);

		if (self->codec_data)
		{
			if (self->must_send_header)
			{
				if (self->codec_type != CT_MPEG1 && self->codec_type != CT_MPEG2 && (self->codec_type != CT_DIVX4 || data[3] == 0x00))
				{
					if (self->codec_type == CT_DIVX311)
					{
						video_write(sink, self, self->codec_data, 0, codec_data_size);
					}
					else
					{
						memcpy(pes_header + pes_header_len, codec_data, codec_data_size);
						pes_header_len += codec_data_size;
					}
					self->must_send_header = FALSE;
				}
			}
			if (self->codec_type == CT_H264 || self->codec_type == CT_H265)
			{
				unsigned int pos = 0;
				if (self->h264_nal_len_size >= 3)
				{
					/* we need to write to the buffer */
					gst_buffer_unmap(buffer, &map);
					if (!gst_buffer_is_writable(buffer))
					{
						/* buffer is not writable, create a new buffer to which we can write */
						buffer = tmpbuf = gst_buffer_copy(buffer);
					}
					gst_buffer_map(buffer, &map, GST_MAP_READ | GST_MAP_WRITE);
					original_data = data = map.data;
					data_len = map.size;
					while (1)
					{
						unsigned int pack_len = 0;
						int i;
						for (i = 0; i < self->h264_nal_len_size; i++, pos++)
						{
							pack_len <<= 8;
							pack_len += data[pos];
							/* replace the lenght field with \x00..\x00\x01 */
							data[pos] = (i == self->h264_nal_len_size - 1) ? 1 : 0;
						}
						if ((pos + pack_len) >= data_len) break;
						pos += pack_len;
					}
				}
				else
				{
					/* length field too small to insert \x00\x00\x01, so we need to copy everything into a second buffer */
					unsigned char *dest;
					unsigned int dest_pos = 0;
					/* TODO: predict needed size, based on data_len and h264_nal_len_size, and number of frames */
					tmpbuf = gst_buffer_new_and_alloc(H264_BUFFER_SIZE);
					GstMapInfo tmpmap;
					gst_buffer_map(tmpbuf, &tmpmap, GST_MAP_READ | GST_MAP_WRITE);
					dest = tmpmap.data;
					while (1)
					{
						unsigned int pack_len = 0;
						int i;
						for (i = 0; i < self->h264_nal_len_size; i++, pos++)
						{
							pack_len <<= 8;
							pack_len += data[pos];
						}
						memcpy(dest + dest_pos, "\x00\x00\x01", 3);
						dest_pos += 3;
						memcpy(dest + dest_pos, data + pos, pack_len);
						dest_pos += pack_len;
						if ((pos + pack_len) >= data_len) break;
						pos += pack_len;
					}
					/* switch to the h264 buffer, where we copied the original render buffer contents */
					gst_buffer_unmap(buffer, &map);
					gst_buffer_unmap(tmpbuf, &tmpmap);
					buffer = tmpbuf;
					gst_buffer_map(buffer, &map, GST_MAP_READ);
					original_data = data = map.data;
					data_len = dest_pos;
				}
			}
			else if (self->codec_type == CT_MPEG4_PART2)
			{
				if (memcmp(data, "\x00\x00\x01", 3))
				{
					memcpy(pes_header + pes_header_len, "\x00\x00\x01", 3);
					pes_header_len += 3;
				}
			}
			else if (self->codec_type == CT_DIVX311)
			{
				if (memcmp(data, "\x00\x00\x01\xb6", 4))
				{
					memcpy(pes_header + pes_header_len, "\x00\x00\x01\xb6", 4);
					pes_header_len += 4;
				}
			}
		}
	}

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (self->must_pack_bitstream)
	{
		unsigned int pos = 0;
		gboolean i_frame = FALSE;
		while (pos < data_len)
		{
			if (memcmp(&data[pos], "\x00\x00\x01\xb6", 4))
			{
				pos++;
				continue;
			}
			pos += 4;
			switch ((data[pos] & 0xC0) >> 6)
			{
				case 0: // I-Frame
					cache_prev_frame = FALSE;
					i_frame = TRUE;
				case 1: // P-Frame
					if (self->prev_frame != buffer)
					{
						struct bitstream bit;
						gboolean store_frame=FALSE;
						if (self->prev_frame)
						{
							if (!self->num_non_keyframes)
							{
								ret = gst_dvbvideosink_render(sink, self->prev_frame);
								gst_buffer_unref(self->prev_frame);
								self->prev_frame = NULL;
								if (ret != GST_FLOW_OK)
									goto error;
								store_frame = TRUE;
							}
							else
							{
								pes_header[pes_header_len++] = 0;
								pes_header[pes_header_len++] = 0;
								pes_header[pes_header_len++] = 1;
								pes_header[pes_header_len++] = 0xB6;
								bitstream_init(&bit, pes_header+pes_header_len, 1);
								bitstream_put(&bit, 1, 2);
								bitstream_put(&bit, 0, 1);
								bitstream_put(&bit, 1, 1);
								bitstream_put(&bit, self->time_inc, self->time_inc_bits);
								bitstream_put(&bit, 1, 1);
								bitstream_put(&bit, 0, 1);
								bitstream_put(&bit, 0x7F >> bit.avail, 8 - bit.avail);
								data_len = 0;
								pes_header_len += bit.data - (pes_header+pes_header_len);
								cache_prev_frame = TRUE;
							}
						}
						else if (!i_frame)
						{
							store_frame = TRUE;
						}

						self->num_non_keyframes=0;

						// extract time_inc
						bitstream_init(&bit, data+pos, 0);
						bitstream_get(&bit, 2); // skip coding_type
						while (bitstream_get(&bit, 1));
						bitstream_get(&bit, 1);
						self->time_inc = bitstream_get(&bit, self->time_inc_bits);

						if (store_frame)
						{
							self->prev_frame = buffer;
							gst_buffer_ref(buffer);
							goto ok;
						}
					}
					else
					{
						cache_prev_frame = FALSE;
					}
					break;
				case 3: // S-Frame
				case 2: // B-Frame
					if (++self->num_non_keyframes == 1 && self->prev_frame)
					{
						commit_prev_frame_data = TRUE;
					}
					break;
				case 4: // N-Frame
				default:
					g_warning("unhandled divx5/xvid frame type %d\n", (data[pos] & 0xC0) >> 6);
					break;
			}
		}
	}
#endif

	payload_len = data_len + pes_header_len - 6;

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (self->prev_frame && self->prev_frame != buffer)
	{
		pes_set_pts(GST_BUFFER_PTS_IS_VALID(self->prev_frame) ? GST_BUFFER_PTS(self->prev_frame) : GST_BUFFER_DTS(self->prev_frame), pes_header);
	}

	if (commit_prev_frame_data)
	{
		payload_len += gst_buffer_get_size(self->prev_frame);
	}
#endif

	if (self->codec_type == CT_MPEG2 || self->codec_type == CT_MPEG1)
	{
		if (!self->codec_data && data_len > 3 && !memcmp(data, "\x00\x00\x01\xb3", 4))
		{
			gboolean ok = TRUE;
			unsigned int pos = 4;
			unsigned int sheader_data_len = 0;
			while (pos < data_len && ok)
			{
				if (pos >= data_len) break;
				pos += 7;
				if (pos >=data_len) break;
				sheader_data_len = 12;
				if (data[pos] & 2)
				{ // intra matrix
					pos += 64;
					if (pos >=data_len) break;
					sheader_data_len += 64;
				}
				if (data[pos] & 1)
				{ // non intra matrix
					pos += 64;
					if (pos >=data_len) break;
					sheader_data_len += 64;
				}
				pos += 1;
				if (pos + 3 >=data_len) break;
				if (!memcmp(&data[pos], "\x00\x00\x01\xb5", 4))
				{
					// extended start code
					pos += 3;
					sheader_data_len += 3;
					do
					{
						pos += 1;
						++sheader_data_len;
						if (pos + 2 > data_len)
						{
							ok = FALSE;
							break;
						}
					} while (memcmp(&data[pos], "\x00\x00\x01", 3));
					if (!ok) break;
				}
				if (pos + 3 >= data_len) break;
				if (!memcmp(&data[pos], "\x00\x00\x01\xb2", 4))
				{
					// private data
					pos += 3;
					sheader_data_len += 3;
					do
					{
						pos += 1;
						++sheader_data_len;
						if (pos + 2 > data_len)
						{
							ok = FALSE;
							break;
						}
					} while (memcmp(&data[pos], "\x00\x00\x01", 3));
					if (!ok) break;
				}
				self->codec_data = gst_buffer_new_and_alloc(sheader_data_len);
				if (self->codec_data)
				{
					gst_buffer_map(self->codec_data, &codecdatamap, GST_MAP_READ | GST_MAP_WRITE);
					codec_data = codecdatamap.data;
					codec_data_size = codecdatamap.size;
					memcpy(codec_data, data + pos - sheader_data_len, sheader_data_len);
				}
				self->must_send_header = FALSE;
				break;
			}
		}
		else if (self->codec_data && self->must_send_header)
		{
			int pos = 0;
			while (pos <= data_len - 4)
			{
				if (memcmp(&data[pos], "\x00\x00\x01\xb8", 4)) /* find group start code */
				{
					pos++;
					continue;
				}
				payload_len += codec_data_size;
				pes_set_payload_size(payload_len, pes_header);
				if (video_write(sink, self, self->pesheader_buffer, 0, pes_header_len) < 0) goto error;
				if (video_write(sink, self, buffer, data - original_data, (data - original_data) + pos) < 0) goto error;
				if (video_write(sink, self, self->codec_data, 0, codec_data_size) < 0) goto error;
				if (video_write(sink, self, buffer, data - original_data + pos, (data - original_data) + data_len - pos) < 0) goto error;
				self->must_send_header = FALSE;
				goto ok;
			}
		}
	}
	else if (self->codec_type == CT_VC1 || self->codec_type == CT_VC1_SM)
	{
		memcpy(pes_header + pes_header_len, "\x00\x00\x01\x0d", 4);
		pes_header_len += 4;
		payload_len += 4;
	}

	pes_set_payload_size(payload_len, pes_header);

	if (video_write(sink, self, self->pesheader_buffer, 0, pes_header_len) < 0) goto error;

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (commit_prev_frame_data)
	{
		gsize prev_frame_size;
		prev_frame_size = gst_buffer_get_size(self->prev_frame);
		if (video_write(sink, self, self->prev_frame, 0, prev_frame_size) < 0) goto error;
	}

	if (self->prev_frame && self->prev_frame != buffer)
	{
		gst_buffer_unref(self->prev_frame);
		self->prev_frame = NULL;
	}

	if (cache_prev_frame)
	{
		gst_buffer_ref(buffer);
		self->prev_frame = buffer;
	}
#endif
	if (video_write(sink, self, buffer, data - original_data, (data - original_data) + data_len) < 0) goto error;

	if (GST_BUFFER_PTS_IS_VALID(buffer) || (self->use_dts && GST_BUFFER_DTS_IS_VALID(buffer)))
	{
		self->pts_written = TRUE;
	}

ok:
	gst_buffer_unmap(buffer, &map);
	gst_buffer_unmap(self->pesheader_buffer, &pesheadermap);
	if (self->codec_data)
	{
		gst_buffer_unmap(self->codec_data, &codecdatamap);
	}
	if (tmpbuf)
	{
		gst_buffer_unref(tmpbuf);
		tmpbuf = NULL;
	}

	return GST_FLOW_OK;
error:
	gst_buffer_unmap(buffer, &map);
	gst_buffer_unmap(self->pesheader_buffer, &pesheadermap);
	if (self->codec_data)
	{
		gst_buffer_unmap(self->codec_data, &codecdatamap);
	}
#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (self->prev_frame && self->prev_frame != buffer)
	{
		gst_buffer_unref(self->prev_frame);
		self->prev_frame = NULL;
	}
#endif
	if (tmpbuf)
	{
		gst_buffer_unref(tmpbuf);
		tmpbuf = NULL;
	}
	{
		GST_ELEMENT_ERROR(self, RESOURCE, READ, (NULL),
				("video write: %s", g_strerror (errno)));
		GST_WARNING_OBJECT (self, "Video write error");
		return ret == GST_FLOW_OK ? GST_FLOW_ERROR : ret;
	}
}

static gboolean gst_dvbvideosink_set_caps(GstBaseSink *basesink, GstCaps *caps)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK (basesink);
	GstStructure *structure = gst_caps_get_structure (caps, 0);
	const char *mimetype = gst_structure_get_name (structure);
	t_stream_type prev_stream_type = self->stream_type;
	self->stream_type = STREAMTYPE_UNKNOWN;
	self->must_send_header = TRUE;

	GST_INFO_OBJECT (self, "caps = %" GST_PTR_FORMAT, caps);

	if (self->codec_data)
	{
		gst_buffer_unref(self->codec_data);
		self->codec_data = NULL;
	}

	GST_DEBUG_OBJECT(self, "set_caps %" GST_PTR_FORMAT, caps);

	if (!strcmp (mimetype, "video/mpeg"))
	{
		gint mpegversion;
		gst_structure_get_int (structure, "mpegversion", &mpegversion);
		switch (mpegversion)
		{
			case 1:
				self->stream_type = STREAMTYPE_MPEG1;
				self->codec_type = CT_MPEG1;
				GST_INFO_OBJECT (self, "MIMETYPE video/mpeg1 -> STREAMTYPE_MPEG1");
			break;
			case 2:
				self->stream_type = STREAMTYPE_MPEG2;
				self->codec_type = CT_MPEG2;
				GST_INFO_OBJECT (self, "MIMETYPE video/mpeg2 -> STREAMTYPE_MPEG2");
			break;
			case 4:
			{
				self->stream_type = STREAMTYPE_MPEG4_Part2;
				guint32 fourcc = 0;
				const gchar *value = gst_structure_get_string(structure, "fourcc");
				if (value)
					fourcc = GST_STR_FOURCC(value);
				switch (fourcc)
				{
					case GST_MAKE_FOURCC('F', 'M', 'P', '4'):
					case GST_MAKE_FOURCC('R', 'M', 'P', '4'):
					case GST_MAKE_FOURCC('x', 'v', 'i', 'd'):
					case GST_MAKE_FOURCC('X', 'V', 'I', 'D'):
						self->stream_type = STREAMTYPE_XVID;
						self->use_dts = TRUE;
#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
						self->must_pack_bitstream = TRUE;
#endif
					break;
				}
				const GValue *codec_data = gst_structure_get_value(structure, "codec_data");
				if (codec_data)
				{
					GST_INFO_OBJECT (self, "MPEG4 have codec data");
					self->codec_data = gst_value_get_buffer(codec_data);
					gst_buffer_ref (self->codec_data);
				}
				self->codec_type = CT_MPEG4_PART2;
				if (self->stream_type == STREAMTYPE_MPEG4_Part2)
					GST_INFO_OBJECT (self, "MIMETYPE video/mpeg4 -> STREAMTYPE_MPEG4_Part2");
				else
					GST_INFO_OBJECT (self, "MIMETYPE video/xvid -> STREAMTYPE_XVID");
			}
			break;
			default:
				GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL), ("unhandled mpeg version %i", mpegversion));
			break;
		}
	}
	else if (!strcmp (mimetype, "video/x-3ivx"))
	{
		const GValue *codec_data = gst_structure_get_value(structure, "codec_data");
		if (codec_data)
		{
			GST_INFO_OBJECT (self, "have 3ivx codec... handle as CT_MPEG4_PART2");
			self->codec_data = gst_value_get_buffer(codec_data);
			gst_buffer_ref (self->codec_data);
		}
		self->stream_type = STREAMTYPE_MPEG4_Part2;
		self->codec_type = CT_MPEG4_PART2;
		GST_INFO_OBJECT (self, "MIMETYPE video/x-3ivx -> STREAMTYPE_MPEG4_Part2");
	}
	else if (!strcmp (mimetype, "video/x-h264"))
	{
		const GValue *cd_data = gst_structure_get_value(structure, "codec_data");
		self->stream_type = STREAMTYPE_MPEG4_H264;
		self->codec_type = CT_H264;
		if (cd_data)
		{
			unsigned char tmp[2048];
			unsigned int tmp_len = 0;
			GstBuffer *codec_data = gst_value_get_buffer(cd_data);
			guint8 *data;
			gsize cd_len;
			unsigned int cd_pos = 0;
			GstMapInfo codecdatamap;
			gst_buffer_map(codec_data, &codecdatamap, GST_MAP_READ);
			data = codecdatamap.data;
			cd_len = codecdatamap.size;
			GST_INFO_OBJECT (self, "H264 have codec data..!");
			if (cd_len > 7 && data[0] == 1)
			{
				unsigned short len = (data[6] << 8) | data[7];
				if (cd_len >= (len + 8))
				{
					unsigned int i=0;
					uint8_t profile_num[] = { 66, 77, 88, 100 };
					uint8_t profile_cmp[2] = { 0x67, 0x00 };
					const char *profile_str[] = { "baseline", "main", "extended", "high" };
					memcpy(tmp, "\x00\x00\x00\x01", 4);
					tmp_len += 4;
					memcpy(tmp + tmp_len, data + 8, len);
					for (i = 0; i < 4; ++i)
					{
						profile_cmp[1] = profile_num[i];
						if (!memcmp(tmp+tmp_len, profile_cmp, 2))
						{
							uint8_t level_org = tmp[tmp_len + 3];
							if (level_org > 0x29)
							{
								GST_INFO_OBJECT (self, "H264 %s profile@%d.%d patched down to 4.1!", profile_str[i], level_org / 10 , level_org % 10);
								tmp[tmp_len+3] = 0x29; // level 4.1
							}
							else
							{
								GST_INFO_OBJECT (self, "H264 %s profile@%d.%d", profile_str[i], level_org / 10 , level_org % 10);
							}
							break;
						}
					}
					tmp_len += len;
					cd_pos = 8 + len;
					if (cd_len > (cd_pos + 2))
					{
						len = (data[cd_pos + 1] << 8) | data[cd_pos + 2];
						cd_pos += 3;
						if (cd_len >= (cd_pos+len))
						{
							memcpy(tmp+tmp_len, "\x00\x00\x00\x01", 4);
							tmp_len += 4;
							memcpy(tmp+tmp_len, data+cd_pos, len);
							tmp_len += len;
							self->codec_data = gst_buffer_new_and_alloc(tmp_len);
							gst_buffer_fill(self->codec_data, 0, tmp, tmp_len);
							self->h264_nal_len_size = (data[4] & 0x03) + 1;
						}
						else
						{
							GST_WARNING_OBJECT (self, "codec_data too short(4)");
						}
					}
					else
					{
						GST_WARNING_OBJECT (self, "codec_data too short(3)");
					}
				}
				else
				{
					GST_WARNING_OBJECT (self, "codec_data too short(2)");
				}
			}
			else if (cd_len <= 7)
			{
				GST_WARNING_OBJECT (self, "codec_data too short(1)");
			}
			else
			{
				GST_WARNING_OBJECT (self, "wrong avcC version %d!", data[0]);
			}
			gst_buffer_unmap(codec_data, &codecdatamap);
		}
		else
		{
			self->h264_nal_len_size = 0;
		}
		GST_INFO_OBJECT (self, "MIMETYPE video/x-h264 -> STREAMTYPE_MPEG4_H264");
	}
	else if (!strcmp (mimetype, "video/x-h265"))
	{
		const GValue *cd_data = gst_structure_get_value(structure, "codec_data");
		self->stream_type = STREAMTYPE_MPEG4_H265;
		self->codec_type = CT_H265;
		if (cd_data)
		{
			unsigned char tmp[2048];
			unsigned int tmp_len = 0;
			GstBuffer *codec_data = gst_value_get_buffer(cd_data);
			guint8 *data;
			gsize cd_len;
			unsigned int cd_pos = 0;
			GstMapInfo codecdatamap;
			gst_buffer_map(codec_data, &codecdatamap, GST_MAP_READ);
			data = codecdatamap.data;
			cd_len = codecdatamap.size;
			GST_INFO_OBJECT (self, "H265 have codec data..!");

			if (cd_len > 3 && (data[0] || data[1] || data[2] > 1)) {
				if (cd_len > 22) {
					int i;
					if (data[0] != 0) {
						GST_ELEMENT_WARNING (self, STREAM, DECODE, ("Unsupported extra data version %d, decoding may fail", data[0]), (NULL));
					}
					self->h264_nal_len_size = (data[21] & 3) + 1;
					int num_param_sets = data[22];
					int pos = 23;
					for (i = 0; i < num_param_sets; i++) {
						int j;
						if (pos + 3 > cd_len) {
							GST_ELEMENT_ERROR (self, STREAM, DECODE, ("Buffer underrun in extra header (%d >= %ld)", pos + 3, cd_len), (NULL));
							break;
						}
						// ignore flags + NAL type (1 byte)
						int nal_count = data[pos + 1] << 8 | data[pos + 2];
						pos += 3;
						for (j = 0; j < nal_count; j++) {
							if (pos + 2 > cd_len) {
								GST_ELEMENT_ERROR (self, STREAM, DECODE, ("Buffer underrun in extra nal header (%d >= %ld)", pos + 2, cd_len), (NULL));
								break;
							}
							int nal_size = data[pos] << 8 | data[pos + 1];
							pos += 2;
							if (pos + nal_size > cd_len) {
								GST_ELEMENT_ERROR (self, STREAM, DECODE, ("Buffer underrun in extra nal (%d >= %ld)", pos + 2 + nal_size, cd_len), (NULL));
								break;
							}
							memcpy(tmp+tmp_len, "\x00\x00\x00\x01", 4);
							tmp_len += 4;
							memcpy(tmp + tmp_len, data + pos, nal_size);
							tmp_len += nal_size;
							pos += nal_size;
						}
					}
				}
				GST_DEBUG ("Assuming packetized data (%d bytes length)", self->h264_nal_len_size);
				{
					self->codec_data = gst_buffer_new_and_alloc(tmp_len);
					gst_buffer_fill(self->codec_data, 0, tmp, tmp_len);
				}
			}
			gst_buffer_unmap(codec_data, &codecdatamap);
		}
		else
		{
			self->h264_nal_len_size = 0;
		}
		GST_INFO_OBJECT (self, "MIMETYPE video/x-h265 -> STREAMTYPE_MPEG4_H265");
	}
	else if (!strcmp (mimetype, "video/x-h263"))
	{
		self->stream_type = STREAMTYPE_H263;
		self->codec_type = CT_MPEG4_PART2;
		GST_INFO_OBJECT (self, "MIMETYPE video/x-h263 -> STREAMTYPE_H263");
	}
	else if (!strcmp (mimetype, "video/x-xvid"))
	{
		self->stream_type = STREAMTYPE_XVID;
		self->codec_type = CT_MPEG4_PART2;
#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
		self->must_pack_bitstream = TRUE;
#endif
		GST_INFO_OBJECT (self, "MIMETYPE video/x-xvid -> STREAMTYPE_XVID");
	}
	else if (!strcmp (mimetype, "video/x-divx") || !strcmp (mimetype, "video/x-msmpeg"))
	{
		gint divxversion = -1;
		if (!gst_structure_get_int(structure, "divxversion", &divxversion))
		{
			gst_structure_get_int(structure, "msmpegversion", &divxversion);
		}
		switch (divxversion)
		{
			case 3:
			case 43:
			{
				#define B_GET_BITS(w,e,b)  (((w)>>(b))&(((unsigned)(-1))>>((sizeof(unsigned))*8-(e+1-b))))
				#define B_SET_BITS(name,v,e,b)  (((unsigned)(v))<<(b))
				static const guint8 brcm_divx311_sequence_header[] =
				{
					0x00, 0x00, 0x01, 0xE0, 0x00, 0x34, 0x80, 0x80, // PES HEADER
					0x05, 0x2F, 0xFF, 0xFF, 0xFF, 0xFF, 
					0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20, /* 0 .. 7 */
					0x08, 0xC8, 0x0D, 0x40, 0x00, 0x53, 0x88, 0x40, /* 8 .. 15 */
					0x0C, 0x40, 0x01, 0x90, 0x00, 0x97, 0x53, 0x0A, /* 16 .. 24 */
					0x00, 0x00, 0x00, 0x00,
					0x30, 0x7F, 0x00, 0x00, 0x01, 0xB2, 0x44, 0x69, /* 0 .. 7 */
					0x76, 0x58, 0x33, 0x31, 0x31, 0x41, 0x4E, 0x44  /* 8 .. 15 */
				};
				self->codec_data = gst_buffer_new_and_alloc(63);
				guint8 *data;
				gint height, width;
				GstMapInfo map;
				gst_buffer_map(self->codec_data, &map, GST_MAP_WRITE);
				data = map.data;
				gst_structure_get_int (structure, "height", &height);
				gst_structure_get_int (structure, "width", &width);
				memcpy(data, brcm_divx311_sequence_header, 63);
				data += 38;
				data[0] = B_GET_BITS(width,11,4);
				data[1] = B_SET_BITS("width [3..0]", B_GET_BITS(width,3,0), 7, 4) |
					B_SET_BITS("'10'", 0x02, 3, 2) |
					B_SET_BITS("height [11..10]", B_GET_BITS(height,11,10), 1, 0);
				data[2] = B_GET_BITS(height,9,2);
				data[3]= B_SET_BITS("height [1.0]", B_GET_BITS(height,1,0), 7, 6) |
					B_SET_BITS("'100000'", 0x20, 5, 0);
				self->stream_type = STREAMTYPE_DIVX311;
				self->codec_type = CT_DIVX311;
				self->use_dts = TRUE;
				GST_INFO_OBJECT (self, "MIMETYPE video/x-divx vers. 3 -> STREAMTYPE_DIVX311");
				gst_buffer_unmap(self->codec_data, &map);
			}
			break;
			case 4:
				self->stream_type = STREAMTYPE_DIVX4;
				self->codec_type = CT_DIVX4;
				self->codec_data = gst_buffer_new_and_alloc(12);
				gst_buffer_fill(self->codec_data, 0, "\x00\x00\x01\xb2\x44\x69\x76\x58\x34\x41\x4e\x44", 12);
				self->use_dts = TRUE;
				GST_INFO_OBJECT (self, "MIMETYPE video/x-divx vers. 4 -> STREAMTYPE_DIVX4");
			break;
			case 6:
			case 5:
				self->use_dts = TRUE;
				self->stream_type = STREAMTYPE_DIVX5;
				GST_INFO_OBJECT (self, "MIMETYPE video/x-divx vers. %d -> STREAMTYPE_DIVX5", divxversion);
#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
				self->must_pack_bitstream = TRUE;
#endif
			break;
			default:
				GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL), ("unhandled divx version %i", divxversion));
			break;
		}
	}
	else if (!strcmp (mimetype, "video/x-wmv"))
	{
		guint32 fourcc = 0;
		const gchar *value = gst_structure_get_string(structure, "format");
		if (value)
		{
			fourcc = GST_STR_FOURCC(value);
		}
		if (fourcc == GST_MAKE_FOURCC('W', 'V', 'C', '1') || fourcc == GST_MAKE_FOURCC('W', 'M', 'V', 'A'))
		{
			self->stream_type = STREAMTYPE_VC1;
			self->codec_type = CT_VC1;
			GST_INFO_OBJECT (self, "MIMETYPE video/x-wmv %s -> STREAMTYPE_VC1", value);
		}
		else
		{
			self->stream_type = STREAMTYPE_VC1_SM;
			self->codec_type = CT_VC1_SM;
			GST_INFO_OBJECT (self, "MIMETYPE video/x-wmv %s -> STREAMTYPE_VC1_SM", value);
		}
	}

	if (self->stream_type != STREAMTYPE_UNKNOWN)
	{
		gint numerator, denominator;
		if (gst_structure_get_fraction (structure, "framerate", &numerator, &denominator))
		{
			FILE *f = fopen("/proc/stb/vmpeg/0/fallback_framerate", "w");
			if (f)
			{
				int valid_framerates[] = { 23976, 24000, 25000, 29970, 30000, 50000, 59940, 60000 };
				int framerate = (int)(((double)numerator * 1000) / denominator);
				int diff = 60000;
				int best = 0;
				int i = 0;
				for (; i < 7; ++i)
				{
					int ndiff = abs(framerate - valid_framerates[i]);
					if (ndiff < diff)
					{
						diff = ndiff;
						best = i;
					}
				}
				fprintf(f, "%d", valid_framerates[best]);
				fclose(f);
			}
		}
		if (self->playing && self->stream_type != prev_stream_type)
		{
			if (self->fd >= 0) ioctl(self->fd, VIDEO_STOP, 0);
			self->playing = FALSE;
		}
		if (!self->playing && (self->fd < 0 || ioctl(self->fd, VIDEO_SET_STREAMTYPE, self->stream_type) < 0))
		{
#ifdef VIDEO_SET_ENCODING
			unsigned int encoding = streamtype_to_encoding(self->stream_type);
			if (self->fd < 0 || ioctl(self->fd, VIDEO_SET_ENCODING, encoding) < 0)
			{
				GST_ELEMENT_ERROR(self, STREAM, DECODE, (NULL), ("hardware decoder can't be set to encoding %i", encoding));
			}
#endif
		}
		else
		{
			if (self->fd < 0 || ioctl(self->fd, VIDEO_SET_STREAMTYPE, self->stream_type) < 0)
			{
				GST_ELEMENT_ERROR(self, STREAM, CODEC_NOT_FOUND, (NULL), ("hardware decoder can't handle streamtype %i", self->stream_type));
			}
		}
		if (self->fd >= 0) 
		{
			if (self->codec_type == CT_VC1)
			{
				const GValue *codec_data = gst_structure_get_value(structure, "codec_data");
				if (codec_data)
				{
					guint8 *codec_data_pointer;
					gint codec_size;
					guint8 *data;
					video_codec_data_t videocodecdata;
					GstMapInfo codecdatamap;
					gst_buffer_map(gst_value_get_buffer(codec_data), &codecdatamap, GST_MAP_READ);
					codec_data_pointer = codecdatamap.data;
					codec_size = codecdatamap.size;
#if defined(DREAMBOX) || defined(DAGS)
					GstMapInfo map;
					self->codec_data = gst_buffer_new_and_alloc(8 + codec_size);
					gst_buffer_map(self->codec_data, &map, GST_MAP_WRITE);
					data = map.data;
					data += 8;
					if (codec_data && codec_size) memcpy(data , codec_data_pointer, codec_size);
#else
					videocodecdata.length = 8 + codec_size;
					data = videocodecdata.data = (guint8*)g_malloc(videocodecdata.length);
					memset(data, 0, videocodecdata.length);
					data += 8;
					memcpy(data, codec_data_pointer, codec_size);
					ioctl(self->fd, VIDEO_SET_CODEC_DATA, &videocodecdata);
					g_free(videocodecdata.data);
#endif
					gst_buffer_unmap(gst_value_get_buffer(codec_data), &codecdatamap);
#if defined(DREAMBOX) || defined(DAGS)
					gst_buffer_unmap(self->codec_data, &map);
#endif
				}
			}
			else if (self->codec_type == CT_VC1_SM)
			{
				const GValue *codec_data = gst_structure_get_value(structure, "codec_data");
				if (codec_data)
				{
					guint8 *codec_data_pointer;
					gint codec_size;
					guint8 *data;
					video_codec_data_t videocodecdata;
					gint width, height;
					GstMapInfo codecdatamap;
					gst_buffer_map(gst_value_get_buffer(codec_data), &codecdatamap, GST_MAP_READ);
					codec_data_pointer = codecdatamap.data;
					codec_size = codecdatamap.size;
					if (codec_size > 4) codec_size = 4;
					gst_structure_get_int(structure, "width", &width);
					gst_structure_get_int(structure, "height", &height);
#if defined(DREAMBOX) || defined(DAGS)
					GstMapInfo map;
					self->codec_data = gst_buffer_new_and_alloc(18 + codec_size);
					gst_buffer_map(self->codec_data, &map, GST_MAP_WRITE);
					data = map.data;
					/* pes header */
					*(data++) = 0x00;
					*(data++) = 0x00;
					*(data++) = 0x01;
					*(data++) = 0x0f;
					/* width */
					*(data++) = (width >> 8) & 0xff;
					*(data++) = width & 0xff;
					/* height */
					*(data++) = (height >> 8) & 0xff;
					*(data++) = height & 0xff;
					if (codec_data && codec_size) memcpy(data, codec_data_pointer, codec_size);
#else
					videocodecdata.length = 33;
					data = videocodecdata.data = (guint8*)g_malloc(videocodecdata.length);
					memset(data, 0, videocodecdata.length);
					data += 18;
					/* width */
					*(data++) = (width >> 8) & 0xff;
					*(data++) = width & 0xff;
					/* height */
					*(data++) = (height >> 8) & 0xff;
					*(data++) = height & 0xff;
					if (codec_data && codec_size) memcpy(data, codec_data_pointer, codec_size);
					ioctl(self->fd, VIDEO_SET_CODEC_DATA, &videocodecdata);
					g_free(videocodecdata.data);
#endif
					gst_buffer_unmap(gst_value_get_buffer(codec_data), &codecdatamap);
#if defined(DREAMBOX) || defined(DAGS)
					gst_buffer_unmap(self->codec_data, &map);
#endif
				}
			}
			if (!self->playing)
				ioctl(self->fd, VIDEO_PLAY);
		}
		self->playing = TRUE;
	}
	else
	{
		GST_ELEMENT_ERROR (self, STREAM, TYPE_NOT_FOUND, (NULL), ("unimplemented stream type %s", mimetype));
	}

	return TRUE;
}

static gboolean gst_dvbvideosink_start(GstBaseSink *basesink)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK(basesink);
	FILE *f = NULL;

	GST_DEBUG_OBJECT(self, "start");

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, self->unlockfd) < 0)
	{
		perror("socketpair");
		goto error;
	}

	fcntl(self->unlockfd[0], F_SETFL, O_NONBLOCK);
	fcntl(self->unlockfd[1], F_SETFL, O_NONBLOCK);

	self->pesheader_buffer = gst_buffer_new_and_alloc(2048);

	f = fopen("/proc/stb/vmpeg/0/fallback_framerate", "r");
	if (f)
	{
		fgets(self->saved_fallback_framerate, sizeof(self->saved_fallback_framerate), f);
		fclose(f);
		f = NULL;
	}

	self->fd = open("/dev/dvb/adapter0/video0", O_RDWR | O_NONBLOCK);

	self->pts_written = FALSE;
	self->lastpts = 0;

	return TRUE;
error:
	{
		GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE, (NULL),
				GST_ERROR_SYSTEM);
		return FALSE;
	}
}

static gboolean gst_dvbvideosink_stop(GstBaseSink *basesink)
{
	GstDVBVideoSink *self = GST_DVBVIDEOSINK(basesink);
	FILE *f = NULL;
	GST_INFO_OBJECT(self, "stop");
	if (self->fd >= 0)
	{
		if (self->playing)
		{
			ioctl(self->fd, VIDEO_STOP);
			self->playing = FALSE;
		}
		if (self->rate != 1.0)
		{
			ioctl(self->fd, VIDEO_SLOWMOTION, 0);
			ioctl(self->fd, VIDEO_FAST_FORWARD, 0);
			self->rate = 1.0;
		}
		ioctl(self->fd, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_DEMUX);
		close(self->fd);
		self->fd = -1;
	}

	if (self->codec_data)
	{
		gst_buffer_unref(self->codec_data);
		self->codec_data = NULL;
	}

	if (self->pesheader_buffer)
	{
		gst_buffer_unref(self->pesheader_buffer);
		self->pesheader_buffer = NULL;
	}

#ifdef PACK_UNPACKED_XVID_DIVX5_BITSTREAM
	if (self->prev_frame)
	{
		gst_buffer_unref(self->prev_frame);
		self->prev_frame = NULL;
	}
#endif

	while (self->queue)
	{
		queue_pop(&self->queue);
	}

	f = fopen("/proc/stb/vmpeg/0/fallback_framerate", "w");
	if (f)
	{
		fputs(self->saved_fallback_framerate, f);
		fclose(f);
		f = NULL;
	}

	/* close write end first */
	if (self->unlockfd[1] >= 0)
	{
		close(self->unlockfd[1]);
		self->unlockfd[1] = -1;
	}
	if (self->unlockfd[0] >= 0)
	{
		close(self->unlockfd[0]);
		self->unlockfd[0] = -1;
	}
	return TRUE;
}

static GstStateChangeReturn gst_dvbvideosink_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstDVBVideoSink *self = GST_DVBVIDEOSINK (element);
	FILE *f;

	switch (transition)
	{
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_NULL_TO_READY");
		self->ok_to_write = 1;
		break;
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_READY_TO_PAUSED");
		self->paused = TRUE;
		if (self->fd >= 0)
		{
			ioctl(self->fd, VIDEO_SELECT_SOURCE, VIDEO_SOURCE_MEMORY);
			ioctl(self->fd, VIDEO_FREEZE);
		}
		if(get_downmix_ready())
			self->using_dts_downmix = TRUE;
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_PAUSED_TO_PLAYING");
		if (self->fd >= 0) ioctl(self->fd, VIDEO_CONTINUE);
		self->paused = FALSE;
		break;
	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

	switch (transition)
	{
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_PLAYING_TO_PAUSED");
		self->paused = TRUE;
		if (self->fd >= 0) ioctl(self->fd, VIDEO_FREEZE);
		/* wakeup the poll */
		write(self->unlockfd[1], "\x01", 1);
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_PAUSED_TO_READY");
		break;
	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_INFO_OBJECT (self,"GST_STATE_CHANGE_READY_TO_NULL");
		break;
	default:
		break;
	}

	return ret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 *
 * exchange the string 'plugin' with your elemnt name
 */
static gboolean plugin_init (GstPlugin *plugin)
{
	gst_debug_set_colored(GST_DEBUG_COLOR_MODE_OFF);
	return gst_element_register (plugin, "dvbvideosink",
						 GST_RANK_PRIMARY,
						 GST_TYPE_DVBVIDEOSINK);
}

/* this is the structure that gstreamer looks for to register plugins
 *
 * exchange the strings 'plugin' and 'Template plugin' with you plugin name and
 * description
 */
GST_PLUGIN_DEFINE (
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	dvbvideosink,
	"DVB Video Output",
	plugin_init,
	VERSION,
	"LGPL",
	"GStreamer",
	"http://gstreamer.net/"
)
