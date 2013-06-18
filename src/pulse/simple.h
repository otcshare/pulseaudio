#ifndef foosimplehfoo
#define foosimplehfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <sys/types.h>

#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/def.h>
#include <pulse/cdecl.h>
#include <pulse/version.h>

#include <pulse/proplist.h>
/** \page simple Simple API
 *
 * \section overv_sec Overview
 *
 * The simple API is designed for applications with very basic sound
 * playback or capture needs. It can only support a single stream per
 * connection and has no support for handling of complex features like
 * events, channel mappings and volume control. It is, however, very simple
 * to use and quite sufficient for many programs.
 *
 * \section conn_sec Connecting
 *
 * The first step before using the sound system is to connect to the
 * server. This is normally done this way:
 *
 * \code
 * pa_simple *s;
 * pa_sample_spec ss;
 *
 * ss.format = PA_SAMPLE_S16NE;
 * ss.channels = 2;
 * ss.rate = 44100;
 *
 * s = pa_simple_new(NULL,               // Use the default server.
 *                   "Fooapp",           // Our application's name.
 *                   PA_STREAM_PLAYBACK,
 *                   NULL,               // Use the default device.
 *                   "Music",            // Description of our stream.
 *                   &ss,                // Our sample format.
 *                   NULL,               // Use default channel map
 *                   NULL,               // Use default buffering attributes.
 *                   NULL,               // Ignore error code.
 *                   );
 * \endcode
 *
 * At this point a connected object is returned, or NULL if there was a
 * problem connecting.
 *
 * \section transfer_sec Transferring data
 *
 * Once the connection is established to the server, data can start flowing.
 * Using the connection is very similar to the normal read() and write()
 * system calls. The main difference is that they're called pa_simple_read()
 * and pa_simple_write(). Note that these operations always block.
 *
 * \section ctrl_sec Buffer control
 *
 * If a playback stream is used then a few other operations are available:
 *
 * \li pa_simple_drain() - Will wait for all sent data to finish playing.
 * \li pa_simple_flush() - Will throw away all data currently in buffers.
 * \li pa_simple_get_latency() - Will return the total latency of
 *                               the playback pipeline.
 *
 * \section cleanup_sec Cleanup
 *
 * Once playback or capture is complete, the connection should be closed
 * and resources freed. This is done through:
 *
 * \code
 * pa_simple_free(s);
 * \endcode
 */

/** \file
 * A simple but limited synchronous playback and recording
 * API. This is a synchronous, simplified wrapper around the standard
 * asynchronous API.
 *
 * See also \subpage simple
 */

/** \example pacat-simple.c
 * A simple playback tool using the simple API */

/** \example parec-simple.c
 * A simple recording tool using the simple API */

PA_C_DECL_BEGIN

/** \struct pa_simple
 * An opaque simple connection object */
typedef struct pa_simple pa_simple;

/** Create a new connection to the server. */
pa_simple* pa_simple_new(
    const char *server,                 /**< Server name, or NULL for default */
    const char *name,                   /**< A descriptive name for this client (application name, ...) */
    pa_stream_direction_t dir,          /**< Open this stream for recording or playback? */
    const char *dev,                    /**< Sink (resp. source) name, or NULL for default */
    const char *stream_name,            /**< A descriptive name for this stream (application name, song title, ...) */
    const pa_sample_spec *ss,           /**< The sample type to use */
    const pa_channel_map *map,          /**< The channel map to use, or NULL for default */
    const pa_buffer_attr *attr,         /**< Buffering attributes, or NULL for default */
    int *error                          /**< A pointer where the error code is stored when the routine returns NULL. It is OK to pass NULL here. */
    );

/** Create a new connection to the server with proplist */
pa_simple* pa_simple_new_proplist(
    const char *server,                 /**< Server name, or NULL for default */
    const char *name,                   /**< A descriptive name for this client (application name, ...) */
    pa_stream_direction_t dir,          /**< Open this stream for recording or playback? */
    const char *dev,                    /**< Sink (resp. source) name, or NULL for default */
    const char *stream_name,            /**< A descriptive name for this client (application name, song title, ...) */
    const pa_sample_spec *ss,           /**< The sample type to use */
    const pa_channel_map *map,          /**< The channel map to use, or NULL for default */
    const pa_buffer_attr *attr,         /**< Buffering attributes, or NULL for default */
    pa_proplist *proplist,	/**< Properties, or NULL for default */
    int *error                          /**< A pointer where the error code is stored when the routine returns NULL. It is OK to pass NULL here. */
    );
/** Close and free the connection to the server. The connection object becomes invalid when this is called. */
void pa_simple_free(pa_simple *s);

/** Write some data to the server. */
int pa_simple_write(pa_simple *s, const void *data, size_t bytes, int *error);

/** Wait until all data already written is played by the daemon. */
int pa_simple_drain(pa_simple *s, int *error);

/** Read some data from the server. */
int pa_simple_read(pa_simple *s, void *data, size_t bytes, int *error);

/** Return the playback latency. */
pa_usec_t pa_simple_get_latency(pa_simple *s, int *error);

/** Flush the playback buffer. This discards any audio in the buffer. */
int pa_simple_flush(pa_simple *s, int *error);
/** Mute the playback stream */
int pa_simple_mute(pa_simple *p, int mute, int *rerror);

/** Volume control the playback stream */
int pa_simple_set_volume(pa_simple *p, int volume, int *rerror);

/** Get stream index */
int pa_simple_get_stream_index(pa_simple *p, unsigned int *idx, int *rerror);

/** Cork on=1/off=0 stream */
int pa_simple_cork(pa_simple *p, int cork, int *rerror);

/** Check whether stream is corked or not */
int pa_simple_is_corked(pa_simple *p);

PA_C_DECL_END

#endif
