#ifndef fooextensionhfoo
#define fooextensionhfoo

/***
  This file is part of PulseAudio.

  Copyright 2014 Intel Corporation

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

#include <pulse/context.h>

#include <pulsecore/tagstruct.h>

#include <stdbool.h>

typedef struct pa_extension pa_extension;

struct pa_extension {
    pa_context *context;
    char *name;
    bool unlinked;

    /* This is called when the context state changes. The callback is called
     * twice for each state change, first with phase = 1 and then with
     * phase = 2. In the first phase the extension should update its internal
     * state without calling any application callbacks. In the second phase it
     * should call the application callbacks (if any). May be NULL. */
    void (*context_state_changed)(pa_extension *extension, unsigned phase);

    /* Called from pa_extension_kill(). May not be NULL. */
    void (*kill)(pa_extension *extension);

    /* Called from pa_extension_process_command(). May be NULL, if the
     * extension doesn't expect any commands from the server. */
    void (*process_command)(pa_extension *extension, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct);

    void *userdata;
};

pa_extension *pa_extension_new(pa_context *context, const char *name);
void pa_extension_put(pa_extension *extension);
void pa_extension_free(pa_extension *extension);

void pa_extension_context_state_changed(pa_extension *extension, unsigned phase);
void pa_extension_kill(pa_extension *extension);
void pa_extension_process_command(pa_extension *extension, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct);

#endif
