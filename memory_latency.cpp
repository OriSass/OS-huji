// OS 2025 EX1

#include "memory_latency.h"
#include "measure.h"
#include <string>
#include <stdexcept>
#include <iostream>
#include <cmath>

#define GALOIS_POLYNOMIAL ((1ULL << 63) | (1ULL << 62) | (1ULL << 60) | (1ULL << 59))
#define DEFAULT_ARR_SIZE 100

/**
 * Converts the struct timespec to time in nano-seconds.
 * @param t - the struct timespec to convert.
 * @return - the value of time in nano-seconds.
 */
uint64_t nanosectime(struct timespec t)
{
	// Your code here
    return t.tv_nsec + (t.tv_sec * 1000000000ull);
}

/**
* Measures the average latency of accessing a given array in a sequential order.
* @param repeat - the number of times to repeat the measurement for and average on.
* @param arr - an allocated (not empty) array to preform measurement on.
* @param arr_size - the length of the array arr.
* @param zero - a variable containing zero in a way that the compiler doesn't "know" it in compilation time.
* @return struct measurement containing the measurement with the following fields:
*      double baseline - the average time (ns) taken to preform the measured operation without memory access.
*      double access_time - the average time (ns) taken to preform the measured operation with memory access.
*      uint64_t rnd - the variable used to randomly access the array, returned to prevent compiler optimizations.
*/
struct measurement measure_sequential_latency(uint64_t repeat, array_element_t* arr, uint64_t arr_size, uint64_t zero)
{
    repeat = arr_size > repeat ? arr_size:repeat; // Make sure repeat >= arr_size

    // Baseline measurement:
    struct timespec t0;
    timespec_get(&t0, TIME_UTC);
    register uint64_t rnd=12345;
    for (register uint64_t i = 0; i < repeat; i++)
    {
        register uint64_t index = (i % arr_size) + rnd * zero;
        rnd ^= index & zero;
        rnd = (rnd >> 1) ^ ((0-(rnd & 1)) & GALOIS_POLYNOMIAL);  // Advance rnd pseudo-randomly (using Galois LFSR)
    }
    struct timespec t1;
    timespec_get(&t1, TIME_UTC);

    // Memory access measurement:
    struct timespec t2;
    timespec_get(&t2, TIME_UTC);
    rnd=(rnd & zero) ^ 12345;
    for (register uint64_t i = 0; i < repeat; i++)
    {
        register uint64_t index = (i % arr_size) + rnd  * zero;
        rnd ^= arr[index] & zero;
        rnd = (rnd >> 1) ^ ((0-(rnd & 1)) & GALOIS_POLYNOMIAL);  // Advance rnd pseudo-randomly (using Galois LFSR)
    }
    struct timespec t3;
    timespec_get(&t3, TIME_UTC);

    // Calculate baseline and memory access times:
    double baseline_per_cycle=(double)(nanosectime(t1)- nanosectime(t0))/(repeat);
    double memory_per_cycle=(double)(nanosectime(t3)- nanosectime(t2))/(repeat);
    struct measurement result;

    result.baseline = baseline_per_cycle;
    result.access_time = memory_per_cycle;
    result.rnd = rnd;
    return result;
}

bool validateArgs (int argc, char **p_string);
/**
 * Runs the logic of the memory_latency program. Measures the access latency for random and sequential memory access
 * patterns.
 * Usage: './memory_latency max_size factor repeat' where:
 *      - max_size - the maximum size in bytes of the array to measure access latency for.
 *      - factor - the factor in the geometric series representing the array sizes to check.
 *      - repeat - the number of times each measurement should be repeated for and averaged on.
 * The program will print output to stdout in the following format:
 *      mem_size_1,offset_1,offset_sequential_1
 *      mem_size_2,offset_2,offset_sequential_2
 *              ...
 *              ...
 *              ...
 */
int main(int argc, char* argv[])
{
    // zero==0, but the compiler doesn't know it. Use as the zero arg of measure_latency and measure_sequential_latency.
    struct timespec t_dummy;
    timespec_get(&t_dummy, TIME_UTC);
    const uint64_t zero = nanosectime(t_dummy)>1000000000ull?0:nanosectime(t_dummy);

    // Your code here
    if(!validateArgs(argc, argv)){
      return EXIT_FAILURE;
    }

    uint64_t max_size = std::stoul(argv[1]); // int
    float factor = std::stof(argv[2]); // float
    uint64_t repeat = std::stoul(argv[3]); // int

    // main logic
    uint64_t arr_size = DEFAULT_ARR_SIZE;
    int measure_count = 0;

      while (arr_size <= max_size)
      {
        measure_count++;
        // allocate arr in size


        // fill array
        uint64_t count = arr_size / sizeof(array_element_t);
        array_element_t* arr = (array_element_t*) malloc(count * sizeof (array_element_t));

        if (arr == NULL) {
          fprintf(stderr, "Memory allocation failed for size %lu\n", arr_size);
          exit(1);
        }

        for (uint64_t i = 0; i < count; i++)
        {
          arr[i] = i + 1;
        }

        // measure random
        struct measurement m_random_latency = measure_latency (repeat, arr,
            count, zero);
        // measure sequentially
        struct measurement m_seq_latency = measure_sequential_latency
            (repeat, arr, count, zero);
        // output results
        std::cout << arr_size << ","
        << m_random_latency.access_time - m_random_latency.baseline << ","
        << m_seq_latency.access_time - m_seq_latency.baseline<< std::endl;
        // free arr
        free(arr);
        arr_size = ceil(arr_size * factor);
      }
      return EXIT_SUCCESS;
  }
bool validateArgs (int argc, char **argv)
{
  try{

    if(argc != 4){
      std::cerr << "Wrong number of arguments, 3 needed to run" << std::endl;
      return false;
    }

    uint64_t max_size = std::stoul(argv[1]); // int
    float factor = std::stof(argv[2]); // float
    uint64_t repeat = std::stoul(argv[3]); // int

    if(max_size < 100){
      std::cerr << "Error: Invalid input! max_size needs to be >= 100" <<
      std::endl;
      return false;
    }
    if(factor <= 1){
      std::cerr << "Error: Invalid input! factor needs to be > 1" <<
                std::endl;
      return false;
    }
    if(repeat <= 0){
      std::cerr << "Error: Invalid input! repeat needs to be > 0" <<
                std::endl;
      return false;
    }

    return true;
  } catch (const std::invalid_argument& e){
      std::cerr << "Error: Invalid input! Ensure arguments are by order: int float int" << std::endl;
      return false;
  }
}
