// Host-side replay evidence only; never linked into the emulator.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace Xlle.Replay {
    public sealed class ProcessIdentity {
        public int pid;
        public string name;
        public string startTime;
        public long start_ticks;
        public string Key { get { return pid + ":" + start_ticks; } }
    }

    public sealed class ObserverEvidence {
        public string schema = "xboxlle.process-observer.v1";
        public string mode = "sampled";
        public bool started, completed, healthy, covered_leg, degraded;
        public string[] errors;
        public int samples, interval_ms;
        public double max_gap_ms, allowed_gap_ms, sample_work_ms, observed_ms;
        public string started_at, stopped_at;
        public ProcessIdentity[] before, after, during, owned, residents;
    }

    public sealed class ProcessObserver : IDisposable {
        readonly object gate = new object();
        readonly Regex pattern;
        readonly int interval;
        readonly double allowedGap;
        readonly ManualResetEvent stop = new ManualResetEvent(false);
        readonly Dictionary<string, ProcessIdentity> found = new Dictionary<string, ProcessIdentity>();
        readonly Dictionary<string, ProcessIdentity> owned = new Dictionary<string, ProcessIdentity>();
        readonly Dictionary<string, ProcessIdentity> residents = new Dictionary<string, ProcessIdentity>();
        readonly List<string> errors = new List<string>();
        ProcessIdentity[] before = new ProcessIdentity[0], after = new ProcessIdentity[0];
        Thread thread;
        bool started, completed, covered;
        ObserverEvidence finalEvidence;
        int samples;
        long startTick, lastTick;
        double maxGap, workMs;
        DateTime startUtc, stopUtc;

        public ProcessObserver(string processPattern, int intervalMs, double allowedGapMs) {
            if (intervalMs < 10 || intervalMs > 5000 ||
                double.IsNaN(allowedGapMs) || double.IsInfinity(allowedGapMs) ||
                allowedGapMs < intervalMs) throw new ArgumentException("invalid observer cadence");
            pattern = new Regex(processPattern, RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
            interval = intervalMs;
            allowedGap = allowedGapMs;
            using (Process self = Process.GetCurrentProcess()) AddOwned(self);
        }

        static double Milliseconds(long ticks) { return ticks * 1000.0 / Stopwatch.Frequency; }

        static ProcessIdentity Identify(Process process) {
            DateTime start = process.StartTime.ToUniversalTime();
            return new ProcessIdentity { pid = process.Id, name = process.ProcessName,
                startTime = start.ToString("o"), start_ticks = start.Ticks };
        }

        public void AddOwned(Process process) {
            ProcessIdentity identity = Identify(process);
            lock (gate) owned[identity.Key] = identity;
        }

        public void AddResident(int pid) {
            using (Process process = Process.GetProcessById(pid)) {
                ProcessIdentity identity = Identify(process);
                lock (gate) residents[identity.Key] = identity;
            }
        }

        public ProcessIdentity[] ResidentIdentities {
            get { lock (gate) return new List<ProcessIdentity>(residents.Values).ToArray(); }
        }

        public void AddResidentIdentity(ProcessIdentity identity) {
            if (identity == null || identity.pid <= 0 || identity.start_ticks <= 0)
                throw new ArgumentException("resident needs PID and creation time");
            lock (gate) residents[identity.Key] = identity;
        }

        ProcessIdentity[] Capture() {
            var result = new List<ProcessIdentity>();
            foreach (Process process in Process.GetProcesses()) {
                using (process) {
                    int pid = 0;
                    string name = null;
                    try {
                        pid = process.Id;
                        name = process.ProcessName;
                        if (!pattern.IsMatch(name)) continue;
                        result.Add(Identify(process));
                    } catch (InvalidOperationException) {
                        // A matching process that exits during the sample is still
                        // evidence. Unknown identities are never granted exclusions.
                        if (name != null && pattern.IsMatch(name))
                            result.Add(new ProcessIdentity { pid = pid, name = name });
                    } catch (System.ComponentModel.Win32Exception) {
                        if (name != null && pattern.IsMatch(name))
                            result.Add(new ProcessIdentity { pid = pid, name = name });
                        else throw;
                    }
                }
            }
            return result.ToArray();
        }

        ProcessIdentity[] Filter(IEnumerable<ProcessIdentity> source) {
            var result = new List<ProcessIdentity>();
            foreach (ProcessIdentity p in source)
                if (!owned.ContainsKey(p.Key) && !residents.ContainsKey(p.Key)) result.Add(p);
            return result.ToArray();
        }

        void Sample() {
            long begin = Stopwatch.GetTimestamp();
            ProcessIdentity[] snapshot = Capture();
            long end = Stopwatch.GetTimestamp();
            lock (gate) {
                // Sample start-to-start gaps include scheduling delays and the
                // previous enumeration. The final sample also covers shutdown.
                if (lastTick != 0) maxGap = Math.Max(maxGap, Milliseconds(begin - lastTick));
                // Also bound enumeration itself, including the final sample.
                maxGap = Math.Max(maxGap, Milliseconds(end - begin));
                lastTick = begin;
                workMs += Milliseconds(end - begin);
                if (samples == 0) before = snapshot;
                after = snapshot;
                ++samples;
                foreach (ProcessIdentity p in snapshot) found[p.Key] = p;
            }
        }

        void Fail(Exception error) {
            lock (gate) errors.Add(error.GetType().Name + ": " + error.Message);
        }

        public void Start() {
            if (started) throw new InvalidOperationException("observer already started");
            startUtc = DateTime.UtcNow;
            startTick = Stopwatch.GetTimestamp();
            started = true;
            try { Sample(); } catch (Exception error) { Fail(error); }
            thread = new Thread(delegate() {
                try {
                    while (!stop.WaitOne(interval)) Sample();
                    Sample();
                } catch (Exception error) { Fail(error); }
            });
            thread.IsBackground = true;
            thread.Name = "xlle replay process observer";
            thread.Start();
        }

        public bool Ready {
            get { lock (gate) return started && samples > 0 && errors.Count == 0 && thread != null && thread.IsAlive; }
        }

        public ProcessIdentity[] Before { get { lock (gate) return Filter(before); } }

        public ObserverEvidence Finish(bool legCovered) {
            if (finalEvidence != null) return finalEvidence;
            if (thread != null) {
                stop.Set();
                if (!thread.Join(5000)) {
                    lock (gate) errors.Add("observer did not stop within 5000 ms");
                } else completed = true;
            }
            stopUtc = DateTime.UtcNow;
            covered = legCovered && started && completed;
            lock (gate) {
                bool degraded = maxGap > allowedGap;
                finalEvidence = new ObserverEvidence {
                    started = started, completed = completed, covered_leg = covered,
                    healthy = started && completed && samples >= 2 && errors.Count == 0 && !degraded,
                    degraded = degraded, errors = errors.ToArray(), samples = samples,
                    interval_ms = interval, allowed_gap_ms = allowedGap, max_gap_ms = maxGap,
                    sample_work_ms = workMs, observed_ms = started ? Milliseconds(Stopwatch.GetTimestamp() - startTick) : 0,
                    started_at = started ? startUtc.ToString("o") : null, stopped_at = stopUtc.ToString("o"),
                    before = Filter(before), after = Filter(after), during = Filter(found.Values),
                    owned = new List<ProcessIdentity>(owned.Values).ToArray(),
                    residents = new List<ProcessIdentity>(residents.Values).ToArray()
                };
                return finalEvidence;
            }
        }

        public void Dispose() {
            if (thread != null && thread.IsAlive) Finish(false);
            if (thread == null || !thread.IsAlive) stop.Dispose();
        }

        // Windows CRT quoting. No shell parses this command line. Preserve empty
        // arguments, embedded quotes and backslashes preceding a quote/end quote.
        public static string QuoteArguments(string[] arguments) {
            var command = new StringBuilder();
            foreach (string arg in arguments) {
                if (command.Length != 0) command.Append(' ');
                command.Append('"');
                int slashes = 0;
                foreach (char ch in arg) {
                    if (ch == '\\') { ++slashes; continue; }
                    if (ch == '"') { command.Append('\\', slashes * 2 + 1); command.Append('"'); }
                    else { command.Append('\\', slashes); command.Append(ch); }
                    slashes = 0;
                }
                command.Append('\\', slashes * 2);
                command.Append('"');
            }
            return command.ToString();
        }
    }
}
