# Linux CFS Simulator
Simulate Linux Completely Fair Scheduler (CFS) using POSIX Threads.

## Build and Run
```shell
$ make
$ ./cfs-sim
```

Note:
- The process info table is showed at the beginning and when a process is finished.
- Press Cltr + C if you want to stop

## Configurations
### Initial Processes
A set of 20 processes are stored in file "processes.txt".
You can add or modify processes by editing this file

### Balancer
The balancer are triggered every 2 seconds using sleep function
You can change that by changing the `b_freq` macro.

### Finish
When there is no more process to execute, the balancer will stop the
simulator by setting `running` = 0.

### Slow down the print out
You can slow down the printing by adjusting the macro `sleep_time`.
