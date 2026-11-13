// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define SF_TYPE_PRI_RECONSTRUCT (sf_pri_reconstruct_get_type())
G_DECLARE_FINAL_TYPE(sf_pri_reconstruct, sf_pri_reconstruct, SF, PRI_RECONSTRUCT, GstBaseTransform)

G_END_DECLS
