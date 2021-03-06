# Linux CFS Simulator
Simulate Linux Completely Fair Scheduler (CFS) using POSIX Threads.

## Build and Run
```shell
$ make
$ ./cfs-sim
```

Note:
- The process status table is shown at the beginning when a process is finished.
- Press Cltr + C if you want to stop

## Configurations
### Initial Processes
A set of 20 processes are specified in file `processes.txt`.
You can add or modify processes by editing this file

### Load Balancer
The load balancer are triggered every 2 seconds.
You can change that by changing the macro `BALANCER_INTERVAL`.

### Finish
When there is no more process to execute, the balancer will stop the
simulator by setting `running` to 0.

### Slow down the print out
You can slow down the printing by adjusting the macro `SLEEP_TIME`.

## License

`linux-cfs-sim` is released under the MIT License. Use of this source code is
governed by a MIT-style license that can be found in the LICENSE file.
