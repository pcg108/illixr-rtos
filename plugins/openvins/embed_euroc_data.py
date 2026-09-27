#!/usr/bin/env python3
"""Embed a validated EuRoC prefix in build-directory C++ headers.

Usage: embed_euroc_data.py <mav0_dir> <output_dir> [num_cam_frames]
The default smoke prefix is 50 stereo pairs; use 200 for the extended run.
Every available initial IMU sample is retained, through the first sample at or
beyond 50 ms after the final camera. Measurement timestamps stay unmodified.
"""

import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path

TAIL_NS = 50_000_000
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def rows(path, width):
    result = []
    with path.open(newline="") as source:
        for line, row in enumerate(csv.reader(source), 1):
            if not row or row[0].strip().startswith("#"):
                continue
            if len(row) != width:
                raise ValueError(f"{path}:{line}: expected {width} columns")
            timestamp = int(row[0])
            if timestamp < 0 or (result and timestamp <= result[-1][0]):
                raise ValueError(f"{path}:{line}: timestamps must be positive and strictly increasing")
            result.append((timestamp, *[value.strip() for value in row[1:]]))
    if not result:
        raise ValueError(f"{path}: no samples")
    return result


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_byte_array(output, data):
    for offset in range(0, len(data), 32):
        output.write("    " + ",".join(f"0x{value:02x}" for value in data[offset:offset + 32]) + ",\n")


def generate(mav0, output, frame_count):
    if frame_count <= 0:
        raise ValueError("num_cam_frames must be positive")
    cameras = [rows(mav0 / name / "data.csv", 2) for name in ("cam0", "cam1")]
    if any(len(camera) < frame_count for camera in cameras):
        raise ValueError(f"dataset contains fewer than {frame_count} stereo pairs")
    cameras = [camera[:frame_count] for camera in cameras]
    for index, (left, right) in enumerate(zip(*cameras)):
        if left[0] != right[0]:
            raise ValueError(f"stereo timestamp mismatch at pair {index}: {left[0]} != {right[0]}")

    all_imu = rows(mav0 / "imu0" / "data.csv", 7)
    required_end = cameras[0][-1][0] + TAIL_NS
    end_index = bisect.bisect_left([sample[0] for sample in all_imu], required_end)
    if end_index == len(all_imu):
        raise ValueError("IMU input does not cover the final camera plus its 50 ms tail")
    imu = [(sample[0], *map(float, sample[1:])) for sample in all_imu[:end_index + 1]]
    if any(not math.isfinite(value) for sample in imu for value in sample[1:]):
        raise ValueError("IMU input contains a nonfinite measurement")
    if imu[0][0] > cameras[0][0][0]:
        raise ValueError("IMU input must begin at or before the first camera")

    files = {}
    for sensor in ("cam0", "cam1", "imu0"):
        relative = f"{sensor}/data.csv"
        files[relative] = {"sha256": sha256(mav0 / relative), "bytes": (mav0 / relative).stat().st_size}
    images = []
    for index, pair in enumerate(zip(*cameras)):
        record = []
        for sensor, (timestamp, filename) in enumerate(pair):
            # Filenames are CSV basenames, never filesystem paths.
            if not filename or Path(filename).name != filename:
                raise ValueError(f"invalid image filename: {filename!r}")
            relative = f"cam{sensor}/data/{filename}"
            path = mav0 / relative
            with path.open("rb") as source:
                if source.read(8) != PNG_SIGNATURE:
                    raise ValueError(f"{path}: expected a PNG image")
            files[relative] = {"sha256": sha256(path), "bytes": path.stat().st_size}
            record.append((path, path.stat().st_size))
        images.append(record)

    manifest = {
        "format_version": 1,
        "camera_pairs": frame_count,
        "imu_samples": len(imu),
        "dataset_origin_ns": min(imu[0][0], cameras[0][0][0]),
        "dataset_end_ns": max(imu[-1][0], cameras[0][-1][0]),
        "first_imu_ns": imu[0][0], "last_imu_ns": imu[-1][0],
        "first_camera_ns": cameras[0][0][0], "last_camera_ns": cameras[0][-1][0],
        "imu_tail_ns": TAIL_NS,
        "embedded_png_bytes": sum(size for pair in images for path, size in pair),
        "files": files,
    }
    digest = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    manifest["selection_sha256"] = digest
    output.mkdir(parents=True, exist_ok=True)
    prefix = "#pragma once\n#include <cstdint>\n#include <cstddef>\n\n"
    with (output / "embedded_dataset.hpp").open("w") as dest:
        dest.write(prefix)
        for name, value in (
            ("Origin", manifest["dataset_origin_ns"]),
            ("End", manifest["dataset_end_ns"]),
        ):
            dest.write(f"static constexpr int64_t kEmbeddedDataset{name}Ns = {value}LL;\n")
        for name, value in (
            ("FirstImu", imu[0][0]), ("LastImu", imu[-1][0]),
            ("FirstCam", cameras[0][0][0]), ("LastCam", cameras[0][-1][0]),
        ):
            dest.write(f"static constexpr int64_t kEmbedded{name}Ns = {value}LL;\n")
        dest.write(f"static constexpr size_t kEmbeddedDatasetImuCount = {len(imu)};\n")
        dest.write(f"static constexpr size_t kEmbeddedDatasetCamCount = {frame_count};\n")
        dest.write(f'static constexpr char kEmbeddedDatasetSha256[] = "{digest}";\n')

    with (output / "embedded_imu.hpp").open("w") as dest:
        dest.write(prefix)
        dest.write(f"static constexpr size_t kEmbeddedImuCount = {len(imu)};\n")
        dest.write("struct EmbeddedImuSample { int64_t ts_ns; double wx, wy, wz, ax, ay, az; };\n")
        dest.write("static const EmbeddedImuSample kEmbeddedImu[] = {\n")
        for timestamp, *values in imu:
            dest.write("    {" + str(timestamp) + "LL, " + ", ".join(f"{value:.17e}" for value in values) + "},\n")
        dest.write("};\n")

    with (output / "embedded_cam.hpp").open("w") as dest:
        dest.write(prefix)
        dest.write(f"static constexpr size_t kEmbeddedCamCount = {frame_count};\n")
        for index, pair in enumerate(images):
            for sensor, (path, size) in enumerate(pair):
                dest.write(f"static const uint8_t kCam{sensor}Frame{index}[] = {{\n")
                write_byte_array(dest, path.read_bytes())
                dest.write("};\n")
        dest.write("struct EmbeddedCamFrame {\n")
        dest.write("    int64_t ts_ns;\n    const uint8_t* cam0_png;\n    size_t cam0_size;\n")
        dest.write("    const uint8_t* cam1_png;\n    size_t cam1_size;\n};\n")
        dest.write("static const EmbeddedCamFrame kEmbeddedCam[] = {\n")
        for index, pair in enumerate(images):
            dest.write(f"    {{{cameras[0][index][0]}LL, kCam0Frame{index}, {pair[0][1]}, "
                       f"kCam1Frame{index}, {pair[1][1]}}},\n")
        dest.write("};\n")
    (output / "dataset_manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"Embedded {frame_count} stereo pairs, {len(imu)} IMUs, "
          f"{manifest['embedded_png_bytes'] / 1024**2:.1f} MiB PNG data; SHA256 {digest}")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mav0_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("num_cam_frames", type=int, nargs="?", default=50)
    args = parser.parse_args()
    try:
        generate(args.mav0_dir, args.output_dir, args.num_cam_frames)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Dataset generation failed: {error}\n")


if __name__ == "__main__":
    main()
