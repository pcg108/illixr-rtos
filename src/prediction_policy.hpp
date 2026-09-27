#pragma once
#include "prediction_math.hpp"
#include <array>

namespace ILLIXR {
struct PredictionEvaluation {
    PredictionResult result;
    Eigen::Matrix<double, 13, 1> raw{Eigen::Matrix<double, 13, 1>::Zero()};
};

// Pure, allocation-free evaluation; the caller owns the coherent input snapshot.
inline PredictionEvaluation evaluate_prediction(const PredictionState* state,
                                                std::uint64_t sequence,
                                                std::int64_t target_ns,
                                                std::int64_t computed_ns) {
    PredictionEvaluation evaluation;
    auto& result = evaluation.result;
    result.computed_runtime_ns = computed_ns;
    result.target_timestamp_ns = target_ns;
    if (!state || !sequence)
        return evaluation;
    result.source_timestamp_ns = state->timestamp_ns;
    result.source_sequence = sequence;
    result.horizon_ns = target_ns - state->timestamp_ns;
    const bool finite = state->position.allFinite() && state->velocity.allFinite()
        && state->orientation.coeffs().allFinite() && state->orientation.norm() > 0.0
        && state->w_hat.allFinite() && state->a_hat.allFinite()
        && state->w_hat2.allFinite() && state->a_hat2.allFinite();
    if (!finite || state->timestamp_ns < 0 || result.horizon_ns < 0) {
        result.status = PredictionStatus::Invalid;
        return evaluation;
    }
    if (result.horizon_ns > prediction_max_horizon_ns) {
        result.status = PredictionStatus::Stale;
        return evaluation;
    }
    evaluation.raw = DesktopPredictionMath::predict_mean_rk4(*state,
        static_cast<double>(result.horizon_ns) * 1e-9);
    if (!evaluation.raw.allFinite()) {
        evaluation.raw.setZero();
        result.status = PredictionStatus::Invalid;
        return evaluation;
    }
    result.position = evaluation.raw.template segment<3>(4).cast<float>();
    result.orientation = Eigen::Quaternionf{
        static_cast<float>(evaluation.raw(3)), static_cast<float>(evaluation.raw(0)),
        static_cast<float>(evaluation.raw(1)), static_cast<float>(evaluation.raw(2))};
    result.status = PredictionStatus::Valid;
    return evaluation;
}

// The service serializes only this short finalization step. RK4 is performed
// outside its lock. Native tests use exactly this policy under a host mutex.
class PredictionPolicy {
public:
    void reset() { *this = PredictionPolicy{}; }
    PredictionResult complete(PredictionConsumer consumer, PredictionEvaluation evaluation,
                              Eigen::Quaternionf& offset_used) {
        auto result = evaluation.result;
        const auto index = static_cast<unsigned>(consumer);
        offset_used = offset_;
        if (index >= last_valid_.size()) {
            result.status = PredictionStatus::Invalid;
            return result;
        }
        if (result.valid()) {
            result.position = Eigen::Vector3f{-result.position.y(), result.position.z(), -result.position.x()};
            const auto q = result.orientation;
            result.orientation = Eigen::Quaternionf{q.w(), -q.y(), q.z(), -q.x()} * offset_;
            // Preserve desktop ordering: the first returned prediction precedes
            // establishment of the shared straight-ahead orientation offset.
            if (first_valid_) {
                offset_ = result.orientation.inverse();
                first_valid_ = false;
            }
            last_valid_[index] = result;
            has_valid_[index] = true;
        } else if (result.stale() && has_valid_[index]) {
            const auto& previous = last_valid_[index];
            result.position = previous.position;
            result.orientation = previous.orientation;
            // These identify the state which actually produced the frozen pose.
            result.source_timestamp_ns = previous.source_timestamp_ns;
            result.source_sequence = previous.source_sequence;
            result.horizon_ns = result.target_timestamp_ns - previous.source_timestamp_ns;
        }
        return result;
    }
private:
    Eigen::Quaternionf offset_{Eigen::Quaternionf::Identity()};
    bool first_valid_{true};
    std::array<PredictionResult, static_cast<unsigned>(PredictionConsumer::Count)> last_valid_{};
    std::array<bool, static_cast<unsigned>(PredictionConsumer::Count)> has_valid_{};
};
} // namespace ILLIXR
