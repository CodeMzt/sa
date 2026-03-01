# Motor Test Program Implementation Plan

## Overview
Implement a motor test program within `can_comms_entry.c` using conditional compilation (`#define MOTOR_TEST`) to isolate it from the main program. The test will focus on motor ID=1 and include comprehensive testing with progress logging and non-blocking delays.

## Implementation Steps

### 1. Modify Main Entry Point
- Add conditional compilation around the main loop in `can_comms_entry()` function
- When `MOTOR_TEST` is defined, bypass the normal operation and execute test routine
- Ensure proper task delay to prevent watchdog timeouts

### 2. Create Motor Test Function
- Implement a dedicated function `motor_test_procedure()` for the test sequence
- Include comprehensive test steps covering motor functionality
- Use `LOG_D` for progress tracking throughout the test
- Implement non-blocking delays using FreeRTOS `vTaskDelay()`

### 3. Test Sequence Details
The test sequence will include:
- Initialize CANFD communication
- Enable motor ID=1
- Read initial motor status and parameters
- Position control tests (move to several positions)
- Speed control tests
- Current control tests
- Fault simulation and clearing
- Parameter reading/writing tests
- Motor disable and cleanup

### 4. Thread Limitation Implementation
- Add conditional compilation guards to other thread entry points
- When `MOTOR_TEST` is defined, add infinite delay at the beginning of other threads to limit their execution
- Specifically target threads other than `can_comms` and `log_task`

### 5. Safety Measures
- Ensure proper error handling during tests
- Implement timeout mechanisms for operations
- Add safety checks before each test step
- Proper cleanup after test completion

## Files to Modify
1. `src/can_comms_entry.c` - Add motor test functionality
2. Potentially other thread entry point files - Add conditional compilation guards

## Conditional Compilation Structure
```c
#ifdef MOTOR_TEST
    // Motor test code here
#else
    // Normal operation code here
#endif
```

## Expected Outcomes
- Comprehensive testing of motor ID=1 functionality
- Detailed logging of test progress
- Isolated test environment that doesn't interfere with main program
- Safe execution with proper error handling