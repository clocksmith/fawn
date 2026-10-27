// CPU-only lifetime controls using Dawn's real internal/external reference counts.
#include <atomic>
#include <barrier>
#include <limits>
#include <thread>

#include "src/utils/assert.h"
#include "partition_alloc/pointers/raw_ptr.h"
#include "src/dawn/common/RefCountedWithExternalCount.h"
#include "dawn/platform/DawnPlatform.h"
#include "src/dawn/native/DeviceGuard.h"
#include "src/dawn/native/webgpu/InnerRuntimeCallbackRegistry.h"

namespace dawn::native::webgpu {
class Owner;
using Registry = InnerRuntimeCallbackRegistry<Owner>;

class Owner final : public RefCountedWithExternalCount<RefCounted> {
  public:
    Owner(Registry* registry, std::atomic<unsigned>* destroyed)
        : registry(registry), destroyed(destroyed) {}
    uintptr_t token = 0;
    void Touch() { touches.fetch_add(1); }

  private:
    ~Owner() override { destroyed->fetch_add(1); }
    void WillDropLastExternalRef() override {
        // DeviceBase uses this exact external-ref transition to call Destroy.
        Ref<Owner> retired = registry->Remove(token);
    }
    raw_ptr<Registry> registry;
    raw_ptr<std::atomic<unsigned>> destroyed;
    std::atomic<unsigned> touches{0};
};

Owner* Create(Registry* registry, std::atomic<unsigned>* destroyed) {
    Owner* owner = new Owner(registry, destroyed);
    owner->IncrementExternalRefCount(); // The initial reference is returned to the API.
    owner->token = registry->Insert(owner);
    DAWN_CHECK(owner->token != 0);
    return owner;
}

class Reentrant;
using ReentrantRegistry = InnerRuntimeCallbackRegistry<Reentrant>;
class Reentrant final : public RefCounted {
  public:
    Reentrant(ReentrantRegistry* registry, bool* destroyed)
        : registry(registry), destroyed(destroyed) {}
    uintptr_t token = 0;
  private:
    ~Reentrant() override {
        DAWN_CHECK(!registry->Lookup(token));
        DAWN_CHECK(!registry->Remove(token));
        *destroyed = true;
    }
    raw_ptr<ReentrantRegistry> registry;
    raw_ptr<bool> destroyed;
};

class Clock final : public dawn::platform::Platform {
  public:
    double MonotonicallyIncreasingTime() override { return ++calls; }
    std::atomic<unsigned> calls{0};
};

void RunPublicationControls() {
    using Lock = DeviceMutex::AutoLockBase<DeviceMutex*>;
    Clock clock;
    Ref<DeviceMutex> mutex = AcquireRef(new DeviceMutex(&clock));
    // Exercise the actual recursive DeviceMutex used by DeviceGuard, including
    // a synchronous provider callback while initialization owns the guard.
    InnerRuntimePublication synchronous;
    {
        Lock initialization(mutex.Get());
        const auto calls = clock.calls.load();
        {
            Lock callback(mutex.Get());
            DAWN_CHECK(clock.calls == calls); // Same mutex, recursive acquisition.
            DAWN_CHECK(!synchronous.MarkLost());
        }
        DAWN_CHECK(!synchronous.Publish());
    }
    // A loss arriving on another thread marks publication failed BEFORE it
    // waits on the real initialization mutex. Never wait for it while guarded.
    InnerRuntimePublication concurrent;
    std::atomic<bool> arrived{false}, entered{false};
    std::thread callback;
    {
        Lock initialization(mutex.Get());
        callback = std::thread([&] {
            DAWN_CHECK(!concurrent.MarkLost());
            arrived = true;
            Lock callbackGuard(mutex.Get());
            entered = true;
        });
        while (!arrived.load()) std::this_thread::yield();
        DAWN_CHECK(!entered && !concurrent.Publish());
    }
    callback.join();
    DAWN_CHECK(entered);
    // A published loss uses the existing error/loss route once; late lookup
    // cannot reacquire its removed registry entry (tested below).
    InnerRuntimePublication published;
    DAWN_CHECK(published.Publish() && published.IsReady());
    DAWN_CHECK(published.MarkLost() && !published.IsReady());
    InnerRuntimePublication destroyed;
    destroyed.Retire();
    DAWN_CHECK(!destroyed.Publish() && !destroyed.MarkLost());
}

int RunCallbackControls() {
    RunPublicationControls();
    Registry registry;
    std::atomic<unsigned> destroyed{0};
    Owner* first = Create(&registry, &destroyed);
    uintptr_t old = first->token;
    // Retain an actual in-flight callback lease while the final API ref retires.
    Ref<Owner> error = registry.Lookup(old);
    first->APIRelease();
    DAWN_CHECK(destroyed == 0 && !registry.Lookup(old));
    error->Touch();
    error = nullptr;
    DAWN_CHECK(destroyed == 1);
    Owner* second = Create(&registry, &destroyed);
    DAWN_CHECK(second->token != old && !registry.Lookup(old));
    second->APIRelease();
    DAWN_CHECK(destroyed == 2);

    for (unsigned i = 0; i < 128; ++i) {
        Owner* owner = Create(&registry, &destroyed);
        uintptr_t token = owner->token;
        std::barrier start(3);
        std::thread errorThread([&] {
            start.arrive_and_wait();
            Ref<Owner> lease = registry.Lookup(token);
            if (lease) lease->Touch();
        });
        std::thread lostThread([&] {
            start.arrive_and_wait();
            Ref<Owner> lease = registry.Remove(token);
            if (lease) lease->Touch();
        });
        start.arrive_and_wait();
        owner->APIRelease();
        errorThread.join();
        lostThread.join();
        DAWN_CHECK(!registry.Lookup(token) && !registry.Remove(token));
        DAWN_CHECK(destroyed == i + 3); // No per-device retained registry payload.
    }
    ReentrantRegistry nested;
    bool reentrantDestroyed = false;
    Ref<Reentrant> reentrant = AcquireRef(new Reentrant(&nested, &reentrantDestroyed));
    auto token = nested.Insert(reentrant.Get());
    reentrant->token = token;
    reentrant = nullptr;
    nested.Remove(token); // Destructor reenters both operations outside their mutex.
    DAWN_CHECK(reentrantDestroyed);

    uintptr_t counter = std::numeric_limits<uintptr_t>::max();
    DAWN_CHECK(NextInnerRuntimeCallbackToken(counter) == std::numeric_limits<uintptr_t>::max());
    DAWN_CHECK(counter == 0 && NextInnerRuntimeCallbackToken(counter) == 0);
    return 0;
}
}  // namespace dawn::native::webgpu

int main() { return dawn::native::webgpu::RunCallbackControls(); }
