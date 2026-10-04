// NoRain's presenter-worker ownership model: queued inputs are owned, rendering
// is ordered, and required work is never discarded under backpressure.
#include "host/nv2a_hle_worker.hpp"
#include "machine/nv2a_hle_draw.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace xlle {
namespace {
bool overlaps(uint32_t a, size_t n, const Nv2aHleSurface &s) {
  return uint64_t(a) < uint64_t(s.address) + size_t(s.pitch) * s.height &&
         uint64_t(s.address) < uint64_t(a) + n;
}
class Worker final : public Nv2aRenderer {
  std::unique_ptr<Nv2aRenderer> renderer_;
  std::function<void()> release_;
  struct Job { std::function<void()> run; size_t bytes; };
  std::deque<Job> jobs_;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::condition_variable ready_, space_;
  size_t bytes_ = 0;
  bool stop_ = false;
  mutable bool reported_ = false;
  std::string failure_;
  // Producer-owned logical residency includes earlier accepted queued draws.
  // It does not expose completed pixels; all readbacks run behind a fence.
  std::vector<Nv2aHleSurface> planes_;
  uint64_t draws_ = 0, fences_ = 0, backpressure_ = 0, max_jobs_ = 0;
  uint64_t busy_ns_ = 0, wait_ns_ = 0;
  void check_locked() const {
    if (!failure_.empty()) {
      reported_ = true;
      throw std::runtime_error("HLE worker: " + failure_);
    }
  }
  void check() const { std::lock_guard lock(mutex_); check_locked(); }
  void enqueue(std::function<void()> work, size_t bytes = 0) {
    std::unique_lock lock(mutex_);
    if (jobs_.size() >= 128 || (bytes_ && bytes_ + bytes > 64 * 1024 * 1024))
      ++backpressure_;
    space_.wait(lock, [&] {
      return !failure_.empty() || (jobs_.size() < 128 &&
             (!bytes_ || bytes_ + bytes <= 64 * 1024 * 1024));
    });
    check_locked();
    bytes_ += bytes;
    jobs_.push_back({std::move(work), bytes});
    max_jobs_ = std::max(max_jobs_, uint64_t(jobs_.size()));
    ready_.notify_one();
  }
  void sync(std::function<void()> work) {
    ++fences_;
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    enqueue([work = std::move(work), done] {
      try { work(); done->set_value(); }
      catch (...) { done->set_exception(std::current_exception()); }
    });
    const auto start = std::chrono::steady_clock::now();
    future.get();
    wait_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
    check();
  }
  bool resident(bool depth, uint32_t a, size_t n, uint32_t pitch,
                uint32_t h, uint32_t fmt) const {
    check();
    for (const auto &p : planes_)
      if (p.depth == depth && p.address == a && size_t(p.pitch) * p.height == n &&
          p.pitch == pitch && p.height == h && (!fmt || p.format == fmt)) return true;
    return false;
  }
  void remember(const Nv2aHleSurface &s) {
    if (!s.width || resident(s.depth, s.address, size_t(s.pitch)*s.height, s.pitch, s.height, s.format)) return;
    planes_.erase(std::remove_if(planes_.begin(), planes_.end(), [&](const auto &p) {
      return overlaps(s.address, size_t(s.pitch)*s.height, p);
    }), planes_.end());
    auto desc = s; desc.bytes.clear(); desc.bytes.shrink_to_fit();
    planes_.push_back(std::move(desc));
  }
  bool any(bool depth, uint32_t a, size_t n) const {
    return std::any_of(planes_.begin(), planes_.end(), [&](const auto &p) {
      return p.depth == depth && overlaps(a,n,p);
    });
  }
  void invalidate(bool depth, uint32_t a, size_t n, uint32_t cause) {
    check();
    if (!any(depth,a,n)) return;
    planes_.erase(std::remove_if(planes_.begin(), planes_.end(), [&](const auto &p) {
      return p.depth == depth && overlaps(a,n,p);
    }), planes_.end());
    enqueue([=,this] {
      if (depth) renderer_->invalidate_resident_depth_surface(a,n,cause);
      else renderer_->invalidate_resident_color_surface(a,n,cause);
    });
  }
public:
  Worker(std::unique_ptr<Nv2aRenderer> renderer, std::function<void()> release)
      : renderer_(std::move(renderer)), release_(std::move(release)) {
    release_(); // Detach on creator; window lifetime stays on creator thread.
    thread_ = std::thread([this] {
      for (;;) {
        Job job;
        {
          std::unique_lock lock(mutex_);
          ready_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
          if (jobs_.empty()) break;
          job = std::move(jobs_.front()); jobs_.pop_front();
        }
        auto start = std::chrono::steady_clock::now();
        // Keep executing fence promises even after a draw failure so callers
        // cannot deadlock. Draw lambdas check the sticky failure before work.
        try { job.run(); }
        catch (const std::exception &e) {
          std::lock_guard lock(mutex_); if (failure_.empty()) failure_ = e.what();
        }
        busy_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - start).count();
        { std::lock_guard lock(mutex_); bytes_ -= job.bytes; }
        space_.notify_all();
      }
      release_();
    });
  }
  ~Worker() override {
    { std::lock_guard lock(mutex_); stop_ = true; }
    ready_.notify_one(); thread_.join();
    std::printf("[hle-worker] draws=%llu fences=%llu backpressure=%llu max-queue=%llu busy-ns=%llu wait-ns=%llu\n",
      (unsigned long long)draws_, (unsigned long long)fences_,
      (unsigned long long)backpressure_, (unsigned long long)max_jobs_,
      (unsigned long long)busy_ns_, (unsigned long long)wait_ns_);
    if (!failure_.empty() && !reported_) {
      std::fprintf(stderr,"Unobserved HLE worker failure: %s\n",failure_.c_str());
      std::terminate(); // Never report success for a failed final queued draw.
    }
  }
  const char *name() const override { return "hle-native-worker"; }
  bool supports_generic_combiner(const Nv2aGenericCombinerPacket &) const override { return false; }
  bool submit_generic_combiner(const Nv2aGenericCombinerPacket &, Nv2aGenericCombinerResult &, std::string &e) override {
    e = "LLE packet submitted to HLE worker"; return false;
  }
  bool requires_resident_target_switch_resolve() const override { return false; }
  bool has_resident_color_surface(uint32_t a,size_t n,uint32_t pitch,uint32_t h,uint32_t fmt) const override { return resident(false,a,n,pitch,h,fmt); }
  bool has_resident_depth_surface(uint32_t a,size_t n,uint32_t pitch,uint32_t h,uint32_t fmt) const override { return resident(true,a,n,pitch,h,fmt); }
  bool submit_hle_draw(const Nv2aHleDraw &p, std::string &error) override {
    try {
      check();
      auto owned = std::make_shared<Nv2aHleDraw>(p);
      size_t bytes = sizeof(p) + p.raw_vertices.size() + p.vertices.size()*sizeof(Nv2aHleDraw::Vertex) +
                     p.indices.size()*4 + p.state.size()*4 + p.program.size()*16 + p.color.bytes.size() + p.depth.bytes.size();
      for (const auto &t : p.textures) bytes += t.bytes.size() + t.palette.size();
      if (!p.indices.empty()) { remember(p.color); remember(p.depth); }
      enqueue([this,owned] {
        { std::lock_guard lock(mutex_); if (!failure_.empty()) return; }
        std::string error;
        if (!renderer_->submit_hle_draw(*owned,error)) throw std::runtime_error(error);
      }, bytes);
      ++draws_; return true; // Acceptance, not a completion or rendered-pixel promise.
    } catch (const std::exception &e) { error=e.what(); return false; }
  }
  bool resolve_resident_color_surface(uint32_t a,size_t n,Nv2aResidentColorResolve &out,std::string &error) override {
    try { check(); out={}; if (!any(false,a,n)) return true;
      bool ok=false; sync([&]{ok=renderer_->resolve_resident_color_surface(a,n,out,error);}); return ok;
    } catch (const std::exception &e) { error=e.what(); return false; }
  }
  bool resolve_resident_depth_surface(uint32_t a,size_t n,Nv2aResidentDepthResolve &out,std::string &error) override {
    try { check(); out={}; if (!any(true,a,n)) return true;
      bool ok=false; sync([&]{ok=renderer_->resolve_resident_depth_surface(a,n,out,error);}); return ok;
    } catch (const std::exception &e) { error=e.what(); return false; }
  }
  void invalidate_resident_color_surface(uint32_t a,size_t n,uint32_t c) override { invalidate(false,a,n,c); }
  void invalidate_resident_depth_surface(uint32_t a,size_t n,uint32_t c) override { invalidate(true,a,n,c); }
  void kick_pending_gpu_work() override { enqueue([this]{renderer_->kick_pending_gpu_work();}); }
  bool clear_resident_surface(uint32_t ca,size_t cn,bool cc,uint32_t cv,uint32_t da,size_t dn,bool cd,uint32_t dv,bool &fill,std::string &error) override {
    try { bool ok=false; sync([&]{ok=renderer_->clear_resident_surface(ca,cn,cc,cv,da,dn,cd,dv,fill,error);}); return ok;
    } catch (const std::exception &e) { error=e.what(); return false; }
  }
};
} // namespace
std::unique_ptr<Nv2aRenderer> wrap_hle_worker(std::unique_ptr<Nv2aRenderer> r, std::function<void()> release) {
  return std::make_unique<Worker>(std::move(r),std::move(release));
}
} // namespace xlle
