// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>

/**
 * @file mode_factory_test_helpers.height
 * @brief Shared assertions/utilities for mode-factory tests.
 */

/**
 * @brief Finds a context by mode name.
 * @tparam ContextCollection Context container type.
 * @tparam NameGetter Callable: std::string_view(const ContextType &).
 * @param contexts Context collection.
 * @param target_name Mode name to find.
 * @param name_getter Name extraction callable.
 * @return Pointer to matching context, or nullptr if not found.
 */
template<typename ContextCollection, typename NameGetter>
[[nodiscard]] const typename ContextCollection::value_type *find_context_by_name(const ContextCollection &contexts,
                                                                                 const std::string_view target_name,
                                                                                 const NameGetter &name_getter) {
    for (const auto &context: contexts) {
        if (name_getter(context) == target_name) {
            return &context;
        }
    }
    return nullptr;
}

/**
 * @brief Asserts that the expected mode names are present and count matches.
 * @tparam ContextCollection Context container type.
 * @tparam NameGetter Callable: std::string_view(const ContextType &).
 * @param contexts Context collection.
 * @param expected_names Expected mode names.
 * @param name_getter Name extraction callable.
 */
template<typename ContextCollection, typename NameGetter>
void expect_contexts_created(const ContextCollection &contexts,
                             const std::initializer_list<std::string_view> expected_names,
                             const NameGetter &name_getter) {
    ASSERT_EQ(contexts.size(), expected_names.size()) << "Unexpected number of factory contexts";
    for (const std::string_view expected_name: expected_names) {
        const auto *context = find_context_by_name(contexts, expected_name, name_getter);
        ASSERT_NE(context, nullptr) << "Missing expected mode: " << expected_name;
    }
}

/**
 * @brief Asserts that exactly one context is active for given data.
 * @tparam ContextCollection Context container type.
 * @tparam DataType Input data type used by enablement predicate.
 * @tparam IsEnabledFn Callable: bool(const ContextType &, const DataType &).
 * @param contexts Context collection.
 * @param data Data used to evaluate enablement.
 * @param is_enabled Predicate callable.
 */
template<typename ContextCollection, typename DataType, typename IsEnabledFn>
void expect_single_active_context(const ContextCollection &contexts, const DataType &data,
                                  const IsEnabledFn &is_enabled) {
    int enabled_count = 0;
    for (const auto &context: contexts) {
        if (is_enabled(context, data)) {
            ++enabled_count;
        }
    }

    EXPECT_EQ(enabled_count, 1) << "Exactly one mode should be active";
}
