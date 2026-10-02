#include "trace_output.hpp"
#pragma once
#include "latest_value.hpp"
#include "prediction_policy.hpp"
#include "relative_clock.hpp"
#include <cstdio>

namespace ILLIXR {
inline LatestValue<PredictionState> prediction_state;

class PosePredictionService {
public:
    PosePredictionService() { k_mutex_init(&mutex_); }
    void reset() {
        k_mutex_lock(&mutex_, K_FOREVER);
        policy_.reset();
        trace_size_ = overflow_ = invalid_ = 0;
        for (auto& placement : placements_)
            placement = Placement{};
        k_mutex_unlock(&mutex_);
    }
    PredictionResult predict(PredictionConsumer consumer, std::int64_t target_ns) {
        PredictionState input;
        std::uint64_t sequence{};
        const bool available = prediction_state.read(input, sequence);
        const unsigned processing_hart = current_hart();
        auto evaluation = evaluate_prediction(available ? &input : nullptr, sequence,
            target_ns, get_global_relative_clock().now_ns());
        evaluation.result.computed_runtime_ns = get_global_relative_clock().now_ns();
        // All Eigen RK4 work is complete before taking the shared state lock.
        k_mutex_lock(&mutex_, K_FOREVER);
        Eigen::Quaternionf offset_used;
        const auto result = policy_.complete(consumer, evaluation, offset_used);
        const auto caller = static_cast<unsigned>(consumer);
        const unsigned publication_hart = current_hart();
        if (caller < static_cast<unsigned>(PredictionConsumer::Count)
            && processing_hart < CONFIG_MP_MAX_NUM_CPUS && publication_hart < CONFIG_MP_MAX_NUM_CPUS) {
            auto& placement = placements_[caller];
            ++placement.work_counts[processing_hart];
            ++placement.publication_counts[publication_hart];
            placement.hart_mask |= (1u << processing_hart) | (1u << publication_hart);
        } else {
            ++invalid_;
        }
        if (result.status == PredictionStatus::Invalid)
            ++invalid_;
        if (trace_size_ < trace_capacity) {
            auto& trace = traces_[trace_size_++];
            trace.consumer = consumer;
            trace.input = input;
            trace.input_sequence = sequence;
            trace.result = result;
            trace.raw = evaluation.raw;
            trace.offset = offset_used;
            trace.processing_hart = processing_hart;
            trace.publication_hart = publication_hart;
        } else {
            ++overflow_;
        }
        k_mutex_unlock(&mutex_);
        return result;
    }
    // Main calls these after both consumers have joined.
    [[nodiscard]] bool validate() const { return overflow_ == 0 && invalid_ == 0; }
    void dump() const {
        for (std::size_t i = 0; i < trace_size_; ++i) {
            const auto& t = traces_[i];
            const auto& r = t.result;
            trace_output::print("ILLIXR_PREDICTION {\"caller\":%u,\"status\":%u,\"processing_hart\":%u,"
                "\"publication_hart\":%u,\"source_ns\":%lld,"
                "\"source_seq\":%llu,\"computed_ns\":%lld,\"target_ns\":%lld,\"horizon_ns\":%lld,"
                "\"input_seq\":%llu,\"input\":{\"timestamp_ns\":%lld,\"previous_timestamp_ns\":%lld,\"position\":",
                static_cast<unsigned>(t.consumer), static_cast<unsigned>(r.status),
                t.processing_hart, t.publication_hart,
                static_cast<long long>(r.source_timestamp_ns), static_cast<unsigned long long>(r.source_sequence),
                static_cast<long long>(r.computed_runtime_ns), static_cast<long long>(r.target_timestamp_ns),
                static_cast<long long>(r.horizon_ns), static_cast<unsigned long long>(t.input_sequence),
                static_cast<long long>(t.input.timestamp_ns), static_cast<long long>(t.input.previous_timestamp_ns));
            vector(t.input.position); trace_output::print(",\"velocity\":"); vector(t.input.velocity);
            trace_output::print(",\"orientation\":"); quaternion(t.input.orientation);
            trace_output::print(",\"w_hat\":"); vector(t.input.w_hat);
            trace_output::print(",\"a_hat\":"); vector(t.input.a_hat);
            trace_output::print(",\"w_hat2\":"); vector(t.input.w_hat2);
            trace_output::print(",\"a_hat2\":"); vector(t.input.a_hat2);
            trace_output::print("},\"raw\":"); vector(t.raw);
            trace_output::print(",\"position\":"); vector(r.position);
            trace_output::print(",\"orientation\":"); quaternion(r.orientation);
            trace_output::print(",\"offset\":"); quaternion(t.offset);
            trace_output::print("}\n");
        }
        for (unsigned caller = 0; caller < static_cast<unsigned>(PredictionConsumer::Count); ++caller) {
            const auto& placement = placements_[caller];
            trace_output::print("ILLIXR_PREDICTION_PLACEMENT {\"caller\":%u,\"hart_mask\":%u,\"work_counts\":[",
                caller, placement.hart_mask);
            for (unsigned hart = 0; hart < CONFIG_MP_MAX_NUM_CPUS; ++hart)
                trace_output::print("%s%llu", hart ? "," : "", static_cast<unsigned long long>(placement.work_counts[hart]));
            trace_output::print("],\"publication_counts\":[");
            for (unsigned hart = 0; hart < CONFIG_MP_MAX_NUM_CPUS; ++hart)
                trace_output::print("%s%llu", hart ? "," : "", static_cast<unsigned long long>(placement.publication_counts[hart]));
            trace_output::print("]}\n");
        }
        trace_output::print("ILLIXR_PREDICTION_SUMMARY {\"calls\":%llu,\"overflow\":%llu,\"invalid\":%llu,"
                    "\"max_horizon_ns\":%lld}\n",
            static_cast<unsigned long long>(trace_size_), static_cast<unsigned long long>(overflow_),
            static_cast<unsigned long long>(invalid_), static_cast<long long>(prediction_max_horizon_ns));
    }
private:
    static constexpr std::size_t trace_capacity = 8192;
    static unsigned current_hart() {
        unsigned long hart;
        __asm__ volatile("csrr %0, mhartid" : "=r"(hart));
        return static_cast<unsigned>(hart);
    }
    struct Placement {
        std::uint64_t work_counts[CONFIG_MP_MAX_NUM_CPUS]{}, publication_counts[CONFIG_MP_MAX_NUM_CPUS]{};
        unsigned hart_mask{};
    };
    struct Trace {
        PredictionState input;
        std::uint64_t input_sequence{};
        PredictionResult result;
        Eigen::Matrix<double, 13, 1> raw;
        Eigen::Quaternionf offset;
        PredictionConsumer consumer{};
        unsigned processing_hart{}, publication_hart{};
    };
    template<class Derived> static void vector(const Eigen::MatrixBase<Derived>& values) {
        trace_output::print("[");
        for (Eigen::Index i = 0; i < values.size(); ++i)
            trace_output::print("%s%.17g", i ? "," : "", static_cast<double>(values(i)));
        trace_output::print("]");
    }
    template<class Scalar> static void quaternion(const Eigen::Quaternion<Scalar>& q) {
        trace_output::print("[%.17g,%.17g,%.17g,%.17g]", static_cast<double>(q.w()),
            static_cast<double>(q.x()), static_cast<double>(q.y()), static_cast<double>(q.z()));
    }
    k_mutex mutex_{};
    PredictionPolicy policy_;
    Placement placements_[static_cast<unsigned>(PredictionConsumer::Count)]{};
    Trace traces_[trace_capacity]{};
    std::size_t trace_size_{}, overflow_{}, invalid_{};
};

inline PosePredictionService& get_pose_prediction() {
    static PosePredictionService service;
    return service;
}
} // namespace ILLIXR
