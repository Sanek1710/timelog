#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace cursor_timer {

// Forward declarations
class StaticTimeHolderRegistry;

// High-resolution clock
using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::nanoseconds;

// Static time holder - exists for program lifetime
struct StaticTimeHolder {
  char name[64];
  uint64_t total_time_ns;
  uint32_t call_count;
  StaticTimeHolder* parent;  // Can be overwritten
  StaticTimeHolder* next;    // Linked list pointer

  StaticTimeHolder(const char* timer_name)
      : total_time_ns(0), call_count(0), parent(nullptr), next(nullptr) {
    size_t len = std::min(strlen(timer_name), 63UL);
    memcpy(name, timer_name, len);
    name[len] = '\0';
  }

  void add_timing(uint64_t duration_ns) {
    total_time_ns += duration_ns;
    call_count++;
  }
};

// Global static time holder registry
class StaticTimeHolderRegistry {
 public:
  static StaticTimeHolderRegistry& instance() {
    static StaticTimeHolderRegistry registry;
    return registry;
  }

  // Register a new static time holder (called by static instances)
  bool register_holder(StaticTimeHolder* holder) {
    // Add to circular linked list - insert after root
    holder->next = root_.next;
    root_.next = holder;
    return true;
  }

  // Iterator for the circular linked list
  class iterator {
   public:
    iterator(StaticTimeHolder* ptr) : ptr_(ptr) {}

    StaticTimeHolder* operator*() const { return ptr_; }
    StaticTimeHolder* operator->() const { return ptr_; }

    iterator& operator++() {
      ptr_ = ptr_->next;
      return *this;
    }

    bool operator!=(const iterator& other) const { return ptr_ != other.ptr_; }
    bool operator==(const iterator& other) const { return ptr_ == other.ptr_; }

   private:
    StaticTimeHolder* ptr_;
  };

  // Begin iterator (first element after root)
  iterator begin() const {
    return iterator(const_cast<StaticTimeHolder*>(root_.next));
  }

  // End iterator (root itself - circular list)
  iterator end() const {
    return iterator(const_cast<StaticTimeHolder*>(&root_));
  }

  // Check if empty (only root exists)
  bool empty() const { return begin() == end(); }

  // Reset all timing data
  void reset_all() {
    for (auto it = begin(); it != end(); ++it) {
      (*it)->total_time_ns = 0;
      (*it)->call_count = 0;
    }
  }

  // Get root node
  StaticTimeHolder* get_root() { return &root_; }

 private:
  StaticTimeHolder root_{"$ROOT$"};  // Static root node - no heap allocation

  // Initialize circular linked list
  StaticTimeHolderRegistry() {
    root_.next = &root_;  // Root points to itself initially
  }
};

// Thread-local cursor for tracking call stack
class ThreadLocalCursor {
 private:
  static StaticTimeHolder*& get_cursor_ref() {
    thread_local StaticTimeHolder* cursor = nullptr;
    return cursor;
  }

 public:
  static StaticTimeHolder* get_cursor() { return get_cursor_ref(); }

  static void set_cursor(StaticTimeHolder* holder) {
    get_cursor_ref() = holder;
  }

  static StaticTimeHolder* get_and_set_cursor(StaticTimeHolder* new_holder) {
    StaticTimeHolder*& cursor_ref = get_cursor_ref();
    StaticTimeHolder* old_cursor = cursor_ref;
    cursor_ref = new_holder;
    return old_cursor;
  }
};

// Runtime timer - lightweight RAII object
class RuntimeTimer {
 public:
  RuntimeTimer(StaticTimeHolder* holder)
      : holder_(holder), start_time_(Clock::now()) {
    // Save current cursor as parent for this timer instance
    parent_cursor_ = ThreadLocalCursor::get_cursor();

    // Set parent relationship in static holder
    if (parent_cursor_ != nullptr) {
      holder_->parent = parent_cursor_;
    }

    // Move cursor to this holder
    ThreadLocalCursor::set_cursor(holder_);
  }

  ~RuntimeTimer() {
    // Record timing
    auto end_time = Clock::now();
    auto duration =
        std::chrono::duration_cast<Duration>(end_time - start_time_);
    holder_->add_timing(duration.count());

    // Restore cursor to the original parent
    ThreadLocalCursor::set_cursor(parent_cursor_);
  }

  StaticTimeHolder* get_holder() const { return holder_; }

 private:
  StaticTimeHolder* holder_;
  StaticTimeHolder* parent_cursor_;  // Store original parent separately
  TimePoint start_time_;
};

// Convenience macros for easy usage
#define STATIC_TIMER(name)                                                    \
  static StaticTimeHolder _static_holder(name);                               \
  static bool _dummy =                                                        \
      (StaticTimeHolderRegistry::instance().register_holder(&_static_holder), \
       true)

#define CURSOR_TIMER(name) \
  STATIC_TIMER(name);      \
  RuntimeTimer _runtime_timer(&_static_holder)

// Tree printing utilities
class TreePrinter {
 public:
  static void print_tree(std::ostream& os = std::cout) {
    auto& registry = StaticTimeHolderRegistry::instance();

    if (registry.empty()) {
      os << "No timers recorded.\n";
      return;
    }

    // Build parent-child relationships from current parent pointers
    std::map<StaticTimeHolder*, std::vector<StaticTimeHolder*>> children;
    std::vector<StaticTimeHolder*> roots;

    for (auto it = registry.begin(); it != registry.end(); ++it) {
      auto* holder = *it;
      if (holder->call_count == 0) continue;  // Skip unused holders

      if (holder->parent == nullptr) {
        roots.push_back(holder);
      } else {
        children[holder->parent].push_back(holder);
      }
    }

    // Helper function to format duration as single number (seconds.ms) with 6
    // precision
    auto format_duration_single = [](uint64_t ns) -> std::string {
      double seconds = static_cast<double>(ns) / 1000000000.0;
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(6) << seconds;
      return oss.str();
    };

    // Helper function to format duration for average time
    auto format_duration_avg = [](uint64_t ns) -> std::string {
      if (ns >= 1000000000) {
        return std::to_string(ns / 1000000000) + "s " +
               std::to_string((ns % 1000000000) / 1000000) + "ms";
      } else if (ns >= 1000000) {
        return std::to_string(ns / 1000000) + "ms " +
               std::to_string((ns % 1000000) / 1000) + "μs";
      } else if (ns >= 1000) {
        return std::to_string(ns / 1000) + "μs " + std::to_string(ns % 1000) +
               "ns";
      } else {
        return std::to_string(ns) + "ns";
      }
    };

    // Recursive tree printing
    std::function<void(StaticTimeHolder*, int)> print_node =
        [&](StaticTimeHolder* holder, int depth) {
          const int total_width = 80;  // Total line width

          // Calculate space for time (right-aligned)
          int time_width = 12;    // Time field width (6 precision + "s")
          int calls_width = 12;   // Calls field width
          int avg_width = 12;     // Average field width
          int bracket_width = 2;  // "[" and "]"
          int slash_width = 3;    // "/1 ="
          int total_right_width = time_width + bracket_width + calls_width +
                                  slash_width + avg_width + bracket_width;

          // Calculate remaining width for name and tree structure
          int indent_width = depth * 2;  // "  " per level
          int prefix_width = 3;          // "├─ "
          int remaining_width = total_width - total_right_width;
          int name_width = remaining_width - indent_width - prefix_width;

          // Print time first (right-aligned)
          std::string time_str =
              format_duration_single(holder->total_time_ns) + "s";
          os << std::setw(time_width) << std::right << time_str;

          // Print calls and average
          if (holder->call_count > 0) {
            double avg_seconds = static_cast<double>(holder->total_time_ns) /
                                 holder->call_count / 1000000000.0;
            os << "[" << std::setw(calls_width) << std::right
               << holder->call_count << "/1 = " << std::setw(avg_width)
               << std::fixed << std::setprecision(6) << avg_seconds << "s]";
          } else {
            os << "[" << std::setw(calls_width) << std::right << "0"
               << "/1 = " << std::setw(avg_width) << "0.000000s]";
          }

          // Print indentation
          for (int i = 0; i < depth; ++i) {
            os << "  ";
          }

          // Print name and tree structure
          os << "├─ " << std::setw(name_width) << std::left << holder->name;

          // Print parent for debugging
          if (holder->parent != nullptr) {
            os << " <- " << holder->parent->name;
          } else {
            os << " <- (root)";
          }

          os << "\n";

          // Print children
          auto children_it = children.find(holder);
          if (children_it != children.end()) {
            for (auto* child : children_it->second) {
              print_node(child, depth + 1);
            }
          }
        };

    // Print header
    os << "\n=== Cursor Timer Tree ===\n";

    // Print all root holders
    for (auto* root : roots) {
      print_node(root, 0);
    }

    // Print summary
    os << "\n=== Summary ===\n";
    uint64_t total_ns = 0;
    uint32_t total_calls = 0;

    for (auto it = registry.begin(); it != registry.end(); ++it) {
      auto* holder = *it;
      if (holder->call_count > 0) {
        total_ns += holder->total_time_ns;
        total_calls += holder->call_count;
      }
    }

    os << "Total time: " << std::setw(12) << std::right
       << format_duration_single(total_ns) << "s\n";
    os << "Total calls: " << std::setw(12) << std::right << total_calls << "\n";

    double avg_seconds = total_calls > 0 ? static_cast<double>(total_ns) /
                                               total_calls / 1000000000.0
                                         : 0.0;
    os << "Average call time: " << std::setw(12) << std::right << std::fixed
       << std::setprecision(6) << avg_seconds << "s\n";
  }

  static void reset_all() { StaticTimeHolderRegistry::instance().reset_all(); }
};

}  // namespace cursor_timer
