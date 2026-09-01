# CPU Scheduling Simulator (C++)

Simulates classic CPU scheduling algorithms and compares them on the same workload.

## Algorithms
- **FCFS** — First Come First Served (non-preemptive)
- **SJF** — Shortest Job First (non-preemptive)
- **STCF** — Shortest Time-to-Completion First (preemptive SJF)
- **Priority** — non-preemptive, lower number = higher priority
- **Round Robin** — configurable time quantum (default 2)
- **MLFQ** — Multi-Level Feedback Queue: 3 levels, per-level allotment {4,8,16},
  priority boost every 30 ticks; demotion charged on total time at a level
  (the anti-gaming form), periodic boost prevents starvation.

## Metrics
Per job: turnaround, response, and waiting time.
Per policy: average of each, plus CPU utilization and throughput.

## Build & run
```
g++ -std=c++17 -O2 scheduler.cpp -o scheduler
./scheduler                  # built-in sample workload
./scheduler sample_workload.txt
```

Workload file format, one process per line: `pid arrival burst priority`
