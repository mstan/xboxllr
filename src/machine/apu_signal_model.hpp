#pragma once
namespace xlle {
// Immutable construction-time policy, independent of graphics/presentation.
enum class ApuSignalModel { Strict, NominalDls2LinearV1, XemuStateVariableV1 };
inline const char* apu_signal_model_name(ApuSignalModel model) {
    return model==ApuSignalModel::Strict?"strict":
           model==ApuSignalModel::NominalDls2LinearV1?"nominal-dls2-linear-v1":
           model==ApuSignalModel::XemuStateVariableV1?"xemu-state-variable-v1":"invalid";
}
} // namespace xlle
