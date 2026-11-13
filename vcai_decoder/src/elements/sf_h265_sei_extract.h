// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define SF_TYPE_H265_SEI_EXTRACT (sf_h265_sei_extract_get_type())
G_DECLARE_FINAL_TYPE(sf_h265_sei_extract, sf_h265_sei_extract, SF, H265_SEI_EXTRACT, GstBaseTransform)

G_END_DECLS
