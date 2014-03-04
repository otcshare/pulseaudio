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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "extension.h"

#include <pulsecore/macro.h>

#include <pulse/internal.h>
#include <pulse/xmalloc.h>

pa_extension *pa_extension_new(pa_context *context, const char *name) {
    pa_extension *extension = NULL;

    pa_assert(context);
    pa_assert(name);

    extension = pa_xnew0(pa_extension, 1);
    extension->context = context;
    extension->name = pa_xstrdup(name);

    return extension;
}

void pa_extension_put(pa_extension *extension) {
    pa_assert(extension);
    pa_assert(extension->kill);

    pa_context_add_extension(extension->context, extension);
}

static void extension_unlink(pa_extension *extension) {
    pa_assert(extension);

    if (extension->unlinked)
        return;

    extension->unlinked = true;

    pa_context_remove_extension(extension->context, extension);
}

void pa_extension_free(pa_extension *extension) {
    pa_assert(extension);

    extension_unlink(extension);

    pa_xfree(extension->name);
    pa_xfree(extension);
}

void pa_extension_context_state_changed(pa_extension *extension, unsigned phase) {
    pa_assert(extension);
    pa_assert(phase == 1 || phase == 2);

    if (extension->context_state_changed)
        extension->context_state_changed(extension, phase);
}

void pa_extension_kill(pa_extension *extension) {
    pa_assert(extension);

    if (extension->unlinked)
        return;

    extension->kill(extension);
}

void pa_extension_process_command(pa_extension *extension, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct) {
    pa_assert(extension);
    pa_assert(tagstruct);

    if (extension->process_command)
        extension->process_command(extension, command, tag, tagstruct);
    else {
        pa_log("Unexpected command for extension %s: %u", extension->name, command);
        pa_context_fail(extension->context, PA_ERR_PROTOCOL);
    }
}
