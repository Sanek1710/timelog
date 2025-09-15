#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "CursorTimer.h"

using namespace cursor_timer;

// Example functions demonstrating the cursor timer system
void expensive_calculation(int iterations) {
  CURSOR_TIMER("expensive_calculation");

  std::vector<int> data;
  data.reserve(iterations);

  {
    CURSOR_TIMER("data_generation");
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000);

    for (int i = 0; i < iterations; ++i) {
      data.push_back(dis(gen));
    }
  }

  {
    CURSOR_TIMER("data_processing");
    volatile int sum = 0;
    for (int i = 0; i < iterations; ++i) {
      sum += data[i] * data[i];
    }
  }

  {
    CURSOR_TIMER("data_sorting");
    std::sort(data.begin(), data.end());
  }
}

void nested_function_calls(int depth, int calls_per_level) {
  CURSOR_TIMER("nested_calls");

  if (depth <= 0) {
    // Base case - do some work
    CURSOR_TIMER("base_work");
    volatile int result = 0;
    for (int i = 0; i < 1000; ++i) {
      result += i * i;
    }
    return;
  }

  // Recursive calls
  for (int i = 0; i < calls_per_level; ++i) {
    nested_function_calls(depth - 1, calls_per_level);
  }
}

void benchmark_millions_of_calls() {
  CURSOR_TIMER("benchmark_millions");

  const int TOTAL_CALLS = 1000000;  // 1 million calls
  const int BATCH_SIZE = 10000;     // Process in batches

  {
    CURSOR_TIMER("batch_processing");
    for (int batch = 0; batch < TOTAL_CALLS / BATCH_SIZE; ++batch) {
      CURSOR_TIMER("batch_processing");

      for (int i = 0; i < BATCH_SIZE; ++i) {
        CURSOR_TIMER("micro_operation");

        // Minimal work to test timer overhead
        volatile int x = i * 2;
        volatile int y = x + 1;
        volatile int z = x * y;
      }
    }
  }
}

void multi_threaded_example() {
  CURSOR_TIMER("multi_threaded_example");

  const int NUM_THREADS = 4;
  const int WORK_PER_THREAD = 100000;

  std::vector<std::thread> threads;

  {
    CURSOR_TIMER("thread_creation");
    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([WORK_PER_THREAD]() {
        CURSOR_TIMER("thread_work");

        {
          CURSOR_TIMER("thread_calculation");
          volatile int sum = 0;
          for (int i = 0; i < WORK_PER_THREAD; ++i) {
            sum += i * i;
          }
        }

        {
          CURSOR_TIMER("thread_sleep");
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      });
    }
  }

  {
    CURSOR_TIMER("thread_joining");
    for (auto& thread : threads) {
      thread.join();
    }
  }
}

void shared_timer_func() {
  CURSOR_TIMER("shared_timer");
  volatile int x = 42;
}

void demonstrate_parent_overwriting() {
  CURSOR_TIMER("parent_overwriting_demo");
  {
    CURSOR_TIMER("context_a");
    { shared_timer_func(); }
  }

  {
    CURSOR_TIMER("context_b");
    { shared_timer_func(); }
  }

  // The "shared_timer" will show up under "context_b" in the tree
  // because its parent was overwritten
}

void stress_test_cursor_system() {
  CURSOR_TIMER("stress_test");

  const int ITERATIONS = 100000;
  const int CALLS_PER_ITERATION = 100;

  std::cout << "Starting stress test with "
            << (ITERATIONS * CALLS_PER_ITERATION) << " timer calls...\n";

  auto start_time = std::chrono::high_resolution_clock::now();

  for (int iter = 0; iter < ITERATIONS; ++iter) {
    CURSOR_TIMER("iteration");

    for (int i = 0; i < CALLS_PER_ITERATION; ++i) {
      CURSOR_TIMER("micro_call");

      // Minimal work
      volatile int x = i * iter;
    }

    // Progress indicator
    if (iter % 10000 == 0) {
      std::cout << "Completed " << iter << " iterations...\n";
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end_time - start_time)
                        .count();

  start_time = std::chrono::high_resolution_clock::now();
  for (int iter = 0; iter < ITERATIONS; ++iter) {
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < CALLS_PER_ITERATION; ++i) {
      auto start_time = std::chrono::high_resolution_clock::now();

      // Minimal work
      volatile int x = i * iter;
      auto end_time = std::chrono::high_resolution_clock::now();
      volatile auto dummy =
          std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                                start_time)
              .count();
    }

    // Progress indicator
    if (iter % 10000 == 0) {
      std::cout << "Completed " << iter << " iterations...\n";
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    volatile auto dummy = std::chrono::duration_cast<std::chrono::milliseconds>(
                              end_time - start_time)
                              .count();
  }
  end_time = std::chrono::high_resolution_clock::now();
  auto total_raw_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();

  double relaive_overhead =
      double(total_time - total_raw_time) / total_raw_time;
  std::cout << "Stress test completed in " << total_time << "ms\n";
  std::cout << "Raw timer runs in " << total_raw_time << "ms\n";
  std::cout << "Average relative overhead: " << relaive_overhead << "ms\n";
}

int main() {
  std::cout << "=== Cursor Timer System Demo ===\n\n";

  // Test 1: Basic functionality
  std::cout << "Test 1: Basic functionality\n";
  {
    CURSOR_TIMER("basic_test");
    expensive_calculation(10000);
  }

  // Test 2: Nested calls
  std::cout << "\nTest 2: Nested function calls\n";
  nested_function_calls(2, 3);

  // Test 3: Millions of calls
  std::cout << "\nTest 3: Millions of timer calls\n";
  benchmark_millions_of_calls();

  // Test 4: Multi-threaded
  std::cout << "\nTest 4: Multi-threaded timing\n";
  multi_threaded_example();

  // Test 5: Parent overwriting demonstration
  std::cout << "\nTest 5: Parent overwriting demonstration\n";
  demonstrate_parent_overwriting();

  // Test 6: Stress test (uncomment for heavy testing)
  // std::cout << "\nTest 6: Stress test\n";
  // stress_test_cursor_system();

  // Print results
  std::cout << "\n=== Performance Results ===\n";
  TreePrinter::print_tree();

  return 0;
}
