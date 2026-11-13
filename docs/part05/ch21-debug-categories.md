# Chapter 21 — libvcai debug categories

From [**libvcai/README.md**](../../libvcai/README.md):

- `init_nal_utils_debug_category()`
- `init_area_copy_debug_category()`
- `init_background_blur_debug_category()`
- `init_bitrate_calculation_debug_category()`

Call after `gst_init()` in modules that define dedicated categories. Controlled via the GStreamer `GST_DEBUG` environment variable.
