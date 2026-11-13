// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define SF_TYPE_TET_APPLY (sf_tet_apply_get_type())
G_DECLARE_FINAL_TYPE(sf_tet_apply, sf_tet_apply, SF, TET_APPLY, GstBaseTransform)

G_END_DECLS
