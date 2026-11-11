#include "doe_ort_ep.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace doe::ort_ep {

namespace {

constexpr const char* kCompatibilityInfo = "doe-ort-basic-ops-ep-v7";
constexpr const char* kOnnxDomain = "";
constexpr const char* kOnnxAiDomain = "ai.onnx";
constexpr const char* kGemmAlphaAttribute = "alpha";
constexpr const char* kGemmBetaAttribute = "beta";
constexpr const char* kGemmTransAAttribute = "transA";
constexpr const char* kGemmTransBAttribute = "transB";
constexpr const char* kAxisAttribute = "axis";
constexpr ONNXTensorElementDataType kSupportedElementType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
constexpr size_t kUnaryInputCount = 1;
constexpr size_t kBinaryInputCount = 2;
constexpr size_t kTernaryInputCount = 3;
constexpr size_t kQuinaryInputCount = 5;
constexpr size_t kSingleOutputCount = 1;
constexpr size_t kMatrixRank = 2;
constexpr size_t kNoInferredInput = std::numeric_limits<size_t>::max();
constexpr float kSupportedGemmAlpha = 1.0f;
constexpr float kSupportedGemmBeta = 1.0f;
constexpr int64_t kSupportedGemmTransA = 0;
constexpr int64_t kSupportedGemmTransB = 0;
constexpr float kDefaultLayerNormEpsilon = 1e-5f;

enum class CompiledOpKind : uint8_t {
  kIdentity = 0,
  kAdd,
  kRelu,
  kSigmoid,
  kTanh,
  kGelu,
  kMatMul,
  kGemm,
  kGemmReluGemm,
  kMatMulAdd,
  kMatMulAddRelu,
  kAddRelu,
  kSoftmax,
  kLayerNorm,
  kConcat,
};

constexpr size_t kCompiledOpKindCount = 15;

constexpr size_t OpIndex(const CompiledOpKind kind) {
  return static_cast<size_t>(kind);
}

struct ClaimIncrement {
  CompiledOpKind kind;
  uint8_t count;
};

struct OpKindMetadata {
  CompiledOpKind kind;
  const char* display_name;
  const char* single_node_op;
  size_t expected_inputs;
  size_t claimed_node_count;
  std::array<ClaimIncrement, 3> claim_breakdown;
};

constexpr std::array<OpKindMetadata, kCompiledOpKindCount> kOpMetadata{{
    {CompiledOpKind::kIdentity, "Identity", "Identity", kUnaryInputCount, 1,
     {{{CompiledOpKind::kIdentity, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kAdd, "Add", "Add", kBinaryInputCount, 1,
     {{{CompiledOpKind::kAdd, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kRelu, "Relu", "Relu", kUnaryInputCount, 1,
     {{{CompiledOpKind::kRelu, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kSigmoid, "Sigmoid", "Sigmoid", kUnaryInputCount, 1,
     {{{CompiledOpKind::kSigmoid, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kTanh, "Tanh", "Tanh", kUnaryInputCount, 1,
     {{{CompiledOpKind::kTanh, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kGelu, "Gelu", "Gelu", kUnaryInputCount, 1,
     {{{CompiledOpKind::kGelu, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kMatMul, "MatMul", "MatMul", kBinaryInputCount, 1,
     {{{CompiledOpKind::kMatMul, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kGemm, "Gemm", "Gemm", kTernaryInputCount, 1,
     {{{CompiledOpKind::kGemm, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kGemmReluGemm, "Gemm->Relu->Gemm", nullptr, kQuinaryInputCount, 3,
     {{{CompiledOpKind::kGemm, 2}, {CompiledOpKind::kRelu, 1}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kMatMulAdd, "MatMul->Add", nullptr, kTernaryInputCount, 2,
     {{{CompiledOpKind::kMatMul, 1}, {CompiledOpKind::kAdd, 1}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kMatMulAddRelu, "MatMul->Add->Relu", nullptr, kTernaryInputCount, 3,
     {{{CompiledOpKind::kMatMul, 1}, {CompiledOpKind::kAdd, 1}, {CompiledOpKind::kRelu, 1}}}},
    {CompiledOpKind::kAddRelu, "Add->Relu", nullptr, kBinaryInputCount, 2,
     {{{CompiledOpKind::kAdd, 1}, {CompiledOpKind::kRelu, 1}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kSoftmax, "Softmax", "Softmax", kUnaryInputCount, 1,
     {{{CompiledOpKind::kSoftmax, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kLayerNorm, "LayerNormalization", "LayerNormalization", kBinaryInputCount, 1,
     {{{CompiledOpKind::kLayerNorm, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
    {CompiledOpKind::kConcat, "Concat", "Concat", kBinaryInputCount, 1,
     {{{CompiledOpKind::kConcat, 1}, {CompiledOpKind::kIdentity, 0}, {CompiledOpKind::kIdentity, 0}}}},
}};

std::atomic<uint64_t> g_get_capability_calls{0};
std::atomic<uint64_t> g_claimed_nodes{0};
std::atomic<uint64_t> g_compile_calls{0};
std::atomic<uint64_t> g_create_state_calls{0};
std::atomic<uint64_t> g_compute_calls{0};
std::atomic<uint64_t> g_release_state_calls{0};
std::array<std::atomic<uint64_t>, kCompiledOpKindCount> g_claimed_op_nodes{};
std::array<std::atomic<uint64_t>, kCompiledOpKindCount> g_compiled_op_groups{};
std::array<std::atomic<uint64_t>, kCompiledOpKindCount> g_compute_op_calls{};

struct TensorValueDescriptor {
  const OrtValueInfo* value_info = nullptr;
  std::string name;
  ONNXTensorElementDataType element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
  std::vector<int64_t> dims;
  size_t element_count = 0;
};

struct SupportedNodeSpec {
  const OrtNode* node = nullptr;
  CompiledOpKind kind = CompiledOpKind::kIdentity;
  std::string node_name;
  std::vector<TensorValueDescriptor> inputs;
  std::vector<TensorValueDescriptor> outputs;
};

struct SupportedGraphSpec {
  CompiledOpKind kind = CompiledOpKind::kIdentity;
  std::string default_node_name;
  std::vector<const OrtNode*> fused_nodes;
  std::vector<std::string> logical_input_names;
};

struct CompiledComputeState {
  std::string node_name;
  CompiledOpKind op_kind = CompiledOpKind::kIdentity;
};

struct CompiledNodeComputeInfo {
  OrtNodeComputeInfo base{};
  ApiBindings bindings{};
  std::string node_name;
  CompiledOpKind op_kind = CompiledOpKind::kIdentity;
  std::array<size_t, kQuinaryInputCount> input_binding_indices{0, 1, 2, 3, 4};
  size_t input_binding_count = 0;
};

struct RuntimeTensorDescriptor {
  ONNXTensorElementDataType element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
  std::vector<int64_t> dims;
  size_t element_count = 0;
};

CompiledNodeComputeInfo& ComputeInfoSelf(OrtNodeComputeInfo* this_ptr) {
  return *reinterpret_cast<CompiledNodeComputeInfo*>(this_ptr);
}

OrtStatus* MakeInvalidArgumentStatus(const OrtApi* api, const char* message) {
  return MakeStatus(api, ORT_INVALID_ARGUMENT, message);
}

OrtStatus* MakeInvalidGraphStatus(const OrtApi* api, const std::string& message) {
  return MakeStatus(api, ORT_INVALID_GRAPH, message.c_str());
}

OrtStatus* MakeNotImplementedStatus(const OrtApi* api, const std::string& message) {
  return MakeStatus(api, ORT_NOT_IMPLEMENTED, message.c_str());
}

const char* CompiledOpKindName(const CompiledOpKind kind) {
  const size_t idx = OpIndex(kind);
  return idx < kOpMetadata.size() ? kOpMetadata[idx].display_name : "unknown";
}

size_t ExpectedInputCountForKind(const CompiledOpKind kind) {
  const size_t idx = OpIndex(kind);
  return idx < kOpMetadata.size() ? kOpMetadata[idx].expected_inputs : 0;
}

bool LookupSingleNodeOp(const std::string_view operator_type, CompiledOpKind* kind_out, size_t* expected_inputs_out) {
  for (const OpKindMetadata& entry : kOpMetadata) {
    if (entry.single_node_op != nullptr && operator_type == entry.single_node_op) {
      *kind_out = entry.kind;
      *expected_inputs_out = entry.expected_inputs;
      return true;
    }
  }
  return false;
}

bool IsSupportedOnnxDomain(const char* domain) {
  if (domain == nullptr) {
    return true;
  }
  const std::string_view domain_view(domain);
  return domain_view == kOnnxDomain || domain_view == kOnnxAiDomain;
}

void IncrementClaimCounters(const CompiledOpKind kind) {
  const size_t idx = OpIndex(kind);
  if (idx >= kOpMetadata.size()) {
    return;
  }
  const OpKindMetadata& meta = kOpMetadata[idx];
  g_claimed_nodes.fetch_add(meta.claimed_node_count, std::memory_order_relaxed);
  for (const ClaimIncrement& inc : meta.claim_breakdown) {
    if (inc.count != 0) {
      g_claimed_op_nodes[OpIndex(inc.kind)].fetch_add(inc.count, std::memory_order_relaxed);
    }
  }
}

void IncrementCompiledGroupCounters(const CompiledOpKind kind) {
  const size_t idx = OpIndex(kind);
  if (idx < g_compiled_op_groups.size()) {
    g_compiled_op_groups[idx].fetch_add(1, std::memory_order_relaxed);
  }
}

void IncrementComputeCounters(const CompiledOpKind kind) {
  const size_t idx = OpIndex(kind);
  if (idx < g_compute_op_calls.size()) {
    g_compute_op_calls[idx].fetch_add(1, std::memory_order_relaxed);
  }
}

std::string ShapeString(const std::vector<int64_t>& dims) {
  std::ostringstream shape;
  shape << '[';
  for (size_t index = 0; index < dims.size(); ++index) {
    if (index != 0) {
      shape << ", ";
    }
    shape << dims[index];
  }
  shape << ']';
  return shape.str();
}

template <typename TensorDescriptor>
bool SameShape(const TensorDescriptor& lhs, const TensorDescriptor& rhs) {
  return lhs.dims == rhs.dims;
}

template <typename TensorDescriptor>
bool SameTypeAndShape(const TensorDescriptor& lhs, const TensorDescriptor& rhs) {
  return lhs.element_type == rhs.element_type && SameShape(lhs, rhs);
}

template <typename TensorDescriptor>
bool IsRank2Matrix(const TensorDescriptor& descriptor) {
  return descriptor.dims.size() == kMatrixRank;
}

template <typename TensorDescriptor>
bool IsStaticMatMulShape(
    const TensorDescriptor& lhs,
    const TensorDescriptor& rhs,
    const TensorDescriptor& output) {
  if (!IsRank2Matrix(lhs) || !IsRank2Matrix(rhs) || !IsRank2Matrix(output)) {
    return false;
  }
  if (lhs.element_type != rhs.element_type || lhs.element_type != output.element_type) {
    return false;
  }
  if (lhs.dims[1] != rhs.dims[0]) {
    return false;
  }
  return output.dims[0] == lhs.dims[0] && output.dims[1] == rhs.dims[1];
}

template <typename TensorDescriptor>
bool InferStaticMatMulOutput(
    const TensorDescriptor& lhs,
    const TensorDescriptor& rhs,
    TensorDescriptor* output_out) {
  if (output_out == nullptr) {
    return false;
  }
  if (!IsRank2Matrix(lhs) || !IsRank2Matrix(rhs) || lhs.element_type != rhs.element_type || lhs.dims[1] != rhs.dims[0]) {
    return false;
  }
  output_out->element_type = lhs.element_type;
  output_out->dims = {lhs.dims[0], rhs.dims[1]};
  output_out->element_count = static_cast<size_t>(lhs.dims[0] * rhs.dims[1]);
  return true;
}

template <typename Scalar>
OrtStatus* ReadOptionalScalarAttribute(
    const OrtApi* api,
    const OrtNode* node,
    const char* attribute_name,
    const OrtOpAttrType expected_type,
    const char* type_label,
    const char* missing_out_message,
    const Scalar default_value,
    Scalar* value_out) {
  if (value_out == nullptr) {
    return MakeInvalidArgumentStatus(api, missing_out_message);
  }
  *value_out = default_value;

  const OrtOpAttr* attribute = nullptr;
  OrtStatus* status = api->Node_GetAttributeByName(node, attribute_name, &attribute);
  if (status != nullptr || attribute == nullptr) {
    return status;
  }

  OrtOpAttrType attribute_type = ORT_OP_ATTR_UNDEFINED;
  status = api->OpAttr_GetType(attribute, &attribute_type);
  if (status != nullptr) return status;
  if (attribute_type != expected_type) {
    std::ostringstream message;
    message << "Doe ORT plugin EP Gemm slice expected " << type_label << " attribute '" << attribute_name << "'.";
    return MakeInvalidGraphStatus(api, message.str());
  }

  size_t values_read = 0;
  status = api->ReadOpAttr(attribute, expected_type, value_out, sizeof(*value_out), &values_read);
  if (status != nullptr) return status;
  if (values_read == 0) {
    std::ostringstream message;
    message << "Doe ORT plugin EP Gemm slice could not read " << type_label << " attribute '" << attribute_name << "'.";
    return MakeInvalidGraphStatus(api, message.str());
  }
  return nullptr;
}

OrtStatus* ReadOptionalFloatAttribute(
    const OrtApi* api,
    const OrtNode* node,
    const char* attribute_name,
    const float default_value,
    float* value_out) {
  return ReadOptionalScalarAttribute(
      api,
      node,
      attribute_name,
      ORT_OP_ATTR_FLOAT,
      "float",
      "Doe ORT plugin EP requires a value_out pointer when reading float node attributes.",
      default_value,
      value_out);
}

OrtStatus* ReadOptionalIntAttribute(
    const OrtApi* api,
    const OrtNode* node,
    const char* attribute_name,
    const int64_t default_value,
    int64_t* value_out) {
  return ReadOptionalScalarAttribute(
      api,
      node,
      attribute_name,
      ORT_OP_ATTR_INT,
      "int",
      "Doe ORT plugin EP requires a value_out pointer when reading int node attributes.",
      default_value,
      value_out);
}

OrtStatus* HasSupportedGemmAttributes(const OrtApi* api, const OrtNode* node, bool* is_supported) {
  if (is_supported == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires an is_supported output pointer when validating Gemm attributes.");
  }
  *is_supported = false;

  float alpha = kSupportedGemmAlpha;
  float beta = kSupportedGemmBeta;
  int64_t trans_a = kSupportedGemmTransA;
  int64_t trans_b = kSupportedGemmTransB;
  OrtStatus* status = ReadOptionalFloatAttribute(api, node, kGemmAlphaAttribute, kSupportedGemmAlpha, &alpha);
  if (status != nullptr) return status;
  status = ReadOptionalFloatAttribute(api, node, kGemmBetaAttribute, kSupportedGemmBeta, &beta);
  if (status != nullptr) return status;
  status = ReadOptionalIntAttribute(api, node, kGemmTransAAttribute, kSupportedGemmTransA, &trans_a);
  if (status != nullptr) return status;
  status = ReadOptionalIntAttribute(api, node, kGemmTransBAttribute, kSupportedGemmTransB, &trans_b);
  if (status != nullptr) return status;

  *is_supported = alpha == kSupportedGemmAlpha && beta == kSupportedGemmBeta && trans_a == kSupportedGemmTransA &&
                  trans_b == kSupportedGemmTransB;
  return nullptr;
}

OrtStatus* GetGraphNodes(const OrtApi* api, const OrtGraph* graph, std::vector<const OrtNode*>* nodes_out) {
  if (graph == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP cannot inspect a null OrtGraph.");
  }
  if (nodes_out == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP requires a nodes_out output vector.");
  }

  size_t num_nodes = 0;
  OrtStatus* status = api->Graph_GetNumNodes(graph, &num_nodes);
  if (status != nullptr) return status;

  nodes_out->assign(num_nodes, nullptr);
  return num_nodes == 0 ? nullptr : api->Graph_GetNodes(graph, nodes_out->data(), nodes_out->size());
}

OrtStatus* GetGraphInputs(const OrtApi* api, const OrtGraph* graph, std::vector<const OrtValueInfo*>* inputs_out) {
  if (graph == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP cannot inspect graph inputs for a null OrtGraph.");
  }
  if (inputs_out == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP requires an inputs_out output vector.");
  }

  size_t num_inputs = 0;
  OrtStatus* status = api->Graph_GetNumInputs(graph, &num_inputs);
  if (status != nullptr) return status;

  inputs_out->assign(num_inputs, nullptr);
  return num_inputs == 0 ? nullptr : api->Graph_GetInputs(graph, inputs_out->data(), inputs_out->size());
}

OrtStatus* GetValueInfoNameString(const OrtApi* api, const OrtValueInfo* value_info, std::string* name_out) {
  if (name_out == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP requires a name_out output pointer.");
  }
  name_out->clear();
  if (value_info == nullptr) {
    return nullptr;
  }

  const char* value_name = nullptr;
  OrtStatus* status = api->GetValueInfoName(value_info, &value_name);
  if (status != nullptr) return status;
  *name_out = value_name != nullptr ? value_name : "";
  return nullptr;
}

OrtStatus* GetNodeValueInfos(
    const OrtApi* api,
    const OrtNode* node,
    const bool want_inputs,
    std::vector<const OrtValueInfo*>* values_out) {
  if (node == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP cannot inspect a null OrtNode.");
  }
  if (values_out == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP requires a values_out output vector.");
  }

  size_t value_count = 0;
  OrtStatus* status =
      want_inputs ? api->Node_GetNumInputs(node, &value_count) : api->Node_GetNumOutputs(node, &value_count);
  if (status != nullptr) return status;

  values_out->assign(value_count, nullptr);
  if (value_count == 0) {
    return nullptr;
  }
  return want_inputs ? api->Node_GetInputs(node, values_out->data(), values_out->size())
                     : api->Node_GetOutputs(node, values_out->data(), values_out->size());
}

OrtStatus* DescribeSupportedTensorValue(
    const OrtApi* api,
    const OrtValueInfo* value_info,
    TensorValueDescriptor* descriptor_out,
    bool* is_supported) {
  if (descriptor_out == nullptr || is_supported == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires descriptor and is_supported output pointers when validating tensor values.");
  }
  *descriptor_out = TensorValueDescriptor{};
  *is_supported = false;
  if (value_info == nullptr) {
    return nullptr;
  }

  descriptor_out->value_info = value_info;
  OrtStatus* status = GetValueInfoNameString(api, value_info, &descriptor_out->name);
  if (status != nullptr) return status;

  const OrtTypeInfo* type_info = nullptr;
  status = api->GetValueInfoTypeInfo(value_info, &type_info);
  if (status != nullptr || type_info == nullptr) return status;

  ONNXType onnx_type = ONNX_TYPE_UNKNOWN;
  status = api->GetOnnxTypeFromTypeInfo(type_info, &onnx_type);
  if (status != nullptr || onnx_type != ONNX_TYPE_TENSOR) return status;

  const OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
  status = api->CastTypeInfoToTensorInfo(type_info, &tensor_info);
  if (status != nullptr || tensor_info == nullptr) return status;

  status = api->GetTensorElementType(tensor_info, &descriptor_out->element_type);
  if (status != nullptr || descriptor_out->element_type != kSupportedElementType) return status;

  size_t rank = 0;
  status = api->GetDimensionsCount(tensor_info, &rank);
  if (status != nullptr) return status;

  descriptor_out->dims.assign(rank, 0);
  if (rank != 0) {
    status = api->GetDimensions(tensor_info, descriptor_out->dims.data(), descriptor_out->dims.size());
    if (status != nullptr) return status;
  }
  for (const int64_t dim : descriptor_out->dims) {
    if (dim < 0) {
      return nullptr;
    }
  }

  status = api->GetTensorShapeElementCount(tensor_info, &descriptor_out->element_count);
  if (status != nullptr) return status;

  *is_supported = true;
  return nullptr;
}

OrtStatus* InspectSupportedNodeWithOptionalInput(
    const OrtApi* api,
    const OrtNode* node,
    const size_t inferred_input_index,
    const TensorValueDescriptor* inferred_input,
    SupportedNodeSpec* spec_out,
    bool* is_supported) {
  if (spec_out == nullptr || is_supported == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires spec_out and is_supported output pointers when validating nodes.");
  }
  *spec_out = SupportedNodeSpec{};
  *is_supported = false;
  if (node == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP cannot inspect a null OrtNode.");
  }

  spec_out->node = node;
  const char* node_name = nullptr;
  OrtStatus* status = api->Node_GetName(node, &node_name);
  if (status != nullptr) return status;
  spec_out->node_name = node_name != nullptr ? node_name : "";

  const char* operator_type = nullptr;
  status = api->Node_GetOperatorType(node, &operator_type);
  if (status != nullptr || operator_type == nullptr) return status;

  const char* domain = nullptr;
  status = api->Node_GetDomain(node, &domain);
  if (status != nullptr) return status;
  if (!IsSupportedOnnxDomain(domain)) {
    return nullptr;
  }

  size_t expected_inputs = 0;
  if (!LookupSingleNodeOp(operator_type, &spec_out->kind, &expected_inputs)) {
    return nullptr;
  }

  std::vector<const OrtValueInfo*> inputs;
  status = GetNodeValueInfos(api, node, true, &inputs);
  if (status != nullptr || inputs.size() != expected_inputs) return status;

  std::vector<const OrtValueInfo*> outputs;
  status = GetNodeValueInfos(api, node, false, &outputs);
  if (status != nullptr || outputs.size() != kSingleOutputCount) return status;

  spec_out->inputs.reserve(inputs.size());
  for (size_t input_index = 0; input_index < inputs.size(); ++input_index) {
    TensorValueDescriptor descriptor;
    bool tensor_supported = false;
    status = DescribeSupportedTensorValue(api, inputs[input_index], &descriptor, &tensor_supported);
    if (status != nullptr) return status;
    if (inferred_input != nullptr && input_index == inferred_input_index) {
      if (!tensor_supported) {
        descriptor.element_type = inferred_input->element_type;
        descriptor.dims = inferred_input->dims;
        descriptor.element_count = inferred_input->element_count;
      } else if (!SameTypeAndShape(descriptor, *inferred_input)) {
        return nullptr;
      }
    } else if (!tensor_supported) {
      return nullptr;
    }
    spec_out->inputs.push_back(std::move(descriptor));
  }

  spec_out->outputs.reserve(outputs.size());
  for (const OrtValueInfo* output_info : outputs) {
    TensorValueDescriptor descriptor;
    bool tensor_supported = false;
    status = DescribeSupportedTensorValue(api, output_info, &descriptor, &tensor_supported);
    if (status != nullptr) return status;
    // Allow ORT to omit concrete output shape info for MatMul/Gemm and infer it below.
    if (!tensor_supported && spec_out->kind != CompiledOpKind::kMatMul && spec_out->kind != CompiledOpKind::kGemm) {
      return nullptr;
    }
    spec_out->outputs.push_back(std::move(descriptor));
  }

  switch (spec_out->kind) {
    case CompiledOpKind::kIdentity:
    case CompiledOpKind::kRelu:
    case CompiledOpKind::kSigmoid:
    case CompiledOpKind::kTanh:
    case CompiledOpKind::kGelu:
    case CompiledOpKind::kSoftmax:
      if (!SameTypeAndShape(spec_out->inputs[0], spec_out->outputs[0])) {
        return nullptr;
      }
      break;
    case CompiledOpKind::kLayerNorm: {
      if (spec_out->inputs[0].dims.empty() || !SameTypeAndShape(spec_out->inputs[0], spec_out->outputs[0]) ||
          spec_out->inputs[1].element_type != kSupportedElementType) {
        return nullptr;
      }
      const int64_t last_dim = spec_out->inputs[0].dims.back();
      if (spec_out->inputs[1].dims.size() != 1 || spec_out->inputs[1].dims[0] != last_dim) {
        return nullptr;
      }
      break;
    }
    case CompiledOpKind::kConcat: {
      const auto& a = spec_out->inputs[0];
      const auto& b = spec_out->inputs[1];
      const auto& y = spec_out->outputs[0];
      if (a.element_type != kSupportedElementType || b.element_type != kSupportedElementType ||
          y.element_type != kSupportedElementType) {
        return nullptr;
      }
      if (a.dims.empty() || a.dims.size() != b.dims.size() || a.dims.size() != y.dims.size()) {
        return nullptr;
      }
      int64_t axis_attr = 0;
      OrtStatus* axis_status = ReadOptionalIntAttribute(api, node, kAxisAttribute, 0, &axis_attr);
      if (axis_status != nullptr) return axis_status;
      const int64_t rank = static_cast<int64_t>(a.dims.size());
      const int64_t axis = axis_attr < 0 ? axis_attr + rank : axis_attr;
      if (axis < 0 || axis >= rank) {
        return nullptr;
      }
      for (size_t i = 0; i < a.dims.size(); ++i) {
        if (static_cast<int64_t>(i) == axis) continue;
        if (a.dims[i] != b.dims[i] || a.dims[i] != y.dims[i]) {
          return nullptr;
        }
      }
      const size_t axis_index = static_cast<size_t>(axis);
      if (y.dims[axis_index] != a.dims[axis_index] + b.dims[axis_index]) {
        return nullptr;
      }
      break;
    }
    case CompiledOpKind::kAdd:
      if (!SameTypeAndShape(spec_out->inputs[0], spec_out->inputs[1]) ||
          !SameTypeAndShape(spec_out->inputs[0], spec_out->outputs[0])) {
        return nullptr;
      }
      break;
    case CompiledOpKind::kMatMul:
    case CompiledOpKind::kGemm: {
      if (spec_out->kind == CompiledOpKind::kGemm) {
        bool gemm_attributes_supported = false;
        status = HasSupportedGemmAttributes(api, node, &gemm_attributes_supported);
        if (status != nullptr || !gemm_attributes_supported) return status;
      }
      TensorValueDescriptor inferred_output;
      if (!InferStaticMatMulOutput(spec_out->inputs[0], spec_out->inputs[1], &inferred_output)) {
        return nullptr;
      }
      if (spec_out->kind == CompiledOpKind::kGemm && !SameTypeAndShape(inferred_output, spec_out->inputs[2])) {
        return nullptr;
      }
      const bool output_has_supported_shape =
          spec_out->outputs[0].element_type == kSupportedElementType && IsRank2Matrix(spec_out->outputs[0]);
      if (output_has_supported_shape &&
          !IsStaticMatMulShape(spec_out->inputs[0], spec_out->inputs[1], spec_out->outputs[0])) {
        return nullptr;
      }
      if (!output_has_supported_shape) {
        spec_out->outputs[0].element_type = inferred_output.element_type;
        spec_out->outputs[0].dims = inferred_output.dims;
        spec_out->outputs[0].element_count = inferred_output.element_count;
      }
      break;
    }
    default:
      return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP does not validate fused groups as a single OrtNode.");
  }

  *is_supported = true;
  return nullptr;
}

OrtStatus* GetSupportedNodeSpec(
    const OrtApi* api,
    const OrtNode* node,
    SupportedNodeSpec* spec_out,
    bool* is_supported) {
  return InspectSupportedNodeWithOptionalInput(api, node, kNoInferredInput, nullptr, spec_out, is_supported);
}

OrtStatus* FindSingleUnclaimedConsumer(
    const OrtApi* api,
    const SupportedNodeSpec& producer_spec,
    const std::unordered_set<const OrtNode*>& already_claimed,
    const size_t max_consumer_input_index_exclusive,
    const OrtNode** consumer_node_out,
    size_t* consumer_input_index_out) {
  *consumer_node_out = nullptr;
  *consumer_input_index_out = 0;
  if (producer_spec.outputs.size() != 1 || producer_spec.outputs[0].value_info == nullptr) {
    return nullptr;
  }

  bool is_graph_output = false;
  OrtStatus* status = api->ValueInfo_IsGraphOutput(producer_spec.outputs[0].value_info, &is_graph_output);
  if (status != nullptr || is_graph_output) return status;

  size_t num_consumers = 0;
  status = api->ValueInfo_GetValueNumConsumers(producer_spec.outputs[0].value_info, &num_consumers);
  if (status != nullptr || num_consumers != 1) return status;

  const OrtNode* consumer_node = nullptr;
  int64_t consumer_input_index = -1;
  status = api->ValueInfo_GetValueConsumers(
      producer_spec.outputs[0].value_info,
      &consumer_node,
      &consumer_input_index,
      1);
  if (status != nullptr) return status;
  if (consumer_node == nullptr || consumer_input_index < 0 ||
      static_cast<size_t>(consumer_input_index) >= max_consumer_input_index_exclusive ||
      already_claimed.find(consumer_node) != already_claimed.end()) {
    return nullptr;
  }

  *consumer_node_out = consumer_node;
  *consumer_input_index_out = static_cast<size_t>(consumer_input_index);
  return nullptr;
}

OrtStatus* FindSupportedReluConsumer(
    const OrtApi* api,
    const SupportedNodeSpec& producer_spec,
    const std::unordered_set<const OrtNode*>& already_claimed,
    SupportedNodeSpec* relu_spec_out,
    bool* found) {
  if (relu_spec_out == nullptr || found == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires relu_spec_out and found output pointers when searching for Relu fusion.");
  }
  *relu_spec_out = SupportedNodeSpec{};
  *found = false;
  if (producer_spec.kind != CompiledOpKind::kAdd && producer_spec.kind != CompiledOpKind::kGemm) {
    return nullptr;
  }

  const OrtNode* consumer_node = nullptr;
  size_t consumer_input_index = 0;
  OrtStatus* status =
      FindSingleUnclaimedConsumer(api, producer_spec, already_claimed, kUnaryInputCount, &consumer_node, &consumer_input_index);
  if (status != nullptr || consumer_node == nullptr) return status;

  SupportedNodeSpec relu_spec;
  bool relu_supported = false;
  status = GetSupportedNodeSpec(api, consumer_node, &relu_spec, &relu_supported);
  if (status != nullptr) return status;
  if (!relu_supported || relu_spec.kind != CompiledOpKind::kRelu) {
    return nullptr;
  }

  *relu_spec_out = std::move(relu_spec);
  *found = true;
  return nullptr;
}

OrtStatus* FindSupportedAddConsumer(
    const OrtApi* api,
    const SupportedNodeSpec& matmul_spec,
    const std::unordered_set<const OrtNode*>& already_claimed,
    SupportedNodeSpec* add_spec_out,
    size_t* add_input_index_out,
    bool* found) {
  if (add_spec_out == nullptr || add_input_index_out == nullptr || found == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires add_spec_out, add_input_index_out, and found pointers for MatMul->Add fusion.");
  }
  *add_spec_out = SupportedNodeSpec{};
  *add_input_index_out = 0;
  *found = false;
  if (matmul_spec.kind != CompiledOpKind::kMatMul) {
    return nullptr;
  }

  const OrtNode* consumer_node = nullptr;
  size_t consumer_input_index = 0;
  OrtStatus* status = FindSingleUnclaimedConsumer(
      api, matmul_spec, already_claimed, kBinaryInputCount, &consumer_node, &consumer_input_index);
  if (status != nullptr || consumer_node == nullptr) return status;

  TensorValueDescriptor inferred_matmul_output;
  if (!InferStaticMatMulOutput(matmul_spec.inputs[0], matmul_spec.inputs[1], &inferred_matmul_output)) {
    return nullptr;
  }

  SupportedNodeSpec add_spec;
  bool add_supported = false;
  status = InspectSupportedNodeWithOptionalInput(
      api, consumer_node, consumer_input_index, &inferred_matmul_output, &add_spec, &add_supported);
  if (status != nullptr) return status;
  if (!add_supported || add_spec.kind != CompiledOpKind::kAdd) {
    return nullptr;
  }

  *add_input_index_out = consumer_input_index;
  *add_spec_out = std::move(add_spec);
  *found = true;
  return nullptr;
}

OrtStatus* FindSupportedGemmConsumer(
    const OrtApi* api,
    const SupportedNodeSpec& relu_spec,
    const std::unordered_set<const OrtNode*>& already_claimed,
    SupportedNodeSpec* gemm_spec_out,
    bool* found) {
  if (gemm_spec_out == nullptr || found == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires gemm_spec_out and found output pointers when searching for Gemm->Relu->Gemm fusion.");
  }
  *gemm_spec_out = SupportedNodeSpec{};
  *found = false;
  if (relu_spec.kind != CompiledOpKind::kRelu) {
    return nullptr;
  }

  const OrtNode* consumer_node = nullptr;
  size_t consumer_input_index = 0;
  OrtStatus* status =
      FindSingleUnclaimedConsumer(api, relu_spec, already_claimed, kUnaryInputCount, &consumer_node, &consumer_input_index);
  if (status != nullptr || consumer_node == nullptr) return status;

  SupportedNodeSpec gemm_spec;
  bool gemm_supported = false;
  status = InspectSupportedNodeWithOptionalInput(
      api, consumer_node, 0, &relu_spec.outputs[0], &gemm_spec, &gemm_supported);
  if (status != nullptr) return status;
  if (!gemm_supported || gemm_spec.kind != CompiledOpKind::kGemm) {
    return nullptr;
  }

  *gemm_spec_out = std::move(gemm_spec);
  *found = true;
  return nullptr;
}

SupportedGraphSpec BuildAddReluGraphSpec(const SupportedNodeSpec& add_spec, const SupportedNodeSpec& relu_spec) {
  return SupportedGraphSpec{
      .kind = CompiledOpKind::kAddRelu,
      .default_node_name = "doe_add_relu",
      .fused_nodes = {add_spec.node, relu_spec.node},
      .logical_input_names = {add_spec.inputs[0].name, add_spec.inputs[1].name},
  };
}

SupportedGraphSpec BuildMatMulAddGraphSpec(
    const SupportedNodeSpec& matmul_spec,
    const SupportedNodeSpec& add_spec,
    const size_t matmul_output_input_index) {
  const size_t addend_input_index = matmul_output_input_index == 0 ? 1 : 0;
  return SupportedGraphSpec{
      .kind = CompiledOpKind::kMatMulAdd,
      .default_node_name = "doe_matmul_add",
      .fused_nodes = {matmul_spec.node, add_spec.node},
      .logical_input_names = {
          matmul_spec.inputs[0].name,
          matmul_spec.inputs[1].name,
          add_spec.inputs[addend_input_index].name},
  };
}

SupportedGraphSpec BuildMatMulAddReluGraphSpec(
    const SupportedNodeSpec& matmul_spec,
    const SupportedNodeSpec& add_spec,
    const size_t matmul_output_input_index,
    const SupportedNodeSpec& relu_spec) {
  SupportedGraphSpec graph_spec = BuildMatMulAddGraphSpec(matmul_spec, add_spec, matmul_output_input_index);
  graph_spec.kind = CompiledOpKind::kMatMulAddRelu;
  graph_spec.default_node_name = "doe_matmul_add_relu";
  graph_spec.fused_nodes = {matmul_spec.node, add_spec.node, relu_spec.node};
  return graph_spec;
}

SupportedGraphSpec BuildGemmReluGemmGraphSpec(
    const SupportedNodeSpec& first_gemm_spec,
    const SupportedNodeSpec& relu_spec,
    const SupportedNodeSpec& second_gemm_spec) {
  return SupportedGraphSpec{
      .kind = CompiledOpKind::kGemmReluGemm,
      .default_node_name = "doe_gemm_relu_gemm",
      .fused_nodes = {first_gemm_spec.node, relu_spec.node, second_gemm_spec.node},
      .logical_input_names = {
          first_gemm_spec.inputs[0].name,
          first_gemm_spec.inputs[1].name,
          first_gemm_spec.inputs[2].name,
          second_gemm_spec.inputs[1].name,
          second_gemm_spec.inputs[2].name},
  };
}

OrtStatus* ResolveGraphInputBindings(
    const OrtApi* api,
    const OrtGraph* graph,
    const SupportedGraphSpec& graph_spec,
    std::array<size_t, kQuinaryInputCount>* indices_out,
    size_t* input_count_out) {
  if (indices_out == nullptr || input_count_out == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires indices_out and input_count_out when resolving graph input bindings.");
  }

  indices_out->fill(0);
  *input_count_out = 0;
  if (graph_spec.logical_input_names.size() > indices_out->size()) {
    return MakeInvalidGraphStatus(api, "Doe ORT plugin EP fused graph exceeded its supported external input count.");
  }

  std::vector<const OrtValueInfo*> graph_inputs;
  OrtStatus* status = GetGraphInputs(api, graph, &graph_inputs);
  if (status != nullptr) return status;
  if (graph_inputs.size() != graph_spec.logical_input_names.size()) {
    std::ostringstream message;
    message << "Doe ORT plugin EP " << CompiledOpKindName(graph_spec.kind) << " slice expects exactly "
            << graph_spec.logical_input_names.size() << " external graph inputs but received " << graph_inputs.size()
            << '.';
    return MakeInvalidGraphStatus(api, message.str());
  }

  for (size_t logical_index = 0; logical_index < graph_spec.logical_input_names.size(); ++logical_index) {
    const std::string& logical_name = graph_spec.logical_input_names[logical_index];
    bool matched = false;
    for (size_t graph_index = 0; graph_index < graph_inputs.size(); ++graph_index) {
      std::string graph_input_name;
      status = GetValueInfoNameString(api, graph_inputs[graph_index], &graph_input_name);
      if (status != nullptr) return status;
      if (graph_input_name == logical_name) {
        (*indices_out)[logical_index] = graph_index;
        matched = true;
        break;
      }
    }
    if (!matched) {
      std::ostringstream message;
      message << "Doe ORT plugin EP " << CompiledOpKindName(graph_spec.kind)
              << " slice could not resolve fused graph input '" << logical_name << "'.";
      return MakeInvalidGraphStatus(api, message.str());
    }
  }

  *input_count_out = graph_spec.logical_input_names.size();
  return nullptr;
}

OrtStatus* AnalyzeSupportedGraph(
    const OrtApi* api,
    const OrtGraph* graph,
    SupportedGraphSpec* spec_out,
    bool* is_supported) {
  if (spec_out == nullptr || is_supported == nullptr) {
    return MakeInvalidArgumentStatus(
        api,
        "Doe ORT plugin EP requires spec_out and is_supported output pointers when validating graphs.");
  }
  *spec_out = SupportedGraphSpec{};
  *is_supported = false;

  std::vector<const OrtNode*> nodes;
  OrtStatus* status = GetGraphNodes(api, graph, &nodes);
  if (status != nullptr || nodes.empty()) return status;

  if (nodes.size() == 1) {
    SupportedNodeSpec node_spec;
    bool node_supported = false;
    status = GetSupportedNodeSpec(api, nodes[0], &node_spec, &node_supported);
    if (status != nullptr || !node_supported) return status;

    spec_out->kind = node_spec.kind;
    spec_out->default_node_name = node_spec.node_name.empty() ? CompiledOpKindName(node_spec.kind) : node_spec.node_name;
    spec_out->fused_nodes = {nodes[0]};
    spec_out->logical_input_names.reserve(node_spec.inputs.size());
    for (const TensorValueDescriptor& input : node_spec.inputs) {
      spec_out->logical_input_names.push_back(input.name);
    }
    *is_supported = true;
    return nullptr;
  }

  if (nodes.size() == 2) {
    SupportedNodeSpec first_spec;
    SupportedNodeSpec second_spec;
    bool first_supported = false;
    bool second_supported = false;
    status = GetSupportedNodeSpec(api, nodes[0], &first_spec, &first_supported);
    if (status != nullptr) return status;
    status = GetSupportedNodeSpec(api, nodes[1], &second_spec, &second_supported);
    if (status != nullptr || !first_supported || !second_supported) return status;

    const auto match_pair = [&](CompiledOpKind a, CompiledOpKind b,
                                const SupportedNodeSpec** out_a, const SupportedNodeSpec** out_b) {
      if (first_spec.kind == a && second_spec.kind == b) {
        *out_a = &first_spec;
        *out_b = &second_spec;
        return true;
      }
      if (first_spec.kind == b && second_spec.kind == a) {
        *out_a = &second_spec;
        *out_b = &first_spec;
        return true;
      }
      return false;
    };

    const SupportedNodeSpec* producer_spec = nullptr;
    const SupportedNodeSpec* consumer_spec = nullptr;
    if (match_pair(CompiledOpKind::kAdd, CompiledOpKind::kRelu, &producer_spec, &consumer_spec)) {
      SupportedNodeSpec fused_relu_spec;
      bool fused = false;
      status = FindSupportedReluConsumer(api, *producer_spec, {}, &fused_relu_spec, &fused);
      if (status != nullptr) return status;
      if (fused && fused_relu_spec.node == consumer_spec->node) {
        *spec_out = BuildAddReluGraphSpec(*producer_spec, fused_relu_spec);
        *is_supported = true;
      }
      return nullptr;
    }

    if (match_pair(CompiledOpKind::kMatMul, CompiledOpKind::kAdd, &producer_spec, &consumer_spec)) {
      SupportedNodeSpec fused_add_spec;
      size_t matmul_output_input_index = 0;
      bool fused = false;
      status = FindSupportedAddConsumer(api, *producer_spec, {}, &fused_add_spec, &matmul_output_input_index, &fused);
      if (status != nullptr) return status;
      if (fused && fused_add_spec.node == consumer_spec->node) {
        *spec_out = BuildMatMulAddGraphSpec(*producer_spec, fused_add_spec, matmul_output_input_index);
        *is_supported = true;
      }
      return nullptr;
    }
    return nullptr;
  }

  if (nodes.size() == 3) {
    std::vector<SupportedNodeSpec> supported_specs(nodes.size());
    size_t matmul_index = nodes.size();
    size_t first_gemm_index = nodes.size();
    size_t second_gemm_index = nodes.size();
    size_t add_index = nodes.size();
    size_t relu_index = nodes.size();
    for (size_t index = 0; index < nodes.size(); ++index) {
      bool node_supported = false;
      status = GetSupportedNodeSpec(api, nodes[index], &supported_specs[index], &node_supported);
      if (status != nullptr || !node_supported) return status;
      switch (supported_specs[index].kind) {
        case CompiledOpKind::kMatMul:
          if (matmul_index != nodes.size()) return nullptr;
          matmul_index = index;
          break;
        case CompiledOpKind::kGemm:
          if (first_gemm_index == nodes.size()) {
            first_gemm_index = index;
          } else if (second_gemm_index == nodes.size()) {
            second_gemm_index = index;
          } else {
            return nullptr;
          }
          break;
        case CompiledOpKind::kAdd:
          if (add_index != nodes.size()) return nullptr;
          add_index = index;
          break;
        case CompiledOpKind::kRelu:
          if (relu_index != nodes.size()) return nullptr;
          relu_index = index;
          break;
        default:
          return nullptr;
      }
    }

    if (first_gemm_index != nodes.size() && second_gemm_index != nodes.size() && relu_index != nodes.size() &&
        matmul_index == nodes.size() && add_index == nodes.size()) {
      const SupportedNodeSpec& relu_spec = supported_specs[relu_index];
      const SupportedNodeSpec* first_gemm_spec = nullptr;
      const SupportedNodeSpec* second_gemm_spec = nullptr;
      for (const size_t gemm_index : {first_gemm_index, second_gemm_index}) {
        const SupportedNodeSpec& candidate = supported_specs[gemm_index];
        SupportedNodeSpec fused_relu_spec;
        bool fused_relu = false;
        status = FindSupportedReluConsumer(api, candidate, {}, &fused_relu_spec, &fused_relu);
        if (status != nullptr) return status;
        if (fused_relu && fused_relu_spec.node == relu_spec.node) {
          first_gemm_spec = &candidate;
          second_gemm_spec = &supported_specs[gemm_index == first_gemm_index ? second_gemm_index : first_gemm_index];
          break;
        }
      }
      if (first_gemm_spec == nullptr || second_gemm_spec == nullptr) {
        return nullptr;
      }

      SupportedNodeSpec fused_second_gemm_spec;
      bool fused_second_gemm = false;
      status = FindSupportedGemmConsumer(api, relu_spec, {}, &fused_second_gemm_spec, &fused_second_gemm);
      if (status != nullptr) return status;
      if (!fused_second_gemm || fused_second_gemm_spec.node != second_gemm_spec->node) {
        return nullptr;
      }

      *spec_out = BuildGemmReluGemmGraphSpec(*first_gemm_spec, relu_spec, fused_second_gemm_spec);
      *is_supported = true;
      return nullptr;
    }

    if (matmul_index == nodes.size() || add_index == nodes.size() || relu_index == nodes.size() ||
        first_gemm_index != nodes.size() || second_gemm_index != nodes.size()) {
      return nullptr;
    }

    const SupportedNodeSpec& matmul_spec = supported_specs[matmul_index];
    const SupportedNodeSpec& add_spec = supported_specs[add_index];
    const SupportedNodeSpec& relu_spec = supported_specs[relu_index];

    SupportedNodeSpec fused_add_spec;
    size_t matmul_output_input_index = 0;
    bool fused_add = false;
    status = FindSupportedAddConsumer(api, matmul_spec, {}, &fused_add_spec, &matmul_output_input_index, &fused_add);
    if (status != nullptr) return status;
    if (!fused_add || fused_add_spec.node != add_spec.node) {
      return nullptr;
    }

    SupportedNodeSpec fused_relu_spec;
    bool fused_relu = false;
    status = FindSupportedReluConsumer(api, fused_add_spec, {}, &fused_relu_spec, &fused_relu);
    if (status != nullptr) return status;
    if (!fused_relu || fused_relu_spec.node != relu_spec.node) {
      return nullptr;
    }

    *spec_out = BuildMatMulAddReluGraphSpec(matmul_spec, fused_add_spec, matmul_output_input_index, fused_relu_spec);
    *is_supported = true;
  }

  return nullptr;
}

OrtStatus* GetRuntimeTensorDescriptor(
    const OrtApi* api,
    const OrtValue* value,
    RuntimeTensorDescriptor* descriptor_out) {
  if (descriptor_out == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP requires a descriptor_out output pointer.");
  }
  *descriptor_out = RuntimeTensorDescriptor{};
  if (value == nullptr) {
    return MakeInvalidArgumentStatus(api, "Doe ORT plugin EP received a null tensor value.");
  }

  OrtTensorTypeAndShapeInfo* tensor_info = nullptr;
  OrtStatus* status = api->GetTensorTypeAndShape(value, &tensor_info);
  if (status != nullptr) return status;

  status = api->GetTensorElementType(tensor_info, &descriptor_out->element_type);
  if (status != nullptr) {
    api->ReleaseTensorTypeAndShapeInfo(tensor_info);
    return status;
  }
  if (descriptor_out->element_type != kSupportedElementType) {
    api->ReleaseTensorTypeAndShapeInfo(tensor_info);
    return MakeNotImplementedStatus(api, "Doe ORT plugin EP currently supports float32 tensors only.");
  }

  size_t rank = 0;
  status = api->GetDimensionsCount(tensor_info, &rank);
  if (status != nullptr) {
    api->ReleaseTensorTypeAndShapeInfo(tensor_info);
    return status;
  }

  descriptor_out->dims.assign(rank, 0);
  if (rank != 0) {
    status = api->GetDimensions(tensor_info, descriptor_out->dims.data(), descriptor_out->dims.size());
    if (status != nullptr) {
      api->ReleaseTensorTypeAndShapeInfo(tensor_info);
      return status;
    }
  }

  for (const int64_t dim : descriptor_out->dims) {
    if (dim < 0) {
      api->ReleaseTensorTypeAndShapeInfo(tensor_info);
      return MakeInvalidGraphStatus(api, "Doe ORT plugin EP requires concrete runtime tensor shapes.");
    }
  }

  status = api->GetTensorShapeElementCount(tensor_info, &descriptor_out->element_count);
  api->ReleaseTensorTypeAndShapeInfo(tensor_info);
  return status;
}

OrtStatus* ValidateRuntimeArity(
    const CompiledNodeComputeInfo& info,
    OrtKernelContext* kernel_context,
    const size_t expected_inputs,
    const size_t expected_outputs) {
  size_t input_count = 0;
  OrtStatus* status = info.bindings.api->KernelContext_GetInputCount(kernel_context, &input_count);
  if (status != nullptr) return status;
  if (input_count != expected_inputs) {
    std::ostringstream message;
    message << "Doe ORT plugin EP " << CompiledOpKindName(info.op_kind) << " slice expects exactly " << expected_inputs
            << " inputs but received " << input_count << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  size_t output_count = 0;
  status = info.bindings.api->KernelContext_GetOutputCount(kernel_context, &output_count);
  if (status != nullptr) return status;
  if (output_count != expected_outputs) {
    std::ostringstream message;
    message << "Doe ORT plugin EP " << CompiledOpKindName(info.op_kind) << " slice expects exactly "
            << expected_outputs << " outputs but received " << output_count << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }
  return nullptr;
}

OrtStatus* GetKernelInputData(
    const CompiledNodeComputeInfo& info,
    OrtKernelContext* kernel_context,
    const size_t logical_input_index,
    RuntimeTensorDescriptor* descriptor_out,
    const float** data_out) {
  if (logical_input_index >= info.input_binding_count || logical_input_index >= info.input_binding_indices.size()) {
    std::ostringstream message;
    message << "Doe ORT plugin EP " << CompiledOpKindName(info.op_kind)
            << " slice does not have a bound graph input at logical index " << logical_input_index << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  const size_t input_index = info.input_binding_indices[logical_input_index];
  if (input_index >= info.input_binding_count) {
    std::ostringstream message;
    message << "Doe ORT plugin EP " << CompiledOpKindName(info.op_kind)
            << " slice resolved logical input " << logical_input_index << " to invalid kernel input index "
            << input_index << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  if (descriptor_out == nullptr || data_out == nullptr) {
    return MakeInvalidArgumentStatus(
        info.bindings.api,
        "Doe ORT plugin EP requires descriptor_out and data_out output pointers when reading kernel inputs.");
  }

  const OrtValue* input = nullptr;
  OrtStatus* status = info.bindings.api->KernelContext_GetInput(kernel_context, input_index, &input);
  if (status != nullptr) return status;
  if (input == nullptr) {
    std::ostringstream message;
    message << "Doe ORT plugin EP received a null input tensor at index " << input_index << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  status = GetRuntimeTensorDescriptor(info.bindings.api, input, descriptor_out);
  if (status != nullptr) return status;

  const void* data = nullptr;
  status = info.bindings.api->GetTensorData(input, &data);
  if (status != nullptr) return status;
  if (data == nullptr) {
    std::ostringstream message;
    message << "Doe ORT plugin EP received null tensor data for input " << input_index << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  *data_out = static_cast<const float*>(data);
  return nullptr;
}

OrtStatus* CreateKernelOutput(
    const CompiledNodeComputeInfo& info,
    OrtKernelContext* kernel_context,
    const RuntimeTensorDescriptor& shape_source,
    float** output_data_out) {
  if (output_data_out == nullptr) {
    return MakeInvalidArgumentStatus(info.bindings.api, "Doe ORT plugin EP requires an output_data_out pointer.");
  }

  OrtValue* output = nullptr;
  OrtStatus* status = info.bindings.api->KernelContext_GetOutput(
      kernel_context,
      0,
      shape_source.dims.empty() ? nullptr : shape_source.dims.data(),
      shape_source.dims.size(),
      &output);
  if (status != nullptr) return status;
  if (output == nullptr) {
    return MakeStatus(info.bindings.api, ORT_FAIL, "Doe ORT plugin EP failed to allocate an output tensor.");
  }

  void* output_data = nullptr;
  status = info.bindings.api->GetTensorMutableData(output, &output_data);
  if (status != nullptr) return status;
  if (output_data == nullptr) {
    return MakeStatus(info.bindings.api, ORT_FAIL, "Doe ORT plugin EP received null output storage from ORT.");
  }

  *output_data_out = static_cast<float*>(output_data);
  return nullptr;
}

OrtStatus* ValidateSameRuntimeShape(
    const CompiledNodeComputeInfo& info,
    const char* lhs_label,
    const RuntimeTensorDescriptor& lhs,
    const char* rhs_label,
    const RuntimeTensorDescriptor& rhs) {
  if (SameTypeAndShape(lhs, rhs)) {
    return nullptr;
  }

  std::ostringstream message;
  message << "Doe ORT plugin EP " << CompiledOpKindName(info.op_kind) << " slice requires exact-shape float32 tensors; "
          << lhs_label << " has shape " << ShapeString(lhs.dims) << " while " << rhs_label << " has shape "
          << ShapeString(rhs.dims) << '.';
  return MakeInvalidGraphStatus(info.bindings.api, message.str());
}

OrtStatus* ValidateRuntimeMatMulShape(
    const CompiledNodeComputeInfo& info,
    const RuntimeTensorDescriptor& lhs,
    const RuntimeTensorDescriptor& rhs,
    RuntimeTensorDescriptor* output_descriptor_out) {
  if (output_descriptor_out == nullptr) {
    return MakeInvalidArgumentStatus(
        info.bindings.api,
        "Doe ORT plugin EP requires an output_descriptor_out pointer when validating MatMul.");
  }

  if (!IsRank2Matrix(lhs) || !IsRank2Matrix(rhs)) {
    std::ostringstream message;
    message << "Doe ORT plugin EP MatMul slice requires rank-2 float32 inputs; input0 has shape "
            << ShapeString(lhs.dims) << " and input1 has shape " << ShapeString(rhs.dims) << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }
  if (lhs.element_type != rhs.element_type) {
    return MakeInvalidGraphStatus(info.bindings.api, "Doe ORT plugin EP MatMul slice requires matching float32 inputs.");
  }
  if (lhs.dims[1] != rhs.dims[0]) {
    std::ostringstream message;
    message << "Doe ORT plugin EP MatMul slice requires exact matrix inner dimensions; input0 has shape "
            << ShapeString(lhs.dims) << " and input1 has shape " << ShapeString(rhs.dims) << '.';
    return MakeInvalidGraphStatus(info.bindings.api, message.str());
  }

  *output_descriptor_out = RuntimeTensorDescriptor{
      .element_type = lhs.element_type,
      .dims = {lhs.dims[0], rhs.dims[1]},
      .element_count = static_cast<size_t>(lhs.dims[0] * rhs.dims[1]),
  };
  return nullptr;
}

float ReluValue(const float value) {
  return value < 0.0f ? 0.0f : value;
}

float SigmoidValue(const float value) {
  return 1.0f / (1.0f + std::exp(-value));
}

float TanhValue(const float value) {
  return std::tanh(value);
}

// GeLU using the exact erf formulation: 0.5 * x * (1 + erf(x / sqrt(2))).
// This matches the ONNX reference implementation. The "approximate=tanh"
// attribute variant is not supported in this slice.
float GeluValue(const float value) {
  constexpr float kInvSqrt2 = 0.70710678118654752440f;
  return 0.5f * value * (1.0f + std::erf(value * kInvSqrt2));
}

void ComputeDenseMatMulEpilogue(
    const float* lhs_data,
    const float* rhs_data,
    const float* bias_data,
    const bool apply_relu,
    const size_t m_dim,
    const size_t k_dim,
    const size_t n_dim,
    float* output_data) {
  for (size_t row = 0; row < m_dim; ++row) {
    for (size_t col = 0; col < n_dim; ++col) {
      float sum = 0.0f;
      for (size_t k_index = 0; k_index < k_dim; ++k_index) {
        sum += lhs_data[row * k_dim + k_index] * rhs_data[k_index * n_dim + col];
      }
      const size_t out_index = row * n_dim + col;
      float result = bias_data != nullptr ? sum + bias_data[out_index] : sum;
      output_data[out_index] = apply_relu ? ReluValue(result) : result;
    }
  }
}

OrtStatus* ExecuteIdentity(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor input_descriptor;
  const float* input_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &input_descriptor, &input_data);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, input_descriptor, &output_data);
  if (status != nullptr) return status;

  std::memmove(output_data, input_data, input_descriptor.element_count * sizeof(float));
  return nullptr;
}

template <float (*Fn)(float)>
OrtStatus* ExecuteUnaryElementwise(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor input_descriptor;
  const float* input_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &input_descriptor, &input_data);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, input_descriptor, &output_data);
  if (status != nullptr) return status;

  for (size_t index = 0; index < input_descriptor.element_count; ++index) {
    output_data[index] = Fn(input_data[index]);
  }
  return nullptr;
}

OrtStatus* ExecuteAddEpilogue(
    const CompiledNodeComputeInfo& info,
    OrtKernelContext* kernel_context,
    const bool apply_relu) {
  RuntimeTensorDescriptor lhs_descriptor;
  RuntimeTensorDescriptor rhs_descriptor;
  const float* lhs_data = nullptr;
  const float* rhs_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &lhs_descriptor, &lhs_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &rhs_descriptor, &rhs_data);
  if (status != nullptr) return status;
  status = ValidateSameRuntimeShape(info, "input0", lhs_descriptor, "input1", rhs_descriptor);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, lhs_descriptor, &output_data);
  if (status != nullptr) return status;

  for (size_t index = 0; index < lhs_descriptor.element_count; ++index) {
    const float sum = lhs_data[index] + rhs_data[index];
    output_data[index] = apply_relu ? ReluValue(sum) : sum;
  }
  return nullptr;
}

// Softmax along the last axis, following ONNX opset 13+ semantics.
// Uses the numerically stable subtract-max trick: exp(x - max) / sum(exp(x - max)).
OrtStatus* ExecuteSoftmax(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor input_descriptor;
  const float* input_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &input_descriptor, &input_data);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, input_descriptor, &output_data);
  if (status != nullptr) return status;

  if (input_descriptor.dims.empty()) {
    return MakeInvalidGraphStatus(info.bindings.api, "Doe ORT plugin EP Softmax requires a tensor with rank >= 1.");
  }
  const size_t reduce_dim = static_cast<size_t>(input_descriptor.dims.back());
  if (reduce_dim == 0) {
    return nullptr;
  }
  const size_t outer = input_descriptor.element_count / reduce_dim;
  for (size_t row = 0; row < outer; ++row) {
    const size_t base = row * reduce_dim;
    float row_max = input_data[base];
    for (size_t i = 1; i < reduce_dim; ++i) {
      if (input_data[base + i] > row_max) row_max = input_data[base + i];
    }
    float row_sum = 0.0f;
    for (size_t i = 0; i < reduce_dim; ++i) {
      const float shifted = std::exp(input_data[base + i] - row_max);
      output_data[base + i] = shifted;
      row_sum += shifted;
    }
    const float inv = row_sum != 0.0f ? 1.0f / row_sum : 0.0f;
    for (size_t i = 0; i < reduce_dim; ++i) {
      output_data[base + i] *= inv;
    }
  }
  return nullptr;
}

// LayerNormalization along the last axis with scale only (no bias), ONNX opset 17.
// Matches the reference: y = (x - mean) / sqrt(var + epsilon) * scale.
OrtStatus* ExecuteLayerNorm(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor input_descriptor;
  RuntimeTensorDescriptor scale_descriptor;
  const float* input_data = nullptr;
  const float* scale_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &input_descriptor, &input_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &scale_descriptor, &scale_data);
  if (status != nullptr) return status;
  if (input_descriptor.dims.empty()) {
    return MakeInvalidGraphStatus(
        info.bindings.api,
        "Doe ORT plugin EP LayerNormalization requires a tensor with rank >= 1.");
  }
  const size_t last = static_cast<size_t>(input_descriptor.dims.back());
  if (scale_descriptor.element_count != last) {
    return MakeInvalidGraphStatus(
        info.bindings.api,
        "Doe ORT plugin EP LayerNormalization scale length must match the last input dimension.");
  }

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, input_descriptor, &output_data);
  if (status != nullptr) return status;
  if (last == 0) return nullptr;

  const size_t outer = input_descriptor.element_count / last;
  const float inv_last = 1.0f / static_cast<float>(last);
  for (size_t row = 0; row < outer; ++row) {
    const size_t base = row * last;
    float mean = 0.0f;
    for (size_t i = 0; i < last; ++i) mean += input_data[base + i];
    mean *= inv_last;
    float var = 0.0f;
    for (size_t i = 0; i < last; ++i) {
      const float diff = input_data[base + i] - mean;
      var += diff * diff;
    }
    var *= inv_last;
    const float inv_std = 1.0f / std::sqrt(var + kDefaultLayerNormEpsilon);
    for (size_t i = 0; i < last; ++i) {
      output_data[base + i] = (input_data[base + i] - mean) * inv_std * scale_data[i];
    }
  }
  return nullptr;
}

// Concat along a non-negative, in-range axis. Inputs must match in every
// non-axis dim; the output carries input0 then input1 along the concat axis.
OrtStatus* ExecuteConcat(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor a_descriptor;
  RuntimeTensorDescriptor b_descriptor;
  const float* a_data = nullptr;
  const float* b_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &a_descriptor, &a_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &b_descriptor, &b_data);
  if (status != nullptr) return status;
  if (a_descriptor.dims.empty() || a_descriptor.dims.size() != b_descriptor.dims.size()) {
    return MakeInvalidGraphStatus(info.bindings.api, "Doe ORT plugin EP Concat inputs must share rank.");
  }

  RuntimeTensorDescriptor output_descriptor = a_descriptor;
  // The node reference is gone at compute time, so recover axis from the two
  // input shapes: for a binary concat, the one dim that differs is the concat
  // axis. If the shapes are identical the axis default (0) applies.
  size_t axis = 0;
  for (size_t i = 0; i < a_descriptor.dims.size(); ++i) {
    if (a_descriptor.dims[i] != b_descriptor.dims[i]) {
      axis = i;
      break;
    }
  }
  output_descriptor.dims[axis] = a_descriptor.dims[axis] + b_descriptor.dims[axis];
  output_descriptor.element_count = a_descriptor.element_count + b_descriptor.element_count;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, output_descriptor, &output_data);
  if (status != nullptr) return status;

  size_t outer = 1;
  for (size_t i = 0; i < axis; ++i) outer *= static_cast<size_t>(a_descriptor.dims[i]);
  size_t inner = 1;
  for (size_t i = axis + 1; i < a_descriptor.dims.size(); ++i) inner *= static_cast<size_t>(a_descriptor.dims[i]);
  const size_t a_axis = static_cast<size_t>(a_descriptor.dims[axis]);
  const size_t b_axis = static_cast<size_t>(b_descriptor.dims[axis]);
  const size_t out_axis = a_axis + b_axis;
  for (size_t o = 0; o < outer; ++o) {
    const size_t out_base = o * out_axis * inner;
    std::memmove(output_data + out_base, a_data + o * a_axis * inner, a_axis * inner * sizeof(float));
    std::memmove(output_data + out_base + a_axis * inner, b_data + o * b_axis * inner, b_axis * inner * sizeof(float));
  }
  return nullptr;
}

OrtStatus* ExecuteMatMul(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor lhs_descriptor;
  RuntimeTensorDescriptor rhs_descriptor;
  RuntimeTensorDescriptor output_descriptor;
  const float* lhs_data = nullptr;
  const float* rhs_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &lhs_descriptor, &lhs_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &rhs_descriptor, &rhs_data);
  if (status != nullptr) return status;
  status = ValidateRuntimeMatMulShape(info, lhs_descriptor, rhs_descriptor, &output_descriptor);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, output_descriptor, &output_data);
  if (status != nullptr) return status;

  ComputeDenseMatMulEpilogue(
      lhs_data,
      rhs_data,
      nullptr,
      false,
      static_cast<size_t>(lhs_descriptor.dims[0]),
      static_cast<size_t>(lhs_descriptor.dims[1]),
      static_cast<size_t>(rhs_descriptor.dims[1]),
      output_data);
  return nullptr;
}

OrtStatus* ExecuteLinearWithBias(
    const CompiledNodeComputeInfo& info,
    OrtKernelContext* kernel_context,
    const char* bias_label,
    const bool apply_relu) {
  RuntimeTensorDescriptor lhs_descriptor;
  RuntimeTensorDescriptor rhs_descriptor;
  RuntimeTensorDescriptor addend_descriptor;
  RuntimeTensorDescriptor output_descriptor;
  const float* lhs_data = nullptr;
  const float* rhs_data = nullptr;
  const float* addend_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &lhs_descriptor, &lhs_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &rhs_descriptor, &rhs_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 2, &addend_descriptor, &addend_data);
  if (status != nullptr) return status;
  status = ValidateRuntimeMatMulShape(info, lhs_descriptor, rhs_descriptor, &output_descriptor);
  if (status != nullptr) return status;
  status = ValidateSameRuntimeShape(info, "matmul_output", output_descriptor, bias_label, addend_descriptor);
  if (status != nullptr) return status;

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, output_descriptor, &output_data);
  if (status != nullptr) return status;

  ComputeDenseMatMulEpilogue(
      lhs_data,
      rhs_data,
      addend_data,
      apply_relu,
      static_cast<size_t>(lhs_descriptor.dims[0]),
      static_cast<size_t>(lhs_descriptor.dims[1]),
      static_cast<size_t>(rhs_descriptor.dims[1]),
      output_data);
  return nullptr;
}

OrtStatus* ExecuteGemmReluGemm(const CompiledNodeComputeInfo& info, OrtKernelContext* kernel_context) {
  RuntimeTensorDescriptor input_descriptor;
  RuntimeTensorDescriptor hidden_weight_descriptor;
  RuntimeTensorDescriptor hidden_bias_descriptor;
  RuntimeTensorDescriptor output_weight_descriptor;
  RuntimeTensorDescriptor output_bias_descriptor;
  RuntimeTensorDescriptor hidden_descriptor;
  RuntimeTensorDescriptor output_descriptor;
  const float* input_data = nullptr;
  const float* hidden_weight_data = nullptr;
  const float* hidden_bias_data = nullptr;
  const float* output_weight_data = nullptr;
  const float* output_bias_data = nullptr;
  OrtStatus* status = GetKernelInputData(info, kernel_context, 0, &input_descriptor, &input_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 1, &hidden_weight_descriptor, &hidden_weight_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 2, &hidden_bias_descriptor, &hidden_bias_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 3, &output_weight_descriptor, &output_weight_data);
  if (status != nullptr) return status;
  status = GetKernelInputData(info, kernel_context, 4, &output_bias_descriptor, &output_bias_data);
  if (status != nullptr) return status;

  status = ValidateRuntimeMatMulShape(info, input_descriptor, hidden_weight_descriptor, &hidden_descriptor);
  if (status != nullptr) return status;
  status = ValidateSameRuntimeShape(info, "hidden_gemm_output", hidden_descriptor, "hidden_bias", hidden_bias_descriptor);
  if (status != nullptr) return status;
  status = ValidateRuntimeMatMulShape(info, hidden_descriptor, output_weight_descriptor, &output_descriptor);
  if (status != nullptr) return status;
  status = ValidateSameRuntimeShape(info, "output_gemm_output", output_descriptor, "output_bias", output_bias_descriptor);
  if (status != nullptr) return status;

  std::vector<float> hidden_activations(hidden_descriptor.element_count, 0.0f);
  const size_t batch_dim = static_cast<size_t>(input_descriptor.dims[0]);
  const size_t input_dim = static_cast<size_t>(input_descriptor.dims[1]);
  const size_t hidden_dim = static_cast<size_t>(hidden_weight_descriptor.dims[1]);
  ComputeDenseMatMulEpilogue(
      input_data, hidden_weight_data, hidden_bias_data, true, batch_dim, input_dim, hidden_dim, hidden_activations.data());

  float* output_data = nullptr;
  status = CreateKernelOutput(info, kernel_context, output_descriptor, &output_data);
  if (status != nullptr) return status;

  ComputeDenseMatMulEpilogue(
      hidden_activations.data(),
      output_weight_data,
      output_bias_data,
      false,
      batch_dim,
      hidden_dim,
      static_cast<size_t>(output_weight_descriptor.dims[1]),
      output_data);
  return nullptr;
}

OrtStatus* ORT_API_CALL CreateStateImpl(
    OrtNodeComputeInfo* this_ptr,
    OrtNodeComputeContext* compute_context,
    void** compute_state) {
  auto& info = ComputeInfoSelf(this_ptr);
  if (compute_state == nullptr) {
    return MakeInvalidArgumentStatus(info.bindings.api, "Doe ORT plugin EP requires a compute_state output pointer.");
  }

  g_create_state_calls.fetch_add(1, std::memory_order_relaxed);
  const char* context_node_name =
      info.bindings.ep_api != nullptr && compute_context != nullptr
          ? info.bindings.ep_api->NodeComputeContext_NodeName(compute_context)
          : nullptr;

  auto state = std::make_unique<CompiledComputeState>();
  state->node_name = context_node_name != nullptr ? context_node_name : info.node_name;
  state->op_kind = info.op_kind;
  *compute_state = state.release();
  return nullptr;
}

OrtStatus* ORT_API_CALL ComputeImpl(
    OrtNodeComputeInfo* this_ptr,
    void* compute_state,
    OrtKernelContext* kernel_context) {
  (void)compute_state;
  auto& info = ComputeInfoSelf(this_ptr);
  if (kernel_context == nullptr) {
    return MakeInvalidArgumentStatus(info.bindings.api, "Doe ORT plugin EP requires a kernel_context input.");
  }

  g_compute_calls.fetch_add(1, std::memory_order_relaxed);
  IncrementComputeCounters(info.op_kind);

  OrtStatus* status =
      ValidateRuntimeArity(info, kernel_context, ExpectedInputCountForKind(info.op_kind), kSingleOutputCount);
  if (status != nullptr) return status;

  switch (info.op_kind) {
    case CompiledOpKind::kIdentity:
      return ExecuteIdentity(info, kernel_context);
    case CompiledOpKind::kAdd:
      return ExecuteAddEpilogue(info, kernel_context, false);
    case CompiledOpKind::kRelu:
      return ExecuteUnaryElementwise<&ReluValue>(info, kernel_context);
    case CompiledOpKind::kSigmoid:
      return ExecuteUnaryElementwise<&SigmoidValue>(info, kernel_context);
    case CompiledOpKind::kTanh:
      return ExecuteUnaryElementwise<&TanhValue>(info, kernel_context);
    case CompiledOpKind::kGelu:
      return ExecuteUnaryElementwise<&GeluValue>(info, kernel_context);
    case CompiledOpKind::kMatMul:
      return ExecuteMatMul(info, kernel_context);
    case CompiledOpKind::kGemm:
      return ExecuteLinearWithBias(info, kernel_context, "gemm_bias", false);
    case CompiledOpKind::kGemmReluGemm:
      return ExecuteGemmReluGemm(info, kernel_context);
    case CompiledOpKind::kMatMulAdd:
      return ExecuteLinearWithBias(info, kernel_context, "add_input", false);
    case CompiledOpKind::kMatMulAddRelu:
      return ExecuteLinearWithBias(info, kernel_context, "add_input", true);
    case CompiledOpKind::kAddRelu:
      return ExecuteAddEpilogue(info, kernel_context, true);
    case CompiledOpKind::kSoftmax:
      return ExecuteSoftmax(info, kernel_context);
    case CompiledOpKind::kLayerNorm:
      return ExecuteLayerNorm(info, kernel_context);
    case CompiledOpKind::kConcat:
      return ExecuteConcat(info, kernel_context);
  }
  return MakeStatus(info.bindings.api, ORT_FAIL, "Doe ORT plugin EP reached an unknown compiled op kind.");
}

void ORT_API_CALL ReleaseStateImpl(OrtNodeComputeInfo* this_ptr, void* compute_state) {
  (void)this_ptr;
  g_release_state_calls.fetch_add(1, std::memory_order_relaxed);
  delete static_cast<CompiledComputeState*>(compute_state);
}

}  // namespace

DoeOrtEp::DoeOrtEp(std::string registration_name, ApiBindings bindings)
    : OrtEp{},
      registration_name_(registration_name.empty() ? "DoeExecutionProvider" : std::move(registration_name)),
      bindings_(bindings) {
  ort_version_supported = kOrtPluginEpRuntimeApiVersion;
  GetName = &GetNameImpl;
  GetCapability = &GetCapabilityImpl;
  Compile = &CompileImpl;
  ReleaseNodeComputeInfos = &ReleaseNodeComputeInfosImpl;
  GetPreferredDataLayout = &GetPreferredDataLayoutImpl;
  OnRunStart = &OnRunStartImpl;
  OnRunEnd = &OnRunEndImpl;
  GetCompiledModelCompatibilityInfo = &GetCompiledModelCompatibilityInfoImpl;
}

DoeOrtEp& DoeOrtEp::Self(OrtEp* this_ptr) {
  return *static_cast<DoeOrtEp*>(this_ptr);
}

const DoeOrtEp& DoeOrtEp::Self(const OrtEp* this_ptr) {
  return *static_cast<const DoeOrtEp*>(this_ptr);
}

const char* ORT_API_CALL DoeOrtEp::GetNameImpl(const OrtEp* this_ptr) NO_EXCEPTION {
  return Self(this_ptr).registration_name_.c_str();
}

void ResetDebugCounters() NO_EXCEPTION {
  g_get_capability_calls.store(0, std::memory_order_relaxed);
  g_claimed_nodes.store(0, std::memory_order_relaxed);
  g_compile_calls.store(0, std::memory_order_relaxed);
  g_create_state_calls.store(0, std::memory_order_relaxed);
  g_compute_calls.store(0, std::memory_order_relaxed);
  g_release_state_calls.store(0, std::memory_order_relaxed);
  for (size_t i = 0; i < kCompiledOpKindCount; ++i) {
    g_claimed_op_nodes[i].store(0, std::memory_order_relaxed);
    g_compiled_op_groups[i].store(0, std::memory_order_relaxed);
    g_compute_op_calls[i].store(0, std::memory_order_relaxed);
  }
}

DoeOrtEpDebugCounters SnapshotDebugCounters() NO_EXCEPTION {
  const auto claimed = [](CompiledOpKind k) {
    return g_claimed_op_nodes[OpIndex(k)].load(std::memory_order_relaxed);
  };
  const auto compiled = [](CompiledOpKind k) {
    return g_compiled_op_groups[OpIndex(k)].load(std::memory_order_relaxed);
  };
  const auto computed = [](CompiledOpKind k) {
    return g_compute_op_calls[OpIndex(k)].load(std::memory_order_relaxed);
  };
  return DoeOrtEpDebugCounters{
      .get_capability_calls = g_get_capability_calls.load(std::memory_order_relaxed),
      .claimed_nodes = g_claimed_nodes.load(std::memory_order_relaxed),
      .claimed_identity_nodes = claimed(CompiledOpKind::kIdentity),
      .claimed_add_nodes = claimed(CompiledOpKind::kAdd),
      .claimed_relu_nodes = claimed(CompiledOpKind::kRelu),
      .claimed_sigmoid_nodes = claimed(CompiledOpKind::kSigmoid),
      .claimed_tanh_nodes = claimed(CompiledOpKind::kTanh),
      .claimed_gelu_nodes = claimed(CompiledOpKind::kGelu),
      .claimed_matmul_nodes = claimed(CompiledOpKind::kMatMul),
      .claimed_gemm_nodes = claimed(CompiledOpKind::kGemm),
      .claimed_softmax_nodes = claimed(CompiledOpKind::kSoftmax),
      .claimed_layernorm_nodes = claimed(CompiledOpKind::kLayerNorm),
      .claimed_concat_nodes = claimed(CompiledOpKind::kConcat),
      .compile_calls = g_compile_calls.load(std::memory_order_relaxed),
      .compiled_identity_groups = compiled(CompiledOpKind::kIdentity),
      .compiled_add_groups = compiled(CompiledOpKind::kAdd),
      .compiled_relu_groups = compiled(CompiledOpKind::kRelu),
      .compiled_sigmoid_groups = compiled(CompiledOpKind::kSigmoid),
      .compiled_tanh_groups = compiled(CompiledOpKind::kTanh),
      .compiled_gelu_groups = compiled(CompiledOpKind::kGelu),
      .compiled_matmul_groups = compiled(CompiledOpKind::kMatMul),
      .compiled_gemm_groups = compiled(CompiledOpKind::kGemm),
      .compiled_gemm_relu_gemm_groups = compiled(CompiledOpKind::kGemmReluGemm),
      .compiled_matmul_add_groups = compiled(CompiledOpKind::kMatMulAdd),
      .compiled_matmul_add_relu_groups = compiled(CompiledOpKind::kMatMulAddRelu),
      .compiled_add_relu_groups = compiled(CompiledOpKind::kAddRelu),
      .compiled_softmax_groups = compiled(CompiledOpKind::kSoftmax),
      .compiled_layernorm_groups = compiled(CompiledOpKind::kLayerNorm),
      .compiled_concat_groups = compiled(CompiledOpKind::kConcat),
      .create_state_calls = g_create_state_calls.load(std::memory_order_relaxed),
      .compute_calls = g_compute_calls.load(std::memory_order_relaxed),
      .compute_identity_calls = computed(CompiledOpKind::kIdentity),
      .compute_add_calls = computed(CompiledOpKind::kAdd),
      .compute_relu_calls = computed(CompiledOpKind::kRelu),
      .compute_sigmoid_calls = computed(CompiledOpKind::kSigmoid),
      .compute_tanh_calls = computed(CompiledOpKind::kTanh),
      .compute_gelu_calls = computed(CompiledOpKind::kGelu),
      .compute_matmul_calls = computed(CompiledOpKind::kMatMul),
      .compute_gemm_calls = computed(CompiledOpKind::kGemm),
      .compute_gemm_relu_gemm_calls = computed(CompiledOpKind::kGemmReluGemm),
      .compute_matmul_add_calls = computed(CompiledOpKind::kMatMulAdd),
      .compute_matmul_add_relu_calls = computed(CompiledOpKind::kMatMulAddRelu),
      .compute_add_relu_calls = computed(CompiledOpKind::kAddRelu),
      .compute_softmax_calls = computed(CompiledOpKind::kSoftmax),
      .compute_layernorm_calls = computed(CompiledOpKind::kLayerNorm),
      .compute_concat_calls = computed(CompiledOpKind::kConcat),
      .release_state_calls = g_release_state_calls.load(std::memory_order_relaxed),
  };
}

OrtStatus* ORT_API_CALL DoeOrtEp::GetCapabilityImpl(
    OrtEp* this_ptr,
    const OrtGraph* graph,
    OrtEpGraphSupportInfo* graph_support_info) NO_EXCEPTION {
  auto& self = Self(this_ptr);
  if (graph == nullptr || graph_support_info == nullptr) {
    return MakeStatus(
        self.bindings_.api,
        ORT_INVALID_ARGUMENT,
        "Doe ORT plugin EP requires non-null graph and graph_support_info inputs.");
  }
  if (self.bindings_.ep_api == nullptr) {
    return MakeStatus(self.bindings_.api, ORT_NOT_IMPLEMENTED, "Doe ORT plugin EP requires OrtEpApi to report graph support.");
  }

  g_get_capability_calls.fetch_add(1, std::memory_order_relaxed);

  std::vector<const OrtNode*> nodes;
  OrtStatus* status = GetGraphNodes(self.bindings_.api, graph, &nodes);
  if (status != nullptr) return status;

  std::unordered_set<const OrtNode*> claimed_nodes;
  for (const OrtNode* node : nodes) {
    if (node == nullptr || claimed_nodes.find(node) != claimed_nodes.end()) {
      continue;
    }

    SupportedNodeSpec node_spec;
    bool node_supported = false;
    status = GetSupportedNodeSpec(self.bindings_.api, node, &node_spec, &node_supported);
    if (status != nullptr) return status;
    if (!node_supported) {
      continue;
    }

    SupportedGraphSpec graph_spec{
        .kind = node_spec.kind,
        .default_node_name = node_spec.node_name.empty() ? CompiledOpKindName(node_spec.kind) : node_spec.node_name,
        .fused_nodes = {node},
        .logical_input_names = {},
    };

    if (node_spec.kind == CompiledOpKind::kMatMul) {
      SupportedNodeSpec add_spec;
      size_t matmul_output_input_index = 0;
      bool found_add = false;
      status = FindSupportedAddConsumer(
          self.bindings_.api, node_spec, claimed_nodes, &add_spec, &matmul_output_input_index, &found_add);
      if (status != nullptr) return status;
      if (found_add) {
        SupportedNodeSpec relu_spec;
        bool found_relu = false;
        status = FindSupportedReluConsumer(self.bindings_.api, add_spec, claimed_nodes, &relu_spec, &found_relu);
        if (status != nullptr) return status;
        graph_spec = found_relu
            ? BuildMatMulAddReluGraphSpec(node_spec, add_spec, matmul_output_input_index, relu_spec)
            : BuildMatMulAddGraphSpec(node_spec, add_spec, matmul_output_input_index);
      }
    } else if (node_spec.kind == CompiledOpKind::kGemm) {
      SupportedNodeSpec relu_spec;
      bool found_relu = false;
      status = FindSupportedReluConsumer(self.bindings_.api, node_spec, claimed_nodes, &relu_spec, &found_relu);
      if (status != nullptr) return status;
      if (found_relu) {
        SupportedNodeSpec second_gemm_spec;
        bool found_second_gemm = false;
        status = FindSupportedGemmConsumer(
            self.bindings_.api, relu_spec, claimed_nodes, &second_gemm_spec, &found_second_gemm);
        if (status != nullptr) return status;
        if (found_second_gemm) {
          graph_spec = BuildGemmReluGemmGraphSpec(node_spec, relu_spec, second_gemm_spec);
        }
      }
    } else if (node_spec.kind == CompiledOpKind::kAdd) {
      SupportedNodeSpec relu_spec;
      bool found_relu = false;
      status = FindSupportedReluConsumer(self.bindings_.api, node_spec, claimed_nodes, &relu_spec, &found_relu);
      if (status != nullptr) return status;
      if (found_relu) {
        graph_spec = BuildAddReluGraphSpec(node_spec, relu_spec);
      }
    }

    status = self.bindings_.ep_api->EpGraphSupportInfo_AddNodesToFuse(
        graph_support_info,
        graph_spec.fused_nodes.data(),
        graph_spec.fused_nodes.size(),
        nullptr);
    if (status != nullptr) return status;

    for (const OrtNode* claimed_node : graph_spec.fused_nodes) {
      claimed_nodes.insert(claimed_node);
    }
    IncrementClaimCounters(graph_spec.kind);
  }

  return nullptr;
}

OrtStatus* ORT_API_CALL DoeOrtEp::CompileImpl(
    OrtEp* this_ptr,
    const OrtGraph** graphs,
    const OrtNode** fused_nodes,
    const size_t count,
    OrtNodeComputeInfo** node_compute_infos,
    OrtNode** ep_context_nodes) NO_EXCEPTION {
  auto& self = Self(this_ptr);
  g_compile_calls.fetch_add(1, std::memory_order_relaxed);

  if (count != 0 && graphs == nullptr) {
    return MakeStatus(
        self.bindings_.api, ORT_INVALID_ARGUMENT, "Doe ORT plugin EP requires graphs when compiling assigned nodes.");
  }
  if (count != 0 && node_compute_infos == nullptr) {
    return MakeStatus(
        self.bindings_.api,
        ORT_INVALID_ARGUMENT,
        "Doe ORT plugin EP requires node_compute_infos when compiling graphs.");
  }
  if (node_compute_infos != nullptr) {
    for (size_t index = 0; index < count; ++index) {
      node_compute_infos[index] = nullptr;
    }
  }
  if (ep_context_nodes != nullptr) {
    for (size_t index = 0; index < count; ++index) {
      ep_context_nodes[index] = nullptr;
    }
  }

  for (size_t index = 0; index < count; ++index) {
    SupportedGraphSpec graph_spec;
    bool graph_supported = false;
    OrtStatus* status = AnalyzeSupportedGraph(self.bindings_.api, graphs[index], &graph_spec, &graph_supported);
    if (status != nullptr) {
      ReleaseNodeComputeInfosImpl(this_ptr, node_compute_infos, index);
      return status;
    }
    if (!graph_supported) {
      ReleaseNodeComputeInfosImpl(this_ptr, node_compute_infos, index);
      return MakeStatus(
          self.bindings_.api,
          ORT_NOT_IMPLEMENTED,
          "Doe ORT plugin EP was asked to compile a graph outside its narrow float32 Identity/Add/Relu/MatMul/Gemm/Gemm->Relu->Gemm/MatMul->Add/MatMul->Add->Relu/Add->Relu slice.");
    }

    auto info = std::make_unique<CompiledNodeComputeInfo>();
    info->base.ort_version_supported = ORT_API_VERSION;
    info->base.CreateState = &CreateStateImpl;
    info->base.Compute = &ComputeImpl;
    info->base.ReleaseState = &ReleaseStateImpl;
    info->bindings = self.bindings_;
    info->op_kind = graph_spec.kind;
    status = ResolveGraphInputBindings(
        self.bindings_.api,
        graphs[index],
        graph_spec,
        &info->input_binding_indices,
        &info->input_binding_count);
    if (status != nullptr) {
      ReleaseNodeComputeInfosImpl(this_ptr, node_compute_infos, index);
      return status;
    }

    const OrtNode* name_source =
        fused_nodes != nullptr && fused_nodes[index] != nullptr ? fused_nodes[index] : graph_spec.fused_nodes.front();
    const char* node_name = nullptr;
    status = self.bindings_.api->Node_GetName(name_source, &node_name);
    if (status != nullptr) {
      ReleaseNodeComputeInfosImpl(this_ptr, node_compute_infos, index);
      return status;
    }
    info->node_name = node_name != nullptr && node_name[0] != '\0' ? node_name : graph_spec.default_node_name;

    IncrementCompiledGroupCounters(info->op_kind);
    node_compute_infos[index] = &info.release()->base;
  }

  return nullptr;
}

void ORT_API_CALL DoeOrtEp::ReleaseNodeComputeInfosImpl(
    OrtEp* this_ptr,
    OrtNodeComputeInfo** node_compute_infos,
    const size_t num_node_compute_infos) NO_EXCEPTION {
  (void)this_ptr;
  if (node_compute_infos == nullptr) {
    return;
  }
  for (size_t index = 0; index < num_node_compute_infos; ++index) {
    if (node_compute_infos[index] != nullptr) {
      delete &ComputeInfoSelf(node_compute_infos[index]);
      node_compute_infos[index] = nullptr;
    }
  }
}

OrtStatus* ORT_API_CALL DoeOrtEp::GetPreferredDataLayoutImpl(
    OrtEp* this_ptr,
    OrtEpDataLayout* preferred_data_layout) NO_EXCEPTION {
  if (preferred_data_layout == nullptr) {
    return MakeStatus(
        Self(this_ptr).bindings_.api,
        ORT_INVALID_ARGUMENT,
        "Doe ORT plugin EP requires a preferred_data_layout output pointer.");
  }
  *preferred_data_layout = OrtEpDataLayout_NCHW;
  return nullptr;
}

OrtStatus* ORT_API_CALL DoeOrtEp::OnRunStartImpl(
    OrtEp* this_ptr,
    const OrtRunOptions* run_options) NO_EXCEPTION {
  (void)this_ptr;
  (void)run_options;
  return nullptr;
}

OrtStatus* ORT_API_CALL DoeOrtEp::OnRunEndImpl(
    OrtEp* this_ptr,
    const OrtRunOptions* run_options,
    const bool sync_stream) NO_EXCEPTION {
  (void)this_ptr;
  (void)run_options;
  (void)sync_stream;
  return nullptr;
}

const char* ORT_API_CALL DoeOrtEp::GetCompiledModelCompatibilityInfoImpl(
    OrtEp* this_ptr,
    const OrtGraph* graph) NO_EXCEPTION {
  (void)this_ptr;
  (void)graph;
  return kCompatibilityInfo;
}

}  // namespace doe::ort_ep
