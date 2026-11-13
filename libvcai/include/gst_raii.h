// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gio/gio.h> // For GSocket
#include <glib/gkeyfile.h>
#include <gst/gst.h>
#include <memory>

namespace sf {
    /**
     * @file gst_raii.h
     * @brief Provides RAII wrappers and helpers for GStreamer and GLib C-style objects.
     */

    /**
     * @struct glib_char_deleter
     * @brief A custom deleter for gchar pointers allocated by GLib functions.
     */
    struct glib_char_deleter {
        /**
         * @brief Free a GLib-allocated string.
         * @param str String pointer to release (may be null).
         */
        void operator()(gchar *str) const {
            if (str) {
                g_free(str);
            }
        }
    };

    /**
     * @struct glib_error_deleter
     * @brief A custom deleter for GError objects.
     */
    struct glib_error_deleter {
        /**
         * @brief Free a GError instance.
         * @param err Error pointer to release (may be null).
         */
        void operator()(GError *err) const {
            if (err) {
                g_error_free(err);
            }
        }
    };

    /**
     * @struct glib_option_context_deleter
     * @brief A custom deleter for GOptionContext objects.
     */
    struct glib_option_context_deleter {
        /**
         * @brief Free a GOptionContext instance.
         * @param context Context pointer to release (may be null).
         */
        void operator()(GOptionContext *context) const {
            if (context) {
                g_option_context_free(context);
            }
        }
    };

    /**
     * @struct glib_key_file_deleter
     * @brief A custom deleter for GKeyFile objects.
     */
    struct glib_key_file_deleter {
        /**
         * @brief Free a GKeyFile instance.
         * @param key_file Key file pointer to release (may be null).
         */
        void operator()(GKeyFile *key_file) const {
            if (key_file) {
                g_key_file_free(key_file);
            }
        }
    };

    /**
     * @struct gst_object_deleter
     * @brief A custom deleter for GStreamer objects that are freed with gst_object_unref.
     */
    struct gst_object_deleter {
        /**
         * @brief Unref a GStreamer object.
         * @param obj Object pointer to release (may be null).
         */
        void operator()(gpointer obj) const {
            if (obj) {
                gst_object_unref(obj);
            }
        }
    };

    /**
     * @struct no_delete_deleter
     * @brief A custom deleter that does nothing - for objects managed by other systems.
     */
    struct no_delete_deleter {
        /**
         * @brief No-op deleter for externally managed objects.
         * @param obj Object pointer (unused).
         */
        void operator()([[maybe_unused]] gpointer obj) const {
            // Do nothing - object is managed by another system (e.g., GStreamer pipeline)
        }
    };

    /**
     * @struct gst_message_deleter
     * @brief A custom deleter for GstMessage objects.
     */
    struct gst_message_deleter {
        /**
         * @brief Unref a GstMessage instance.
         * @param msg Message pointer to release (may be null).
         */
        void operator()(GstMessage *msg) const {
            if (msg) {
                gst_message_unref(msg);
            }
        }
    };

    /**
     * @struct gst_sample_deleter
     * @brief A custom deleter for GstSample objects (which are GstMiniObjects).
     */
    struct gst_sample_deleter {
        /**
         * @brief Unref a GstSample instance.
         * @param sample Sample pointer to release (may be null).
         */
        void operator()(GstSample *sample) const {
            if (sample) {
                gst_sample_unref(sample);
            }
        }
    };

    /**
     * @struct gst_buffer_deleter
     * @brief A custom deleter for GstBuffer objects.
     */
    struct gst_buffer_deleter {
        /**
         * @brief Unref a GstBuffer instance.
         * @param buffer Buffer pointer to release (may be null).
         */
        void operator()(GstBuffer *buffer) const {
            if (buffer) {
                gst_buffer_unref(buffer);
            }
        }
    };

    /**
     * @struct gst_caps_deleter
     * @brief A custom deleter for GstCaps objects.
     */
    struct gst_caps_deleter {
        /**
         * @brief Unref a GstCaps instance.
         * @param caps Caps pointer to release (may be null).
         */
        void operator()(GstCaps *caps) const {
            if (caps) {
                gst_caps_unref(caps);
            }
        }
    };

    /**
     * @struct glib_main_loop_deleter
     * @brief A custom deleter for GMainLoop objects.
     */
    struct glib_main_loop_deleter {
        /**
         * @brief Unref a GMainLoop instance.
         * @param loop Loop pointer to release (may be null).
         */
        void operator()(GMainLoop *loop) const {
            if (loop) {
                g_main_loop_unref(loop);
            }
        }
    };

    /**
     * @struct glib_socket_deleter
     * @brief A custom deleter for GSocket objects.
     */
    struct glib_socket_deleter {
        /**
         * @brief Unref a GSocket instance.
         * @param socket Socket pointer to release (may be null).
         */
        void operator()(GSocket *socket) const {
            if (socket) {
                g_object_unref(socket);
            }
        }
    };

    /**
     * @struct glib_socket_service_deleter
     * @brief A custom deleter for GSocketService objects.
     */
    struct glib_socket_service_deleter {
        /**
         * @brief Unref a GSocketService instance.
         * @param service Service pointer to release (may be null).
         */
        void operator()(GSocketService *service) const {
            if (service) {
                g_object_unref(service);
            }
        }
    };

    /**
     * @struct glib_socket_address_deleter
     * @brief A custom deleter for GSocketAddress objects.
     */
    struct glib_socket_address_deleter {
        /**
         * @brief Unref a GSocketAddress instance.
         * @param address Address pointer to release (may be null).
         */
        void operator()(GSocketAddress *address) const {
            if (address) {
                g_object_unref(address);
            }
        }
    };

    /**
     * @struct glib_inet_address_deleter
     * @brief A custom deleter for GInetAddress objects.
     */
    struct glib_inet_address_deleter {
        /**
         * @brief Unref a GInetAddress instance.
         * @param address Inet address pointer to release (may be null).
         */
        void operator()(GInetAddress *address) const {
            if (address) {
                g_object_unref(address);
            }
        }
    };

    /**
     * @struct glib_source_deleter
     * @brief A custom deleter for GSource objects.
     */
    struct glib_source_deleter {
        /**
         * @brief Unref a GSource instance.
         * @param source Source pointer to release (may be null).
         */
        void operator()(GSource *source) const {
            if (source) {
                g_source_unref(source);
            }
        }
    };

    /**
     * @struct gst_structure_deleter
     * @brief A custom deleter for GstStructure objects.
     */
    struct gst_structure_deleter {
        /**
         * @brief Free a GstStructure instance.
         * @param structure Structure pointer to release (may be null).
         */
        void operator()(GstStructure *structure) const {
            if (structure) {
                gst_structure_free(structure);
            }
        }
    };

    /**
     * @struct unique_gst_buffer_unmapper
     * @brief An RAII guard to automatically unmap a GstBuffer.
     *
     * This struct takes a GstBuffer and its corresponding GstMapInfo on
     * construction and calls gst_buffer_unmap in its destructor, ensuring
     * the buffer is always correctly unmapped even in the case of exceptions.
     */
    struct unique_gst_buffer_unmapper {
        /**
         * @brief Constructs the unmapper guard.
         * @param buf The GstBuffer to unmap on scope exit.
         * @param map The GstMapInfo associated with the mapping.
         */
        unique_gst_buffer_unmapper(GstBuffer *buf, GstMapInfo *map) : buffer(buf), map_info(map) {}

        /**
         * @brief Destructor that performs the gst_buffer_unmap call.
         */
        ~unique_gst_buffer_unmapper() {
            if (buffer && map_info) {
                gst_buffer_unmap(buffer, map_info);
            }
        }

        unique_gst_buffer_unmapper(const unique_gst_buffer_unmapper &) = delete;
        unique_gst_buffer_unmapper &operator=(const unique_gst_buffer_unmapper &) = delete;
        unique_gst_buffer_unmapper(unique_gst_buffer_unmapper &&) = delete;
        unique_gst_buffer_unmapper &operator=(unique_gst_buffer_unmapper &&) = delete;

    private:
        GstBuffer *buffer;
        GstMapInfo *map_info;
    };

    using unique_g_char_ptr = std::unique_ptr<gchar, glib_char_deleter>;
    using unique_g_error_ptr = std::unique_ptr<GError, glib_error_deleter>;
    using unique_g_option_context_ptr = std::unique_ptr<GOptionContext, glib_option_context_deleter>;
    using unique_g_key_file_ptr = std::unique_ptr<GKeyFile, glib_key_file_deleter>;
    using unique_g_main_loop_ptr = std::unique_ptr<GMainLoop, glib_main_loop_deleter>;
    using unique_gsocket_ptr = std::unique_ptr<GSocket, glib_socket_deleter>;
    using unique_g_socket_service_ptr = std::unique_ptr<GSocketService, glib_socket_service_deleter>;
    using unique_g_socket_address_ptr = std::unique_ptr<GSocketAddress, glib_socket_address_deleter>;
    using unique_g_inet_address_ptr = std::unique_ptr<GInetAddress, glib_inet_address_deleter>;
    using unique_g_source_ptr = std::unique_ptr<GSource, glib_source_deleter>;
    using unique_gst_element_ptr = std::unique_ptr<GstElement, gst_object_deleter>;
    using unique_gst_element_no_delete_ptr =
            std::unique_ptr<GstElement, no_delete_deleter>; // For elements managed by pipelines
    using unique_gst_bus_ptr = std::unique_ptr<GstBus, gst_object_deleter>;
    using unique_gst_message_ptr = std::unique_ptr<GstMessage, gst_message_deleter>;
    using unique_gst_pad_ptr = std::unique_ptr<GstPad, gst_object_deleter>;
    using unique_gst_sample_ptr = std::unique_ptr<GstSample, gst_sample_deleter>;
    using unique_gst_buffer_ptr = std::unique_ptr<GstBuffer, gst_buffer_deleter>;
    using unique_gst_buffer_no_delete_ptr =
            std::unique_ptr<GstBuffer, no_delete_deleter>; // For buffers managed by GStreamer pipelines
    using unique_gst_caps_ptr = std::unique_ptr<GstCaps, gst_caps_deleter>;
    using unique_gst_caps_no_delete_ptr =
            std::unique_ptr<GstCaps, no_delete_deleter>; // For caps managed by GStreamer pipelines
    using unique_gst_structure_ptr = std::unique_ptr<GstStructure, gst_structure_deleter>;
} // namespace sf
