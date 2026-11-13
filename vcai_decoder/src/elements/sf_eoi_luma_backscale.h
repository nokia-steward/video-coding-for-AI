// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define SF_TYPE_EOI_LUMA_BACKSCALE (sf_eoi_luma_backscale_get_type())
G_DECLARE_FINAL_TYPE(sf_eoi_luma_backscale, sf_eoi_luma_backscale, SF, EOI_LUMA_BACKSCALE, GstBaseTransform)

G_END_DECLS
