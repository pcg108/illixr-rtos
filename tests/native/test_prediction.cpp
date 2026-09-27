#include "../../src/prediction_policy.hpp"
#include "../../src/prediction_adapter.hpp"
#include "desktop_prediction_reference.hpp"
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

using namespace ILLIXR;
static desktop_reference::Input reference_input(const PredictionState& s) {
    return {s.w_hat, s.a_hat, s.w_hat2, s.a_hat2, s.position, s.velocity, s.orientation};
}

int main() {
    PredictionState state;
    state.timestamp_ns = 1'000'000'000;
    state.previous_timestamp_ns = 995'000'000;
    state.a_hat = state.a_hat2 = Eigen::Vector3d{0, 0, 9.81};
    for (double dt : {0.000001, 0.001, 0.005, 0.01, 0.05}) {
        const auto stationary = DesktopPredictionMath::predict_mean_rk4(state, dt);
        assert(stationary.segment<3>(4).norm() < 1e-14);
        assert(stationary.segment<3>(7).norm() < 1e-14);
        assert(stationary(3) == 1.0);
    }
    state.velocity = {0.5, -1.25, 2.0};
    const auto constant_velocity = DesktopPredictionMath::predict_mean_rk4(state, 0.05);
    assert((constant_velocity.segment<3>(4) - state.velocity * 0.05).norm() < 1e-14);

    std::mt19937 generator{1729};
    std::uniform_real_distribution<double> random{-1, 1};
    double maximum_error{};
    for (unsigned i = 0; i < 500; ++i) {
        auto vector = [&] { return Eigen::Vector3d{random(generator), random(generator), random(generator)}; };
        state.position = vector() * 3;
        state.velocity = vector() * 2;
        state.w_hat = vector() * 5;
        state.w_hat2 = vector() * 5;
        state.a_hat = vector() * 15;
        state.a_hat2 = vector() * 15;
        state.orientation = Eigen::Quaterniond{random(generator), random(generator), random(generator), random(generator)}.normalized();
        const double dt = 0.000001 + (random(generator) + 1.0) * 0.024;
        const auto reference = desktop_reference::Prediction::predict_mean_rk4(reference_input(state), dt);
        Eigen::internal::set_is_malloc_allowed(false);
        const auto actual = DesktopPredictionMath::predict_mean_rk4(state, dt);
        Eigen::internal::set_is_malloc_allowed(true);
        maximum_error = std::max(maximum_error, (actual - reference).norm());
        assert((actual - reference).norm() < 1e-12);
        assert(std::abs(actual.head<4>().norm() - 1.0) < 1e-12);
    }

    // Adapter proof: the original integrator stores Hamilton G-to-I. The
    // desktop predictor expects the conjugate as JPL G-to-I coefficients.
    const Eigen::Quaterniond integrator_orientation{Eigen::AngleAxisd{0.7, Eigen::Vector3d{1,2,3}.normalized()}};
    const auto snapshot_q = integrator_orientation.conjugate();
    const Eigen::Vector4d coefficients{snapshot_q.x(), snapshot_q.y(), snapshot_q.z(), snapshot_q.w()};
    assert((DesktopPredictionMath::quat_2_Rot(coefficients).transpose()
        - integrator_orientation.toRotationMatrix().transpose()).norm() < 1e-12);

    // This oracle is independent of the desktop predictor: it continues the
    // existing integrator's closed-form constant-rate Hamilton update and
    // compares the future snapshot orientation, not merely a static rotation.
    // Exercise the actual helper called by the integrator's publication path.
    const Eigen::Quaterniond starts[] = {
        Eigen::Quaterniond::Identity(), integrator_orientation,
        Eigen::Quaterniond{Eigen::AngleAxisd{1.2, Eigen::Vector3d::UnitZ()}
            * Eigen::AngleAxisd{-0.55, Eigen::Vector3d::UnitX()}},
        Eigen::Quaterniond{Eigen::AngleAxisd{-1.0, Eigen::Vector3d{2,-1,0.5}.normalized()}}};
    const Eigen::Vector3d rates[] = {{0,0,0}, {1,0,0}, {0,-2,0}, {0,0,1},
                                   {0.7,-0.4,1.1}, {-1.5,0.3,-0.8}};
    auto continued_snapshot = [](const Eigen::Quaterniond& initial,
                                 const Eigen::Vector3d& rate, double dt) {
        const auto norm = rate.norm();
        const Eigen::Quaterniond increment = norm > 0
            ? Eigen::Quaterniond{Eigen::AngleAxisd{norm * dt, rate / norm}}
            : Eigen::Quaterniond::Identity();
        return (initial * increment).conjugate();
    };
    auto predicted_rotation = [](const PredictionState& snapshot, double dt) {
        const auto raw = DesktopPredictionMath::predict_mean_rk4(snapshot, dt);
        return Eigen::Quaterniond{raw(3),raw(0),raw(1),raw(2)};
    };
    double maximum_continuity_error{};
    unsigned continuity_cases{};
    for (const auto& initial : starts)
        for (const auto& rate : rates)
            for (double dt : {0.001, 0.005, 0.01, 0.025, 0.05}) {
                PredictionState snapshot;
                snapshot.a_hat = snapshot.a_hat2 = Eigen::Vector3d{0,0,9.81};
                Eigen::internal::set_is_malloc_allowed(false);
                adapt_integrator_rotation(snapshot, initial, rate, rate);
                const auto actual = predicted_rotation(snapshot, dt);
                Eigen::internal::set_is_malloc_allowed(true);
                const auto expected = continued_snapshot(initial, rate, dt);
                const double error = actual.angularDistance(expected);
                maximum_continuity_error = std::max(maximum_continuity_error, error);
                assert(error < 1e-7);
                ++continuity_cases;
            }
    // Distinct endpoints ensure the helper does not accidentally duplicate one
    // measurement. Check each against an independent constant-rate continuation.
    PredictionState endpoints;
    adapt_integrator_rotation(endpoints, starts[2], rates[4], rates[5]);
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        auto snapshot = endpoints;
        snapshot.w_hat = snapshot.w_hat2 = endpoint ? endpoints.w_hat2 : endpoints.w_hat;
        assert(predicted_rotation(snapshot, 0.01).angularDistance(
            continued_snapshot(starts[2], rates[4 + endpoint], 0.01)) < 1e-7);
    }
    // Regression controls: quaternion conjugation with unadapted gyro, and
    // negation without the frame rotation, must fail the continuity criterion.
    PredictionState wrong;
    wrong.orientation = starts[2].conjugate();
    wrong.w_hat = wrong.w_hat2 = rates[4];
    const auto expected = continued_snapshot(starts[2], rates[4], 0.05);
    assert(predicted_rotation(wrong, 0.05).angularDistance(expected) > 0.001);
    wrong.w_hat = wrong.w_hat2 = -rates[4];
    assert(predicted_rotation(wrong, 0.05).angularDistance(expected) > 0.001);

    // Policy guards and coordinate conversion are tested independently of the
    // reference's unchecked dt division. Zero dt preserves the complete state.
    const auto zero = evaluate_prediction(&state, 1, state.timestamp_ns, 10);
    assert(zero.result.valid());
    assert((zero.raw.segment<3>(4) - state.position).norm() == 0.0);
    assert(evaluate_prediction(&state, 1, state.timestamp_ns - 1, 10).result.status == PredictionStatus::Invalid);
    assert(evaluate_prediction(nullptr, 0, state.timestamp_ns, 10).result.status == PredictionStatus::Fallback);
    assert(evaluate_prediction(&state, 1, state.timestamp_ns + 50'000'001, 10).result.stale());
    PredictionPolicy policy;
    Eigen::Quaternionf used;
    const auto first = policy.complete(PredictionConsumer::Render, zero, used);
    assert(used.isApprox(Eigen::Quaternionf::Identity()));
    assert((first.position - Eigen::Vector3f{-float(state.position.y()),float(state.position.z()),-float(state.position.x())}).norm() == 0.0f);
    const auto second = policy.complete(PredictionConsumer::Timewarp, zero, used);
    assert(second.orientation.isApprox(Eigen::Quaternionf::Identity(), 1e-6f));
    const auto stale_eval = evaluate_prediction(&state, 2, state.timestamp_ns + 60'000'000, 30);
    const auto frozen_render = policy.complete(PredictionConsumer::Render, stale_eval, used);
    const auto frozen_warp = policy.complete(PredictionConsumer::Timewarp, stale_eval, used);
    assert(frozen_render.stale() && frozen_warp.stale());
    assert(frozen_render.orientation.isApprox(first.orientation));
    assert(frozen_warp.orientation.isApprox(second.orientation));
    assert(frozen_render.source_sequence == 1 && frozen_warp.source_sequence == 1);
    assert(frozen_render.target_timestamp_ns == stale_eval.result.target_timestamp_ns);

    policy.reset();
    std::mutex lock;
    std::atomic<unsigned> completed{};
    auto caller = [&](PredictionConsumer consumer) {
        for (unsigned i = 0; i < 1000; ++i) {
            const auto evaluation = evaluate_prediction(&state, i + 1, state.timestamp_ns + 5'000'000, i);
            std::lock_guard guard{lock};
            Eigen::Quaternionf offset;
            const auto result = policy.complete(consumer, evaluation, offset);
            assert(result.valid());
            assert(std::abs(result.orientation.norm() - 1.0f) < 1e-6f);
            ++completed;
        }
    };
    std::thread render{caller, PredictionConsumer::Render};
    std::thread timewarp{caller, PredictionConsumer::Timewarp};
    render.join(); timewarp.join();
    assert(completed == 2000);
    std::cout << "prediction: 500 independent desktop comparisons; max raw error=" << maximum_error
              << "; " << continuity_cases << " independent angular-continuity cases; max error="
              << maximum_continuity_error << " rad; guards, conventions, allocation and concurrent callers passed\n";
}
