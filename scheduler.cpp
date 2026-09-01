// ============================================================================
//  CPU Scheduling Simulator
//  Implements FCFS, SJF (non-preemptive), STCF (preemptive SJF), Priority,
//  Round Robin, and a Multi-Level Feedback Queue (MLFQ).
//
//  Reports per-job turnaround / response / waiting time plus CPU utilization
//  and throughput, and prints a side-by-side comparison across all policies.
//
//  Build:  g++ -std=c++17 -O2 scheduler.cpp -o scheduler
//  Run:    ./scheduler                 (uses a built-in sample workload)
//          ./scheduler workload.txt    (lines: "pid arrival burst priority")
//
//  Convention: for Priority scheduling a LOWER number = HIGHER priority.
// ============================================================================
#include <bits/stdc++.h>
using namespace std;

struct Process {
    int pid, arrival, burst, priority;
};

// Per-job results produced by a run.
struct JobResult {
    int pid, arrival, burst;
    int start = -1;        // first time on CPU (for response time)
    int completion = -1;
    int turnaround() const { return completion - arrival; }
    int waiting()    const { return turnaround() - burst; }
    int response()   const { return start - arrival; }
};

// Whole-run summary.
struct Summary {
    string policy;
    vector<JobResult> jobs;
    double avg_turnaround = 0, avg_response = 0, avg_waiting = 0;
    double utilization = 0, throughput = 0;
};

// Fill averages + utilization + throughput from completed jobs.
static void finalize(Summary& s) {
    long tt = 0, rt = 0, wt = 0, busy = 0;
    int makespan = 0;
    for (auto& j : s.jobs) {
        tt += j.turnaround();
        rt += j.response();
        wt += j.waiting();
        busy += j.burst;
        makespan = max(makespan, j.completion);
    }
    int n = (int)s.jobs.size();
    s.avg_turnaround = (double)tt / n;
    s.avg_response   = (double)rt / n;
    s.avg_waiting    = (double)wt / n;
    s.utilization    = makespan ? 100.0 * busy / makespan : 0;
    s.throughput     = makespan ? (double)n / makespan : 0;
}

// ---- helper: index jobs by pid for result writing --------------------------
static map<int, JobResult> blank_results(const vector<Process>& ps) {
    map<int, JobResult> r;
    for (auto& p : ps) r[p.pid] = JobResult{p.pid, p.arrival, p.burst};
    return r;
}
static Summary pack(const string& name, map<int, JobResult>& r,
                    const vector<Process>& ps) {
    Summary s; s.policy = name;
    for (auto& p : ps) s.jobs.push_back(r[p.pid]);
    finalize(s);
    return s;
}

// ============================ FCFS ==========================================
Summary fcfs(vector<Process> ps) {
    sort(ps.begin(), ps.end(), [](const Process& a, const Process& b){
        return a.arrival != b.arrival ? a.arrival < b.arrival : a.pid < b.pid;
    });
    auto r = blank_results(ps);
    int t = 0;
    for (auto& p : ps) {
        if (t < p.arrival) t = p.arrival;          // CPU idle until arrival
        r[p.pid].start = t;
        t += p.burst;
        r[p.pid].completion = t;
    }
    return pack("FCFS", r, ps);
}

// ===================== SJF (non-preemptive) =================================
Summary sjf(vector<Process> ps) {
    auto r = blank_results(ps);
    int n = ps.size(), done = 0, t = 0;
    vector<bool> finished(n, false);
    while (done < n) {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (!finished[i] && ps[i].arrival <= t)
                if (pick == -1 || ps[i].burst < ps[pick].burst ||
                   (ps[i].burst == ps[pick].burst && ps[i].arrival < ps[pick].arrival))
                    pick = i;
        if (pick == -1) {                          // nothing arrived yet -> jump
            int next = INT_MAX;
            for (int i = 0; i < n; ++i) if (!finished[i]) next = min(next, ps[i].arrival);
            t = next; continue;
        }
        r[ps[pick].pid].start = t;
        t += ps[pick].burst;
        r[ps[pick].pid].completion = t;
        finished[pick] = true; done++;
    }
    return pack("SJF (non-preemptive)", r, ps);
}

// ============= STCF / SRTF (preemptive SJF), tick-based =====================
Summary stcf(vector<Process> ps) {
    auto r = blank_results(ps);
    int n = ps.size(), done = 0, t = 0;
    vector<int> rem(n);
    for (int i = 0; i < n; ++i) rem[i] = ps[i].burst;
    while (done < n) {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (rem[i] > 0 && ps[i].arrival <= t)
                if (pick == -1 || rem[i] < rem[pick] ||
                   (rem[i] == rem[pick] && ps[i].arrival < ps[pick].arrival))
                    pick = i;
        if (pick == -1) { t++; continue; }         // idle tick
        if (r[ps[pick].pid].start == -1) r[ps[pick].pid].start = t;
        rem[pick]--; t++;
        if (rem[pick] == 0) { r[ps[pick].pid].completion = t; done++; }
    }
    return pack("STCF (preemptive SJF)", r, ps);
}

// ================= Priority (non-preemptive, lower = higher) ================
Summary priority_np(vector<Process> ps) {
    auto r = blank_results(ps);
    int n = ps.size(), done = 0, t = 0;
    vector<bool> finished(n, false);
    while (done < n) {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (!finished[i] && ps[i].arrival <= t)
                if (pick == -1 || ps[i].priority < ps[pick].priority ||
                   (ps[i].priority == ps[pick].priority && ps[i].arrival < ps[pick].arrival))
                    pick = i;
        if (pick == -1) {
            int next = INT_MAX;
            for (int i = 0; i < n; ++i) if (!finished[i]) next = min(next, ps[i].arrival);
            t = next; continue;
        }
        r[ps[pick].pid].start = t;
        t += ps[pick].burst;
        r[ps[pick].pid].completion = t;
        finished[pick] = true; done++;
    }
    return pack("Priority (non-preemptive)", r, ps);
}

// ============================ Round Robin ===================================
Summary round_robin(vector<Process> ps, int quantum) {
    // Stable order by arrival so we enqueue arrivals deterministically.
    vector<int> order(ps.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b){
        return ps[a].arrival != ps[b].arrival ? ps[a].arrival < ps[b].arrival
                                               : ps[a].pid < ps[b].pid;
    });
    auto r = blank_results(ps);
    int n = ps.size(), done = 0, t = 0, idx = 0;
    vector<int> rem(n);
    for (int i = 0; i < n; ++i) rem[i] = ps[i].burst;
    queue<int> ready;                              // holds indices into ps
    auto enqueue_arrivals = [&](){
        while (idx < n && ps[order[idx]].arrival <= t) ready.push(order[idx++]);
    };
    while (done < n) {
        enqueue_arrivals();
        if (ready.empty()) {                       // jump to next arrival
            if (idx < n) { t = ps[order[idx]].arrival; continue; }
            break;
        }
        int i = ready.front(); ready.pop();
        if (r[ps[i].pid].start == -1) r[ps[i].pid].start = t;
        int run = min(quantum, rem[i]);
        t += run; rem[i] -= run;
        enqueue_arrivals();                        // arrivals during this slice join first
        if (rem[i] > 0) ready.push(i);             // then the preempted job re-queues
        else { r[ps[i].pid].completion = t; done++; }
    }
    return pack("Round Robin (q=" + to_string(quantum) + ")", r, ps);
}

// =========================== MLFQ ==========================================
//  K priority levels (0 = highest). New jobs enter at the top (Rule 3).
//  Within a level, 1-tick round robin (Rules 1 & 2). A job that uses up its
//  total allotment at a level is demoted (Rule 4 - charged on total time, not
//  per-burst, which is the anti-gaming form). Every BOOST ticks, all jobs are
//  lifted back to the top queue (Rule 5) to prevent starvation.
Summary mlfq(vector<Process> ps, vector<int> allotment, int boost) {
    int K = allotment.size();
    vector<int> order(ps.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b){
        return ps[a].arrival != ps[b].arrival ? ps[a].arrival < ps[b].arrival
                                               : ps[a].pid < ps[b].pid;
    });
    auto r = blank_results(ps);
    int n = ps.size(), done = 0, t = 0, idx = 0;
    vector<int> rem(n), level(n, 0), used(n, 0);
    for (int i = 0; i < n; ++i) rem[i] = ps[i].burst;
    vector<queue<int>> q(K);

    auto enqueue_arrivals = [&](){
        while (idx < n && ps[order[idx]].arrival <= t) {
            int i = order[idx++]; level[i] = 0; used[i] = 0; q[0].push(i);
        }
    };
    while (done < n) {
        enqueue_arrivals();
        if (boost > 0 && t > 0 && t % boost == 0) {     // Rule 5: priority boost
            vector<int> all;
            for (int l = 0; l < K; ++l)
                while (!q[l].empty()) { all.push_back(q[l].front()); q[l].pop(); }
            for (int i : all) { level[i] = 0; used[i] = 0; q[0].push(i); }
        }
        int lvl = -1;
        for (int l = 0; l < K; ++l) if (!q[l].empty()) { lvl = l; break; }
        if (lvl == -1) { t++; continue; }               // idle tick
        int i = q[lvl].front(); q[lvl].pop();
        if (r[ps[i].pid].start == -1) r[ps[i].pid].start = t;
        rem[i]--; used[i]++; t++;
        enqueue_arrivals();
        if (rem[i] == 0) { r[ps[i].pid].completion = t; done++; continue; }
        if (used[i] >= allotment[lvl]) {                 // Rule 4: demote
            int nl = min(lvl + 1, K - 1);
            level[i] = nl; used[i] = 0; q[nl].push(i);
        } else {
            q[lvl].push(i);                              // RR within level
        }
    }
    return pack("MLFQ", r, ps);
}

// ============================ Reporting =====================================
void print_summary(const Summary& s) {
    cout << "\n=== " << s.policy << " ===\n";
    cout << left << setw(6) << "PID" << setw(9) << "Arrival" << setw(7) << "Burst"
         << setw(7) << "Start" << setw(7) << "Finish" << setw(11) << "Turnaround"
         << setw(10) << "Response" << setw(9) << "Waiting" << "\n";
    auto js = s.jobs;
    sort(js.begin(), js.end(), [](const JobResult&a, const JobResult&b){return a.pid<b.pid;});
    for (auto& j : js)
        cout << left << setw(6) << j.pid << setw(9) << j.arrival << setw(7) << j.burst
             << setw(7) << j.start << setw(7) << j.completion << setw(11) << j.turnaround()
             << setw(10) << j.response() << setw(9) << j.waiting() << "\n";
    cout << fixed << setprecision(2);
    cout << "Averages -> turnaround: " << s.avg_turnaround
         << " | response: " << s.avg_response
         << " | waiting: " << s.avg_waiting << "\n";
    cout << "CPU utilization: " << s.utilization << "%  | throughput: "
         << s.throughput << " jobs/unit\n";
}

void print_comparison(const vector<Summary>& all) {
    cout << "\n\n############ SIDE-BY-SIDE COMPARISON (same workload) ############\n";
    cout << left << setw(28) << "Policy" << right << setw(13) << "AvgTurn"
         << setw(13) << "AvgResp" << setw(13) << "AvgWait"
         << setw(9) << "Util%" << setw(12) << "Thruput" << "\n";
    cout << string(90, '-') << "\n";
    cout << fixed << setprecision(2);
    for (auto& s : all)
        cout << left << setw(28) << s.policy << right << setw(13) << s.avg_turnaround
             << setw(13) << s.avg_response << setw(13) << s.avg_waiting
             << setw(9) << s.utilization << setw(12) << s.throughput << "\n";
    cout << "\nReadout: SJF/STCF minimize turnaround; Round Robin minimizes\n"
            "response time; MLFQ balances both without knowing burst lengths.\n";
}

vector<Process> load_workload(const string& path) {
    vector<Process> ps;
    ifstream f(path);
    if (!f) { cerr << "Could not open " << path << "\n"; exit(1); }
    int pid, a, b, pr;
    while (f >> pid >> a >> b >> pr) ps.push_back({pid, a, b, pr});
    return ps;
}

int main(int argc, char** argv) {
    vector<Process> ps;
    if (argc > 1) {
        ps = load_workload(argv[1]);
    } else {
        // Built-in sample: pid, arrival, burst, priority (lower = higher prio).
        ps = {
            {1, 0, 8, 3},
            {2, 1, 4, 1},
            {3, 2, 9, 4},
            {4, 3, 5, 2},
            {5, 4, 2, 5},
        };
    }
    cout << "Workload (" << ps.size() << " processes): pid/arrival/burst/priority\n";
    for (auto& p : ps)
        cout << "  P" << p.pid << "  arr=" << p.arrival << " burst=" << p.burst
             << " prio=" << p.priority << "\n";

    vector<Summary> all;
    all.push_back(fcfs(ps));
    all.push_back(sjf(ps));
    all.push_back(stcf(ps));
    all.push_back(priority_np(ps));
    all.push_back(round_robin(ps, 2));
    all.push_back(mlfq(ps, {4, 8, 16}, 30));   // 3 levels, allotments, boost@30

    for (auto& s : all) print_summary(s);
    print_comparison(all);
    return 0;
}
