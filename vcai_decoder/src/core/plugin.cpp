// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "elements/sf_eoi_luma_backscale.h"
#include "elements/sf_h265_sei_extract.h"
#include "elements/sf_pri_reconstruct.h"
#include "elements/sf_tet_apply.h"

#include <gst/gst.h>

#ifndef PACKAGE
#define PACKAGE "vcai_decoder"
#endif

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.1.0"
#endif

#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "VCAI Decoder Plugin"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "TODO"
#endif

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "sfh265seiextract", GST_RANK_NONE, SF_TYPE_H265_SEI_EXTRACT)) {
        return FALSE;
    }

    if (!gst_element_register(plugin, "sfprireconstruct", GST_RANK_NONE, SF_TYPE_PRI_RECONSTRUCT)) {
        return FALSE;
    }
    if (!gst_element_register(plugin, "sfeoilumabackscale", GST_RANK_NONE, SF_TYPE_EOI_LUMA_BACKSCALE)) {
        return FALSE;
    }
    if (!gst_element_register(plugin, "sftetapply", GST_RANK_NONE, SF_TYPE_TET_APPLY)) {
        return FALSE;
    }

    return TRUE;
}

extern "C" {

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, vcai_decoder, "VCAI decoder helper elements",
                  plugin_init, PACKAGE_VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
}
