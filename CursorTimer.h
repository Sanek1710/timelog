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

// generic context registry forward declaration
template <typename Holder>
class StaticContextRegistry;

// Base static context holder (independent of time logic)
struct StaticContextHolder {
  char name[64];
  StaticContextHolder* parent = nullptr;
  StaticContextHolder* next = nullptr;

 protected:
  // Root constructor: name "/", self parent/next
  StaticContextHolder() : parent(this), next(this) {
    name[0] = '/';
    name[1] = '\0';
  }

  explicit StaticContextHolder(const char* n) : parent(nullptr), next(nullptr) {
    size_t len = std::min(strlen(n), 63UL);
    memcpy(name, n, len);
    name[len] = '\0';
  }
};

// Generic global static context registry (circular list)
template <typename Holder>
class StaticContextRegistry {
 public:
  static StaticContextRegistry& instance() {
    static StaticContextRegistry registry;
    return registry;
  }

  void register_holder(Holder& holder) {
    holder.next = root.next;
    root.next = &holder;
  }

  class iterator {
   public:
    explicit iterator(Holder* ptr) : ptr(ptr) {}
    Holder* operator*() const { return ptr; }
    Holder* operator->() const { return ptr; }
    iterator& operator++() { return ptr = static_cast<Holder*>(ptr->next), *this; }
    bool operator!=(const iterator& other) const { return ptr != other.ptr; }
    bool operator==(const iterator& other) const { return ptr == other.ptr; }

   private:
    Holder* ptr;
  };

  iterator begin() const {
    return iterator(const_cast<Holder*>(static_cast<const Holder*>(root.next)));
  }
  iterator end() const { return iterator(const_cast<Holder*>(&root)); }

  bool empty() const { return begin() == end(); }

  Holder* get_root() { return &root; }

 protected:
  Holder root;  // uses Holder's default root ctor
  StaticContextRegistry() = default;
};




// Forward declare time holder and define alias registry
struct StaticTimeHolder;
using StaticTimeHolderRegistry = StaticContextRegistry<StaticTimeHolder>;

// Time holder derives from context holder
struct StaticTimeHolder : public StaticContextHolder {
  uint64_t total_time_ns = 0;
  uint32_t call_count = 0;

  StaticTimeHolder(const char* timer_name, StaticTimeHolderRegistry& registry)
      : StaticContextHolder(timer_name) {
    registry.register_holder(*this);
  }

  void add_timing(uint64_t duration_ns) {
    total_time_ns += duration_ns;
    call_count++;
  }

 protected:
  // Root constructor used by registry
  StaticTimeHolder() = default;

  template <typename Holder>
  friend class StaticContextRegistry;
};

// Reset all timing data
inline void reset_all_timeholders() {
  auto& reg = StaticTimeHolderRegistry::instance();
  for (auto it = reg.begin(); it != reg.end(); ++it) {
    (*it)->total_time_ns = 0;
    (*it)->call_count = 0;
  }
}

// Thread-local cursor for tracking call stack
template <typename Holder, typename Registry>
class ThreadLocalContextCursor {
  using Cursor = Holder*;

 private:
  static Cursor& get_cursor_ref() {
    thread_local Cursor cursor =
        StaticTimeHolderRegistry::instance().get_root();
    return cursor;
  }

 public:
  static Cursor get_cursor() { return get_cursor_ref(); }

  static void set_cursor(Cursor holder) { get_cursor_ref() = holder; }

  static Cursor exchange(Cursor new_holder) {
    Cursor& cursor_ref = get_cursor_ref();
    Cursor old_cursor = cursor_ref;
    cursor_ref = new_holder;
    return old_cursor;
  }
};

using ThreadLocalTimeCursor = ThreadLocalContextCursor<StaticTimeHolder, StaticTimeHolderRegistry>;

// High-resolution clock
using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::nanoseconds;

// Runtime timer - lightweight RAII object
class RuntimeTimer {
 public:
  RuntimeTimer(StaticTimeHolder* holder)
      : holder(*holder),
        parent_cursor(ThreadLocalTimeCursor::exchange(holder)),
        start_time(Clock::now()) {
    // oh god i hope its never null by design
    holder->parent = parent_cursor;
  }

  ~RuntimeTimer() {
    // Record timing
    const auto end_time = Clock::now();
    const auto duration =
        std::chrono::duration_cast<Duration>(end_time - start_time);
    holder.add_timing(duration.count());

    // Restore cursor to the original parent
    ThreadLocalTimeCursor::set_cursor(parent_cursor);
  }

 private:
  StaticTimeHolder& holder;
  StaticTimeHolder* parent_cursor;  // Store original parent separately

  TimePoint start_time;
};

// Convenience macros for easy usage
#define STATIC_TIMER(name)                     \
  static StaticTimeHolder _static_holder(name, \
                                         StaticTimeHolderRegistry::instance())

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

      if (holder->parent == registry.get_root()) {
        roots.push_back(holder);
      } else {
        children[static_cast<StaticTimeHolder*>(holder->parent)].push_back(holder);
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
              format_duration_single(holder->total_time_ns) + "s ";
          os << std::setw(time_width) << std::right << time_str;

          // Print calls and average
          if (holder->call_count > 0) {
            double avg_seconds = static_cast<double>(holder->total_time_ns) /
                                 holder->call_count / 1000000000.0;
            os << "[" << std::setw(calls_width) << std::right
               << holder->call_count << " : " << std::setw(avg_width)
               << std::fixed << std::setprecision(6) << avg_seconds << "s] ";
          } else {
            os << "[" << std::setw(calls_width) << std::right << "0"
               << " : " << std::setw(avg_width) << "0.000000s] ";
          }

          // Print indentation
          for (int i = 0; i < depth; ++i) os << "  ";

          // Print name and tree structure
          os << " " << std::setw(name_width) << std::left << holder->name;

          // Print parent for debugging
          // os << " <- " << holder->parent->name;

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
    os << "\nTIME REGISTRY:\n";

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

  static void reset_all() { reset_all_timeholders(); }
};

}  // namespace cursor_timer
