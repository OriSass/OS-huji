#include "resources/uthreads.h"
#include <iostream>
#include <unordered_map>
#include <queue>
#include <vector>
#include <csignal>      // for sigaction, SIGVTALRM
#include <sys/time.h>   // for setitimer
#include <iostream>
#include <cstring>
#include <cerrno>
#include <setjmp.h>


using std::queue, std::unordered_map, std::vector;

#define MAIN_THREAD_TID 0

// ========================
// Internal Global Variables
// ========================

// TODO: Add thread management structures here (e.g., ready queue, threads map, etc.)

enum State{
    RUNNING, BLOCKED, READY
};

class Thread{

  private:
    int tid;
    State state;
    int quantums_passed;
    jmp_buf env; // store the execution context


  public:
    Thread(int tid, bool is_main) : tid(tid), state(READY){
      quantums_passed = is_main ? 1 : 0;
      setup_env();
    }

  void set_state (State state)
  {
    this->state = state;
  }
  void increment_quantum ()
  {
    this->quantums_passed++;
  }
  // Getter for the jmp_buf (so you can access it in the handler)
  jmp_buf* get_env() {
    return &env;
  }

  // Initialize the execution context for the thread
  bool setup_env() {
    if (sigsetjmp(env, 1) == 0) {
      return true;  // If sigsetjmp returns 0, it is called the first time
    }
    return false; // If sigsetjmp returns non-zero, it was called via siglongjmp
  }
};

static int quantum_def;
static int quantums_passed;
static int running_tid;

static queue<int> ready_queue;
static unordered_map<int,Thread*> threads_map;
static vector<int> available_tids;
// ========================
// Function Definitions
// ========================

void init_available_tids() {
  for (int i = 1; i < MAX_THREAD_NUM; ++i) {
    available_tids.push_back(i);
  }
}

// This function will be called every time the quantum ends
void timer_handler(int sig) {
  std::cout << "Quantum expired! Performing context switch..." << std::endl;
  (void)sig; // to suppress unused warning

  if (sigsetjmp(reinterpret_cast<__jmp_buf_tag *>(threads_map[running_tid]->get_env ()), 1) != 0) {
    return; // if returning from siglongjmp, just return
  }
  //  1. If it was preempted because its quantum has expired, move it to the end of the
  //  READY threads list.
  threads_map[running_tid]->set_state (READY);
  ready_queue.push (running_tid);
  //  2. Move the next thread in the queue of READY threads to the RUNNING state
  running_tid = ready_queue.front();
  ready_queue.pop();

  threads_map[running_tid]->set_state(RUNNING);
  threads_map[running_tid]->increment_quantum();

  quantums_passed++;

  siglongjmp(reinterpret_cast<__jmp_buf_tag *>(threads_map[running_tid]->get_env ()), 1);

}

// Sets up the signal handler for SIGVTALRM
void setup_signal_handler() {
  struct sigaction sa{};
  sa.sa_handler = &timer_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
    std::cerr << "Error setting up signal handler" << std::endl;
    exit(1);
  }
}


// Sets up a virtual timer to fire every quantum_usecs microseconds
void setup_timer(int quantum_usecs) {
  struct itimerval timer{};

  timer.it_value.tv_sec = quantum_usecs / 1000000;
  timer.it_value.tv_usec = quantum_usecs % 1000000;

  timer.it_interval.tv_sec = quantum_usecs / 1000000;
  timer.it_interval.tv_usec = quantum_usecs % 1000000;

  if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) == -1) {
    std::cerr << "system error: setitimer failed: " << strerror(errno) << std::endl;
    exit(1);
  }
}

/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_init(int quantum_usecs)
{
  if(quantum_usecs <= 0){
    std::cerr << "thread library error: quantum should be positive\n";
    return -1;
  }
  running_tid = MAIN_THREAD_TID;
  quantum_def = quantum_usecs;
  quantums_passed = 1;

  Thread* main_thread = new Thread(MAIN_THREAD_TID, true);
  main_thread->set_state (RUNNING);
  threads_map[MAIN_THREAD_TID] = main_thread;

  setup_signal_handler();
  setup_timer (quantum_def);

  init_available_tids();

  return 0;
}


/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
 */
int uthread_spawn(thread_entry_point entry_point)
{
  // TODO: implement
  return 0;
}


/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
 */
int uthread_terminate(int tid)
{
  // TODO: implement
  return 0;
}


/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_block(int tid)
{
  // TODO: implement
  return 0;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid)
{
  // TODO: implement
  return 0;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_sleep(int num_quantums)
{
  // TODO: implement
  return 0;
}


/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
 */
int uthread_get_tid()
{
  // TODO: implement
  return 0;
}


/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
 */
int uthread_get_total_quantums()
{
  // TODO: implement
  return 0;
}


/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
 */
int uthread_get_quantums(int tid)
{
  // TODO: implement
  return 0;
}
