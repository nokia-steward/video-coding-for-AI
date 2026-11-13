// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace sf {
    /**
     * @file thread_safe_queue.h
     * @brief Provides a generic, thread-safe queue for producer-consumer patterns.
     */

    /**
     * @struct thread_safe_queue
     * @brief A thread-safe wrapper around std::queue.
     *
     * This struct uses a mutex and condition variable to allow for safe, blocking
     * multi-threaded access, suitable for passing data between processing threads.
     *
     * @tparam T The type of elements to be stored in the queue.
     */
    template<typename T>
    struct thread_safe_queue {
        /**
         * @brief Default constructor. Initializes all member variables.
         */
        thread_safe_queue() : mutex(), queue(), condition_variable(), closed(false) {}

        /**
         * @brief Pushes an item onto the queue and notifies a waiting thread.
         * @param item The item to push.
         */
        void push(T item) {
            std::lock_guard lock(mutex);
            queue.push(std::move(item));
            condition_variable.notify_one();
        }

        /**
         * @brief Waits for and pops an item from the queue.
         *
         * This function will block until an item is available or the queue is closed.
         *
         * @return A std::optional containing the item, or std::nullopt if the
         * queue has been closed and is empty.
         */
        std::optional<T> pop() {
            std::unique_lock lock(mutex);
            condition_variable.wait(lock, [this] { return !queue.empty() || closed; });

            if (closed && queue.empty()) {
                return std::nullopt;
            }

            T item = std::move(queue.front());
            queue.pop();
            return item;
        }

        /**
         * @brief Pops an item from the queue without waiting.
         *
         * This function will not block. It returns immediately with either
         * the first item or std::nullopt if the queue is empty.
         *
         * @return A std::optional containing the item, or std::nullopt if the
         * queue is empty.
         */
        std::optional<T> try_pop() {
            std::lock_guard lock(mutex);
            if (queue.empty()) {
                return std::nullopt;
            }

            T item = std::move(queue.front());
            queue.pop();
            return item;
        }

        /**
         * @brief Checks whether the queue is currently empty.
         * @return true if the queue is empty, false otherwise.
         */
        [[nodiscard]] bool empty() const {
            std::lock_guard lock(mutex);
            return queue.empty();
        }

        /**
         * @brief Gets the current size of the queue.
         * @return The number of items in the queue.
         */
        [[nodiscard]] size_t size() const {
            std::lock_guard lock(mutex);
            return queue.size();
        }

        /**
         * @brief Closes the queue, notifying all waiting threads.
         *
         * Once closed, no more items can be pushed. Popping will continue until
         * the queue is empty, after which it will return std::nullopt.
         */
        void close() {
            std::lock_guard lock(mutex);
            closed = true;
            condition_variable.notify_all();
        }

        /**
         * @brief Drains all remaining items from the queue.
         *
         * This method is useful during shutdown to ensure all RAII objects
         * in the queue are properly destroyed. It should be called after close()
         * to clean up any remaining items.
         */
        void drain() {
            std::lock_guard lock(mutex);
            while (!queue.empty()) {
                queue.pop();
            }
        }

    private:
        mutable std::mutex mutex;
        std::queue<T> queue;
        std::condition_variable condition_variable;
        bool closed;
    };
} // namespace sf
