// CPU-only controls with Dawn's actual ErrorSink, ErrorAdapter and error scopes.
#include "src/utils/assert.h"
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "src/dawn/native/ErrorScope.h"
#include "src/dawn/native/ErrorSink.h"
#include "src/dawn/native/webgpu/InnerRuntimeError.h"

namespace dawn::native {
WGPU_IMPORT_BITMASK_OPERATORS
using namespace webgpu;

class Sink final : public ErrorSink {
  public:
    ErrorScopeStack scopes;
    std::vector<std::pair<wgpu::ErrorType, std::string>> events;
    bool lost = false;

    void ConsumeErrorImpl(std::unique_ptr<UnknownError> error,
                          InternalErrorType allowed) override {
        auto type = error->GetType();
        if (!(type & (allowed | InternalErrorType::Validation))) {
            lost = true;
            return;
        }
        auto publicType = ToWGPUErrorType(type);
        const auto& message = error->GetData()->GetMessage();
        if (!scopes.HandleError(publicType, message)) {
            events.emplace_back(publicType, message);
        }
    }
};

ResultOrError<void*> Construct(InnerRuntimeErrorCapture& errors, bool created) {
    DAWN_TRY(errors.Check(created, "controlled resource creation"));
    // Dawn Result<T*> uses low address bits for its error tag. A success
    // fixture must have the same alignment as a real allocated API handle.
    alignas(16) static char successfulHandle;
    return &successfulHandle;
}

int RunControls() {
    constexpr auto allowed =
        InternalErrorType::OutOfMemory | InternalErrorType::PipelineUncategorized;
    for (auto [nativeType, filter, publicType] : {
             std::tuple{WGPUErrorType_Validation, wgpu::ErrorFilter::Validation,
                        wgpu::ErrorType::Validation},
             std::tuple{WGPUErrorType_OutOfMemory, wgpu::ErrorFilter::OutOfMemory,
                        wgpu::ErrorType::OutOfMemory},
             std::tuple{WGPUErrorType_Internal, wgpu::ErrorFilter::Internal,
                        wgpu::ErrorType::Internal}}) {
        for (bool scoped : {false, true}) {
            Sink sink;
            if (scoped) sink.scopes.Push(filter);
            InnerRuntimeErrorCapture errors;
            std::string message = "provider rejected creation";
            errors.Record(nativeType, {message.data(), message.size()});
            message.assign(message.size(), 'x');
            // Preserve the first provider error and its copied bytes.
            errors.Record(WGPUErrorType_Validation, {"later error", 11});
            void* object = nullptr;
            size_t forwardedCommands = 0;
            if (!sink.ConsumedError(Construct(errors, false), &object, allowed)) {
                ++forwardedCommands;
            }
            DAWN_CHECK(!sink.lost && !object && forwardedCommands == 0);
            if (scoped) {
                auto captured = sink.scopes.Pop();
                DAWN_CHECK(captured.GetErrorType() == publicType);
                DAWN_CHECK(ToString(captured.GetErrorMessage()) == "provider rejected creation");
                DAWN_CHECK(sink.events.empty());
            } else {
                DAWN_CHECK(sink.events.size() == 1);
                DAWN_CHECK(sink.events[0].first == publicType);
                DAWN_CHECK(sink.events[0].second == "provider rejected creation");
            }
        }
    }
    // A null object without a provider error is a backend contract violation.
    Sink unreported;
    InnerRuntimeErrorCapture missing;
    void* object = nullptr;
    DAWN_CHECK(unreported.ConsumedError(Construct(missing, false), &object, allowed));
    DAWN_CHECK(unreported.lost && !object);
    // NoError never invents an error, and a real object can be published.
    Sink success;
    InnerRuntimeErrorCapture valid;
    valid.Record(WGPUErrorType_NoError, {"", 0});
    DAWN_CHECK(!success.ConsumedError(Construct(valid, true), &object, allowed));
    DAWN_CHECK(object && success.events.empty() && !success.lost);
    return 0;
}

}  // namespace dawn::native

int main() { return dawn::native::RunControls(); }
