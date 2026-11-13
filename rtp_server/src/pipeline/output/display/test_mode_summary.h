#pragma once

#include <string>
#include <vector>

#include <gst/gst.h>

#include "test_mode.h"

[[nodiscard]] std::string format_test_mode_summary_markdown(const test_mode_run_summary &summary,
                                                            const std::vector<test_mode_frame_result> &frame_results,
                                                            guint16 port);

[[nodiscard]] std::string format_test_mode_summary_json(const test_mode_run_summary &summary,
                                                        const std::vector<test_mode_frame_result> &frame_results,
                                                        guint16 port);

[[nodiscard]] std::string
format_semseg_test_mode_summary_markdown(const semseg_test_mode_run_summary &summary,
                                         const std::vector<semseg_test_mode_frame_result> &frame_results, guint16 port);

[[nodiscard]] std::string
format_semseg_test_mode_summary_json(const semseg_test_mode_run_summary &summary,
                                     const std::vector<semseg_test_mode_frame_result> &frame_results, guint16 port);

[[nodiscard]] bool write_test_mode_summary_file(const std::string &path, const std::string &contents,
                                                GstDebugCategory *log_category);
