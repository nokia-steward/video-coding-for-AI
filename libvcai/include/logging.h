// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

namespace sf {
    /**
     * @file logging.h
     * @brief Provides convenient logging functions that wrap GStreamer debug macros.
     *
     * These functions provide a consistent logging interface across the libvcai
     * library and applications that use it. They wrap GStreamer's debug macros with
     * convenient C++ function signatures.
     */

    /**
     * @enum log_level
     * @brief Defines log levels for status reporting and logging.
     */
    enum class log_level { error, warning, info };

    /**
     * @brief Log an error message.
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void error(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_ERROR(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a warning message.
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void warning(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_WARNING(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log an info message.
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void info(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_INFO(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a debug message.
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void debug(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_DEBUG(category, format, args...);
        }
        // Debug messages with null category are silently ignored
    }

    /**
     * @brief Log a log message (level 1).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void log(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_LOG(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a fixme message (for TODO items).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void fixme(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_FIXME(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a trace message (for function entry/exit).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void trace(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_TRACE(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a memory dump message (for memory operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param message The message string.
     * @param data The data to dump.
     * @param length The length of the data.
     */
    inline void memory_dump(GstDebugCategory *category, const char *message, const guint8 *data, const guint length) {
        if (category != nullptr) {
            GST_CAT_MEMDUMP(category, message, data, length);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a clock message (for timing operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void clock(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_CLOCK(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a buffer message (for buffer operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void buffer(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_BUFFER(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a buffer list message (for buffer list operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void buffer_list(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_BUFFER_LIST(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a caps message (for caps operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void caps(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_CAPS(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a context message (for context operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void context(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_CONTEXT(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a device message (for device operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void device(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_DEVICE(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a pad message (for pad operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void pad(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_PAD(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a pipeline message (for pipeline operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void pipeline(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_PIPELINE(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a qos message (for QoS operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void qos(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_QOS(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a refcount message (for reference counting operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void refcount(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_REFCOUNT(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a state message (for state change operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void state(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_STATE(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a states message (for state change operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void states(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_STATES(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a sync message (for synchronization operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void sync(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_SYNC(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

    /**
     * @brief Log a thread message (for threading operations).
     * @param category The debug category (e.g., GST_CAT_DEFAULT).
     * @param format The format string (printf-style).
     * @param args Variable arguments for the format string.
     */
    template<typename... Args>
    void thread(GstDebugCategory *category, const char *format, Args... args) {
        if (category != nullptr) {
            GST_CAT_THREAD(category, format, args...);
        }
        // If category is NULL, skip logging to avoid assertion failure
    }

} // namespace sf
