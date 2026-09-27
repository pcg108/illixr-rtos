#include "SLAMMath.hpp"
#include "vio_config.hpp"
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

namespace {
struct Imu { int64_t ns; Eigen::Vector3d w, a; };
struct Cam { int64_t ns; std::string filename; };
struct Event { std::string kind; size_t index; };

std::vector<std::vector<std::string>> csv(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open " + path);
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream record(line);
        std::vector<std::string> cells;
        std::string cell;
        while (std::getline(record, cell, ',')) {
            const auto last = cell.find_last_not_of(" \r\t");
            if (last != std::string::npos) cell.erase(last + 1);
            cells.push_back(cell);
        }
        rows.push_back(cells);
    }
    return rows;
}

std::vector<Event> events_from_trace(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open trace " + path);
    std::vector<Event> events;
    std::string line;
    while (std::getline(stream, line)) {
        const auto marker = line.find("ILLIXR_TRACE ");
        if (marker == std::string::npos) continue;
        std::istringstream record(line.substr(marker + 13));
        Event event;
        if (!(record >> event.kind >> event.index) ||
            (event.kind != "IMU" && event.kind != "CAM"))
            throw std::runtime_error("Invalid trace record: " + line);
        events.push_back(event);
    }
    if (events.empty()) throw std::runtime_error("No estimator input events in trace");
    return events;
}
}

int main(int argc, char** argv) {
  try {
    std::string dataset, trace, output;
    size_t frames = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) throw std::runtime_error("Missing argument value");
        const std::string key(argv[i]), value(argv[i+1]);
        if (key == "--dataset") dataset = value;
        else if (key == "--trace") trace = value;
        else if (key == "--output") output = value;
        else if (key == "--frames") frames = std::stoul(value);
        else throw std::runtime_error("Unknown argument " + key);
    }
    if (dataset.empty() || output.empty() || (trace.empty() == (frames == 0)))
        throw std::runtime_error("Usage: estimator_replay --dataset MAV0 --output FILE (--trace LOG | --frames N)");
    std::vector<Imu> imus;
    for (const auto& row : csv(dataset + "/imu0/data.csv")) {
        if (row.size() != 7) throw std::runtime_error("Invalid IMU CSV row");
        imus.push_back({std::stoll(row[0]), {std::stod(row[1]), std::stod(row[2]), std::stod(row[3])},
            {std::stod(row[4]), std::stod(row[5]), std::stod(row[6])}});
    }
    std::vector<Cam> cam0, cam1;
    for (const auto& row : csv(dataset + "/cam0/data.csv")) cam0.push_back({std::stoll(row.at(0)), row.at(1)});
    for (const auto& row : csv(dataset + "/cam1/data.csv")) cam1.push_back({std::stoll(row.at(0)), row.at(1)});
    std::vector<Event> events;
    if (!trace.empty()) events = events_from_trace(trace);
    else {
        if (frames > cam0.size() || frames > cam1.size()) throw std::runtime_error("Insufficient camera frames");
        size_t imu = 0;
        for (size_t cam = 0; cam < frames; ++cam) {
            while (imu < imus.size() && (imu == 0 || imus[imu-1].ns < cam0[cam].ns))
                events.push_back({"IMU", imu++});
            events.push_back({"CAM", cam});
        }
        while (imu < imus.size() && imus[imu].ns <= cam0[frames-1].ns + 50000000)
            events.push_back({"IMU", imu++});
    }
    std::ofstream out(output);
    if (!out) throw std::runtime_error("Cannot create output " + output);
    out << std::setprecision(17);
    cv::setNumThreads(1);
    cv::setUseOptimized(false);
    OpenVINS::MSCKFEstimator estimator(OpenVINS::create_vio_config());
    size_t imu_count = 0, cam_count = 0, poses = 0;
    size_t previous_cam = 0;
    for (const auto& event : events) {
        out << "ILLIXR_TRACE " << event.kind << ' ' << event.index << '\n';
        if (event.kind == "IMU") {
            if (event.index != imu_count) throw std::runtime_error("IMU indices must be contiguous from zero");
            const auto& imu = imus.at(event.index);
            estimator.feed_imu(static_cast<double>(imu.ns) * 1e-9, imu.w, imu.a);
            ++imu_count;
        } else {
            if (cam_count && event.index <= previous_cam) throw std::runtime_error("Camera indices must increase");
            previous_cam = event.index;
            const auto& left = cam0.at(event.index);
            const auto& right = cam1.at(event.index);
            if (left.ns != right.ns) throw std::runtime_error("Stereo timestamps differ");
            const cv::Mat a = cv::imread(dataset + "/cam0/data/" + left.filename, cv::IMREAD_GRAYSCALE);
            const cv::Mat b = cv::imread(dataset + "/cam1/data/" + right.filename, cv::IMREAD_GRAYSCALE);
            if (a.empty() || b.empty()) throw std::runtime_error("Cannot decode stereo pair");
            estimator.feed_stereo(static_cast<double>(left.ns) * 1e-9, a, b);
            ++cam_count;
            if (estimator.is_initialized()) {
                const auto& state = estimator.get_state();
                Eigen::Vector3f p = state.p_IinG.cast<float>();
                Eigen::Quaternionf q = state.q_GtoI.cast<float>();
                q.normalize();
                if (!p.allFinite() || !q.coeffs().allFinite()) throw std::runtime_error("Non-finite native pose");
                out << "ILLIXR_POSE " << event.index << ' ' << left.ns << ' '
                    << p.x() << ' ' << p.y() << ' ' << p.z() << ' '
                    << q.w() << ' ' << q.x() << ' ' << q.y() << ' ' << q.z() << '\n';
                ++poses;
            }
        }
    }
    out << "ILLIXR_NATIVE_RESULT {\"imu_processed\":" << imu_count
        << ",\"cam_processed\":" << cam_count << ",\"poses\":" << poses
        << ",\"initialized\":" << (estimator.is_initialized() ? "true" : "false") << "}\n";
    std::cout << "Replayed " << imu_count << " IMUs, " << cam_count << " stereo pairs; " << poses << " initialized poses\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
