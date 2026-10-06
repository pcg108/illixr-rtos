#!/usr/bin/env python3
"""Verify generated FireSim hardware, routed reports, packages and firmware.

This inspects existing artifacts only; it neither builds nor programs hardware.
Platform boot and complete workload acceptance remain separate runtime gates.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tarfile
from datetime import datetime, timezone

from setup_firesim import manifest, DATA, same_or_new

MEMORY_RUNTIME = {"readLatency": 30, "writeLatency": 30, "readMaxReqs": 10, "writeMaxReqs": 10}
HOST_INTERFACE = {"fesvr_step_size_cycles": 10000, "idle_counts": 1, "wait_ticks": 8}

def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def evidence(path):
    return {"path": str(Path(path).resolve()), "sha256": sha256(path)}


def number(value):
    return int.from_bytes(value, "big")


def cells(value):
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


def strings(value):
    return value.rstrip(b"\0").decode().split("\0") if value else []


def read_dts(path):
    blob = subprocess.check_output(["dtc", "-I", "dts", "-O", "dtb", str(path)],
                                   stderr=subprocess.PIPE)
    magic, _, pos, string_offset = struct.unpack_from(">4I", blob)
    require(magic == 0xD00DFEED, "dtc returned an invalid FDT")
    nodes, stack = {}, []
    while True:
        tag, = struct.unpack_from(">I", blob, pos)
        pos += 4
        if tag == 1:
            end = blob.index(0, pos)
            stack.append(blob[pos:end].decode())
            pos = (end + 4) & ~3
            nodes["/" + "/".join(stack[1:])] = {}
        elif tag == 2:
            stack.pop()
        elif tag == 3:
            length, offset = struct.unpack_from(">2I", blob, pos)
            pos += 8
            start = string_offset + offset
            key = blob[start:blob.index(0, start)].decode()
            nodes["/" + "/".join(stack[1:])][key] = blob[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif tag == 4:
            continue
        elif tag == 9:
            return nodes
        else:
            raise ValueError(f"Unknown FDT tag {tag}")


def reg_region(path, props, nodes):
    parent = nodes[path.rsplit("/", 1)[0] or "/"]
    address_bytes = number(parent.get("#address-cells", b"\0\0\0\2")) * 4
    size_bytes = number(parent.get("#size-cells", b"\0\0\0\1")) * 4
    require(len(props["reg"]) == address_bytes + size_bytes,
            f"Expected a single register region for {path}")
    return {"base": number(props["reg"][:address_bytes]),
            "size": number(props["reg"][address_bytes:])}


def inspect_hardware(dts, harts):
    nodes = read_dts(dts)
    cpus, memories, controllers = [], [], {"clint": [], "plic": []}
    for path, props in nodes.items():
        if props.get("status") == b"disabled\0":
            continue
        if props.get("device_type") == b"cpu\0":
            isa = strings(props["riscv,isa"])[0]
            require(isa.startswith("rv64") and set("imafdc") <= set(isa.split("_")[0][4:]),
                    f"RV64IMAFDC firmware ISA unsupported: {isa}")
            cpu_hz = number(props.get("clock-frequency", b"\0"))
            require(cpu_hz in (0, 500000000), f"Unexpected CPU DTS frequency {cpu_hz}")
            cpus.append({"path": path, "hart_id": number(props["reg"]),
                         "isa": isa, "dts_clock_hz": cpu_hz, "clock_hz": 500000000})
        if props.get("device_type") == b"memory\0":
            memories.append({"path": path, **reg_region(path, props, nodes)})
        for kind in controllers:
            if any(kind in c for c in strings(props.get("compatible", b""))):
                entry = {"path": path, **reg_region(path, props, nodes),
                         "interrupts_extended": cells(props["interrupts-extended"])}
                if kind == "plic":
                    entry.update(ndev=number(props["riscv,ndev"]),
                                 max_priority=number(props["riscv,max-priority"]))
                controllers[kind].append(entry)
    cpus.sort(key=lambda cpu: cpu["hart_id"])
    hart_ids = [cpu["hart_id"] for cpu in cpus]
    require(hart_ids == list(range(harts)), f"Unexpected hart IDs {hart_ids}")
    timer_hz = number(nodes["/cpus"]["timebase-frequency"])
    require(timer_hz == 500000, f"Expected 500 kHz RTC, got {timer_hz}")
    require(len(memories) == 1 and (memories[0]["base"], memories[0]["size"]) ==
            (0x80000000, 0x10000000), f"Unexpected DRAM regions {memories}")
    phandle_hart = {number(nodes[cpu["path"] + "/interrupt-controller"]["phandle"]):
                   cpu["hart_id"] for cpu in cpus}
    for kind, region, irqs in (("clint", (0x2000000, 0x10000), (3, 7)),
                               ("plic", (0xc000000, 0x4000000), (11, 9))):
        require(len(controllers[kind]) == 1, f"Expected one {kind}")
        device = controllers[kind][0]
        require((device["base"], device["size"]) == region, f"Unexpected {kind} mapping")
        pairs = device["interrupts_extended"]
        require(len(pairs) == 4 * harts, f"Unexpected {kind} interrupt count")
        contexts = [{"hart_id": phandle_hart.get(pairs[i]), "irq": pairs[i + 1]}
                    for i in range(0, len(pairs), 2)]
        require(contexts == [{"hart_id": h, "irq": irq} for h in hart_ids for irq in irqs],
                f"Unexpected {kind} contexts {contexts}")
        device["contexts"] = contexts
    plic = controllers["plic"][0]
    require((plic["ndev"], plic["max_priority"]) == (1, 1), "Unexpected PLIC sources/priority")
    bus_clocks = {path: number(props["clock-frequency"]) for path, props in nodes.items()
                  if props.get("compatible") == b"fixed-clock\0" and "clock-frequency" in props}
    require(bus_clocks and set(bus_clocks.values()) == {500000000},
            f"Unexpected fixed bus clocks {bus_clocks}")
    require(any("ucb,htif0" in strings(p.get("compatible", b"")) for p in nodes.values()),
            "Generated hardware lacks HTIF compatibility declaration")
    return {"harts": harts, "hart_ids": hart_ids, "timer_hz": timer_hz,
            "core_hz": 500000000, "memory_base": memories[0]["base"],
            "memory_size": memories[0]["size"], "isa": cpus[0]["isa"], "cpus": cpus,
            "clint": controllers["clint"][0], "plic": plic, "bus_clocks": bus_clocks,
            "dts": str(dts.resolve()), "dts_sha256": sha256(dts)}


def inspect_clocks_and_bridges(summary, annotations):
    text = summary.read_text()
    reference = re.search(r"Input Reference Frequency:\s*([\d.]+) MHz", text)
    outputs = re.findall(r"Output clock (.*?), requested:\s*([\d.]+) MHz, actual:\s*([\d.]+) MHz \(division of (\d+)\)", text)
    require(reference and float(reference[1]) == 500, "Clock bridge reference must be 500 MHz")
    require(outputs and all(float(req) == float(actual) == 500 and int(div) == 1
                            for _, req, actual, div in outputs), "Clock bridge sinks must all run at 500 MHz")
    require(any(name == "harnessbinder_clock" for name, *_ in outputs), "Missing harness clock evidence")
    bridges = [a for a in json.loads(annotations.read_text())
               if a.get("class") == "firesim.lib.bridgeutils.BridgeAnnotation"]
    by_kind = {}
    for bridge in bridges:
        by_kind.setdefault(bridge["widgetClass"].rsplit(".", 1)[-1], []).append(bridge)
    expected = {"PeekPokeBridgeModule", "ResetPulseBridgeModule", "UARTBridgeModule",
                "FASEDMemoryTimingModel", "TSIBridgeModule", "ClockBridgeModule"}
    require(set(by_kind) == expected and all(len(v) == 1 for v in by_kind.values()),
            f"Unexpected bridge set/counts {[(k, len(v)) for k, v in by_kind.items()]}")
    keys = {kind: value[0]["widgetConstructorKey"] for kind, value in by_kind.items()}
    require(keys["TSIBridgeModule"]["memoryRegionNameOpt"] == "MainMemory_0" and
            keys["FASEDMemoryTimingModel"]["memoryRegionName"] == "MainMemory_0",
            "TSI and FASED must share MainMemory_0 for fast ELF loading")
    clocks = keys["ClockBridgeModule"]["clocks"]
    require(len(clocks) == 1 and clocks[0]["multiplier"] == clocks[0]["divisor"] == 1,
            f"Expected a single 1:1 rational target clock, got {clocks}")
    return {"summary": evidence(summary), "annotations": evidence(annotations),
            "reference_hz": 500000000, "sinks": [o[0] for o in outputs],
            "rational_clocks": clocks, "bridges": keys}


def validate_host_interface(generated_header, settings=None):
    settings = dict(HOST_INTERFACE if settings is None else settings)
    require(settings.keys() == HOST_INTERFACE.keys(), "Unexpected host interface settings")
    for name, value in settings.items():
        require(type(value) is int and 0 < value <= (1 << 31) - 1,
                f"{name}: host interface setting must be a positive signed 32-bit integer")
    text = generated_header.read_text()
    matches = list(re.finditer(r"new reset_pulse_t\(.*?\bargs\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\);", text, re.S))
    require(len(matches) == 1, "Missing or ambiguous generated reset-pulse constructor")
    maximum, default = map(int, matches[0].groups())
    require(0 < default <= maximum, "Invalid generated reset-pulse lengths")
    startup_wait = settings["fesvr_step_size_cycles"] * settings["wait_ticks"]
    require(startup_wait > maximum, "FESVR startup wait must exceed maximum generated reset-pulse length")
    return {**settings, "generated_header": evidence(generated_header),
            "reset_default_cycles": default, "reset_max_cycles": maximum,
            "startup_wait_cycles": startup_wait,
            "reset_constructor_line": text.count("\n", 0, matches[0].start()) + 1}


def validate_memory_runtime(rtl_path, elaboration_log, requested=None):
    """Check runtime writes against compiled widths and elaborated capacities.

    FASED prints cfg.maxReads/maxWrites at elaboration; these are also the
    RuntimeSetting upper bounds in SplitTransactionMMRegIO. A port's unsigned
    range alone is insufficient: a 4-bit port can represent 15 but these models
    have queues for only 10 requests. Do not infer capacity from reset values.
    """
    requested = dict(MEMORY_RUNTIME if requested is None else requested)
    require(requested.keys() == MEMORY_RUNTIME.keys(), "Unexpected memory runtime register set")
    rtl = rtl_path.read_text()
    log = elaboration_log.read_text()
    registers = {}
    for name, value in requested.items():
        require(type(value) is int and value > 0, f"{name}: runtime value must be a positive integer")
        ports = list(re.finditer(rf"^[ \t]*input\s+\[(\d+):0\]\s+tNasti_io_mmReg_{name}\b[^\n]*", rtl, re.M))
        defaults = list(re.finditer(rf"^[ \t]*{name}\s*<=\s*32'h([0-9a-fA-F]+);[^\n]*", rtl, re.M))
        assignments = list(re.finditer(rf"^[ \t]*assign model_tNasti_io_mmReg_{name}\s*=\s*{name}(\[\d+:0\])?;[^\n]*", rtl, re.M))
        require(len(ports) == len(defaults) == len(assignments) == 1,
                f"{name}: missing or ambiguous generated port/default/assignment evidence")
        width = int(ports[0][1]) + 1
        require(1 <= width <= 32, f"{name}: unsupported generated port width {width}")
        expected_slice = f"[{width - 1}:0]" if width < 32 else None
        require(assignments[0][1] == expected_slice, f"{name}: generated assignment/port width mismatch")
        hardware_default = int(defaults[0][1], 16)
        bit_max = (1 << width) - 1
        require(0 < hardware_default <= bit_max, f"{name}: invalid hardware default")
        require(value <= bit_max, f"{name}: runtime value {value} overflows {width}-bit model port")
        supported_max = bit_max
        capacity_lines = []
        if name.endswith("MaxReqs"):
            label = "Read" if name.startswith("read") else "Write"
            matches = list(re.finditer(rf"^.*Max {label} Requests:\s*(\d+)\s*$", log, re.M))
            capacities = {int(match[1]) for match in matches}
            require(len(capacities) == 1, f"{name}: missing or ambiguous elaborated request capacity")
            supported_max = capacities.pop()
            require(0 < supported_max <= bit_max, f"{name}: invalid elaborated request capacity")
            require(hardware_default == supported_max,
                    f"{name}: hardware default differs from elaborated cfg capacity")
            capacity_lines = [{"line": log.count("\n", 0, match.start()) + 1,
                               "text": match[0].strip()} for match in matches]
        require(value <= supported_max,
                f"{name}: runtime value {value} exceeds compiled capacity {supported_max}")
        registers[name] = {"requested_value": value, "model_width_bits": width,
                           "hardware_default": hardware_default, "supported_max": supported_max,
                           "rtl_lines": [{"line": rtl.count("\n", 0, match.start()) + 1,
                                          "text": match[0].strip()}
                                         for match in (ports[0], assignments[0], defaults[0])],
                           "capacity_log_lines": capacity_lines}
    return {"rtl": evidence(rtl_path), "elaboration_log": evidence(elaboration_log),
            "registers": registers,
            "capacity_basis": "FASED elaboration prints cfg.maxReads/cfg.maxWrites; SplitTransactionMMRegIO uses those same values as RuntimeSetting maximums"}


def parse_final_timing(path):
    require(path.name == "final_timing_summary.rpt", "Only final_timing_summary.rpt is accepted")
    text = path.read_text()
    require(re.search(r"Command\s*:\s*report_timing_summary\b.*final_timing_summary\.rpt", text),
            "Missing final timing-report command provenance")
    require("post_synth" not in text.split("Timing Summary Report", 1)[0], "Synthesis timing is not final timing")
    require("Design Timing Summary" in text and "Clock Summary" in text, "Missing design timing summary")
    table = text.split("Design Timing Summary", 1)[1].split("Clock Summary", 1)[0]
    headings = re.search(r"WNS\(ns\).*TPWS Total Endpoints", table)
    require(headings is not None and all(x in headings[0] for x in
            ("TNS(ns)", "WHS(ns)", "THS(ns)", "WPWS(ns)", "TPWS(ns)")),
            "Timing report lacks setup/hold/pulse-width metrics")
    numeric = []
    for line in table[headings.end():].splitlines():
        values = line.split()
        if len(values) == 12:
            try:
                numeric.append([float(value) for value in values])
            except ValueError:
                continue
    require(len(numeric) == 1 and all(math.isfinite(v) for v in numeric[0]),
            "Expected exactly one finite design timing row")
    values = numeric[0]
    names = ("wns_ns", "tns_ns", "setup_failing_endpoints", "setup_total_endpoints",
             "whs_ns", "ths_ns", "hold_failing_endpoints", "hold_total_endpoints",
             "wpws_ns", "tpws_ns", "pulse_failing_endpoints", "pulse_total_endpoints")
    metrics = dict(zip(names, values))
    for index in (0, 4, 8):
        require(values[index] >= 0, f"Negative {names[index]}: {values[index]}")
        require(values[index + 1] == 0 and values[index + 2] == 0,
                f"Timing violations in {names[index]} domain")
        require(values[index + 3] > 0 and values[index + 3].is_integer(),
                f"No valid endpoint coverage for {names[index]}")
    require("All user specified timing constraints are met." in table,
            "Final timing report does not affirm timing closure")
    for check in ("no_clock", "unconstrained_internal_endpoints", "loops", "latch_loops"):
        counts = re.findall(rf"checking {check} \((\d+)\)", text)
        require(counts and all(int(count) == 0 for count in counts),
                f"Timing check {check} missing or nonzero")
    clock_table = text.split("Clock Summary", 1)[1].split("Intra Clock Table", 1)[0]
    host_clock = re.findall(r"^\s*host_clock\s+\{[^}]+\}\s+([\d.]+)\s+([\d.]+)", clock_table, re.M)
    require(len(host_clock) == 1 and abs(float(host_clock[0][1]) - 30.0) < .01,
            "Routed host clock does not match the 30 MHz physical request")
    return {"report": evidence(path), "metrics": metrics,
            "physical_clock": {"requested_mhz": 30, "reported_period_ns": float(host_clock[0][0]),
                               "reported_mhz": float(host_clock[0][1])}}


def parse_postroute_bus_skew(path):
    require(path.name == "overall_fpga_top_bus_skew_postroute_physopted.rpt",
            "Expected final post-route physical-optimization bus-skew report")
    text = path.read_text()
    require(re.search(r"Command\s*:\s*report_bus_skew\b.*overall_fpga_top_bus_skew_postroute_physopted\.rpt", text),
            "Missing bus-skew report command provenance")
    marker = "1. Bus Skew Report Summary\n--------------------------\n"
    require(text.count(marker) == 1, "Missing or ambiguous bus-skew summary")
    summary = text.split(marker, 1)[1].split("2. Bus Skew Report Per Constraint", 1)[0]
    ids = [int(x) for x in re.findall(r"^\s*(\d+)\s+\d+\s+\[get_cells", summary, re.M)]
    rows = re.findall(r"^\s*(Fast|Slow)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s*$", summary, re.M)
    require(ids and ids == list(range(1, len(ids) + 1)) and len(rows) == len(ids),
            "Incomplete bus-skew constraint coverage")
    slacks = [float(row[3]) for row in rows]
    require(all(math.isfinite(x) and x >= 0 for x in slacks), "Bus-skew timing violation")
    return {"report": evidence(path), "constraints": len(rows),
            "minimum_slack_ns": min(slacks), "violations": 0}


def parse_final_route(path):
    require(path.name == "final_route_status.rpt", "Only final_route_status.rpt is accepted")
    text = path.read_text()
    require("Design Route Status" in text, "Missing route status header")
    counts = {}
    for label, value in re.findall(r"# of (.+?)\.*\s*:\s*(\d+)\s*:", text):
        key = label.rstrip(". ").strip()
        require(key not in counts, f"Duplicate routing metric {key}")
        counts[key] = int(value)
    for key in ("logical nets", "routable nets", "fully routed nets", "nets with routing errors"):
        require(key in counts, f"Missing route metric {key}")
    require(counts["routable nets"] > 0 and counts["fully routed nets"] == counts["routable nets"],
            "Design routing is incomplete")
    require(counts["logical nets"] >= counts["routable nets"], "Inconsistent route counts")
    for key, count in counts.items():
        if any(part in key for part in ("routing errors", "unrouted", "partially routed")):
            require(count == 0, f"Route failure: {key}={count}")
    return {"report": evidence(path), "counts": counts}


def inspect_package(package, quintuplet):
    project = package.parent
    require(project.name == "cl_" + quintuplet, "Artifact directory does not match target quintuplet")
    files = {name: checked_file(project / "vivado_proj" / name) for name in ("firesim.bit", "firesim.mcs")}
    expected = {name: sha256(path) for name, path in files.items()}
    with tarfile.open(package, "r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}
        require(len(members) == len(archive.getmembers()), "Duplicate archive members")
        for name, digest in expected.items():
            member = members.get("xilinx_alveo_u250/" + name)
            require(member is not None and member.isfile() and member.size > 0,
                    f"Missing packaged {name}")
            value = hashlib.sha256()
            stream = archive.extractfile(member)
            for chunk in iter(lambda: stream.read(1048576), b""):
                value.update(chunk)
            require(value.hexdigest() == digest, f"Packaged {name} differs from reviewed implementation output")
        metadata = members.get("xilinx_alveo_u250/metadata")
        require(metadata is not None and metadata.isfile() and metadata.size < 65536, "Missing valid bitstream metadata")
        text = archive.extractfile(metadata).read().decode()
        tags = dict(item.split(":", 1) for item in text.strip().split(","))
        for key in ("firesim-buildquintuplet", "firesim-deployquintuplet"):
            require(tags.get(key) == quintuplet, f"Bitstream metadata mismatch: {key}")
    return {"bit": evidence(files["firesim.bit"]), "mcs": evidence(files["firesim.mcs"]), "tags": tags}


def checked_file(path):
    path = Path(path).resolve(strict=True)
    require(path.is_file() and path.stat().st_size > 0, f'Missing nonempty artifact: {path}')
    return path


def firmware_elf(artifact, harts):
    from run_firesim_matrix import validate_firmware
    build = json.loads((artifact / 'build_manifest.json').read_text())
    target = build['target']
    case = {'elf': str(artifact / 'zephyr.elf'), 'harts': harts,
            'placement': 'pinned' if target['placement'] == 'pinned' else 'unpinned',
            'platform_check': target['platform_check_only'],
            'modeled_clock_scale': target.get('modeled_clock_scale', 1),
            'ticks_per_sec': target['ticks_per_sec']}
    validate_firmware(case)
    elf = checked_file(artifact / 'zephyr.elf')
    segments = []
    with elf.open('rb') as stream:
        header = stream.read(64)
        require(header[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', header, 16) == (2, 243)
                and struct.unpack_from('<I', header, 48)[0] & 6 == 4, 'Expected executable RV64 lp64d ELF')
        entry, phoff = struct.unpack_from('<QQ', header, 24)
        phsize, phnum = struct.unpack_from('<HH', header, 54)
        require(entry == 0x80000000 and phsize == 56, 'Unexpected firmware entry/program headers')
        for i in range(phnum):
            stream.seek(phoff + i * phsize)
            kind, flags, offset, virt, phys, filesz, memsz, align = struct.unpack('<IIQQQQQQ', stream.read(56))
            if kind == 1:
                require(virt == phys and 0x80000000 <= phys <= phys + memsz <= 0x90000000 and filesz <= memsz,
                        'Firmware segment does not fit identity-mapped 256 MiB RAM')
                segments.append({'address': phys, 'filesz': filesz, 'memsz': memsz, 'flags': flags})
    require(segments, 'Firmware has no load segments')
    symbols = subprocess.check_output(['readelf', '-Ws', str(elf)], text=True)
    htif = {}
    for name in ['tohost', 'fromhost']:
        match = re.search(rf'^\s*\d+:\s*([0-9a-fA-F]+)\s+8\s+OBJECT\s+GLOBAL\s+DEFAULT\s+\d+\s+{name}$', symbols, re.M)
        require(match is not None, f'Missing HTIF symbol {name}')
        htif[name] = int(match[1], 16)
        require(any(s['address'] <= htif[name] and htif[name] + 8 <= s['address'] + s['memsz'] for s in segments),
                f'HTIF {name} is outside loaded memory')
    return {'artifact': str(artifact), 'elf': evidence(elf), 'entry': entry,
            'load_segments': segments, 'htif_symbols': htif,
            'build_manifest': evidence(artifact / 'build_manifest.json')}, case


def accelerator_header(root, cfg, kind, info):
    name = 'gemmini_params_illixr' + ('_int8' if kind == 'int8' else '') + '.h'
    header = checked_file(root / name)
    require(sha256(header) == info['generated_header_sha256'][kind],
            f'{kind} generated parameters differ from the tested firmware ABI')
    return {'harts': [0], 'precision': kind, 'dim': 16 if kind == 'int8' else 4,
            'scratch_kib': 256 if kind == 'int8' else 32,
            'accumulator_kib': 64 if kind == 'int8' else 8,
            'custom_opcode': 2 if kind == 'int8' else 3, 'header': evidence(header)}


def inspect_tiles(fir, cfg):
    tiles, current = {}, None
    tile_prefix = 'RocketTile' if cfg['core'] == 'rocket' else 'ShuttleTile'
    with fir.open() as stream:
        for line in stream:
            m = re.match(r'  module (\w+)\s*:', line)
            if m:
                current = m[1]
                if re.fullmatch(tile_prefix + r'(?:_\d+)?', current):
                    tiles[current] = []
            m = re.match(r'    inst (\w+) of (\w+)', line)
            if m and current in tiles:
                tiles[current].append({'instance': m[1], 'module': m[2]})
    require(len(tiles) == cfg['harts'], 'Generated FIRRTL tile count differs from configuration')
    for hart in range(cfg['harts']):
        instances = tiles[tile_prefix + ('_' + str(hart) if hart else '')]
        require(sum('Saturn' in i['module'] and 'Unit' in i['module'] for i in instances) == int(cfg['vector']),
                f'Hart {hart} Saturn instance count differs')
        require(sum(i['module'].startswith('Gemmini') for i in instances) ==
                ((int(cfg['fp32_gemmini']) + int(cfg['int8_gemmini'])) if hart == 0 else 0),
                f'Hart {hart} Gemmini instance count differs')
    return tiles


def check(args):
    root = args.chipyard.resolve()
    info = manifest()
    cfg = info['configurations'][args.config]
    target = cfg['target_config']
    quintuplet = 'xilinx_alveo_u250-firesim-FireSim-' + target + '-BaseXilinxAlveoU250Config'
    staging = args.staging_dir.resolve()
    prefix = 'firechip.chip.FireSim.' + target
    dts = checked_file(staging / (prefix + '.dts'))
    hardware = inspect_hardware(dts, cfg['harts'])
    nodes = read_dts(dts)
    for cpu in hardware['cpus']:
        compatible = strings(nodes[cpu['path']].get('compatible', b''))
        require(any(cfg['core'] in name.lower() for name in compatible), 'Generated CPU kind differs')
        cpu['compatible'] = compatible
        if cfg['vector']:
            require('v' in cpu['isa'].split('_')[0][4:] and 'zvl256b' in cpu['isa'] and 'zve64d' in cpu['isa'],
                    'Every vector hart must support VLEN256 and FP64')
    hardware['clock_bridge'] = inspect_clocks_and_bridges(
        checked_file(staging / (prefix + '.firesimRationalClockBridge.freq-summary')),
        checked_file(staging / (prefix + '.anno.json')))
    fir = checked_file(staging / (prefix + '.fir'))
    hardware['tile_instances'] = inspect_tiles(fir, cfg)
    hardware['firrtl'] = evidence(fir)
    if cfg.get('hpm'):
        from hpm_hardware import inspect as inspect_hpm
        hardware['hpm'] = inspect_hpm(fir, cfg['harts'], root / 'generators/rocket-chip/src/main/scala/rocket/RocketCore.scala')
    if cfg['vector']:
        hardware.update(vlen=256, elen=64, datapath_bits=128)
    if cfg['fp32_gemmini']:
        hardware['gemmini'] = accelerator_header(root, cfg, 'fp32', info)
    if cfg['int8_gemmini']:
        hardware['ritnet'] = accelerator_header(root, cfg, 'int8', info)
    bootrom = checked_file(root / 'generators/testchipip/src/main/resources/testchipip/bootrom/bootrom.rv64.img')
    require(sha256(bootrom) == info['bootrom_sha256'], 'Boot ROM differs from tested platform')
    hardware['bootrom'] = evidence(bootrom)
    rtl = checked_file(args.rtl)
    hardware['generated_rtl'] = evidence(rtl)
    memory = validate_memory_runtime(rtl, checked_file(args.elaboration_log))
    host = validate_host_interface(checked_file(rtl.with_name('FireSim-generated.const.h')))
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    runtime = output.parent / ('illixr-' + {1:'single',2:'dual',4:'quad'}[cfg['harts']] + '-runtime.conf')
    same_or_new(runtime, (DATA / 'runtime.conf').read_bytes())
    hardware.update(config=args.config, schema_version=1, hardware_verified=True, timing_closed=False,
                    runtime_conf=str(runtime), runtime_conf_sha256=sha256(DATA / 'runtime.conf'),
                    host_interface=host,
                    memory_timing={'model':'FASED latency-bandwidth pipe','read_latency_cycles':30,
                                   'write_latency_cycles':30,'max_reads':10,'max_writes':10,
                                   'rtl_runtime_validation':memory})
    if args.elaboration_only:
        hardware.update(inspector=evidence(Path(__file__)), package_manifest=evidence(DATA / 'manifest.json'),
                        verification_scope='Elaboration/header/clock/memory compatibility only; no timing, driver, bitstream or FPGA acceptance')
        require(not output.exists(), 'Preserve existing elaboration evidence; select a new --output path')
        runtime.write_bytes((DATA / 'runtime.conf').read_bytes())
        output.write_text(json.dumps(hardware, indent=2) + '\n')
        print(output)
        return
    require(args.build_output and args.bus_skew_report and args.firmware,
            'Full verification requires --build-output, --bus-skew-report, and --firmware; use --elaboration-only before synthesis')
    package = checked_file(args.build_output / 'firesim.tar.gz')
    packaged = inspect_package(package, quintuplet)
    reports = args.build_output / 'vivado_proj/reports'
    timing = parse_final_timing(checked_file(reports / 'final_timing_summary.rpt'))
    routing = parse_final_route(checked_file(reports / 'final_route_status.rpt'))
    skew = parse_postroute_bus_skew(checked_file(args.bus_skew_report))
    driver = checked_file(args.build_output / 'driver/FireSim-xilinx_alveo_u250')
    with driver.open('rb') as stream:
        elf = stream.read(64)
    require(elf[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', elf, 18)[0] == 62 and os.access(driver, os.X_OK),
            'Expected executable x86-64 FireSim driver')
    hardware.update(config=args.config, schema_version=1, hardware_verified=True, timing_closed=True,
                    bitstream=str(package), bitstream_sha256=sha256(package), driver=str(driver), driver_sha256=sha256(driver),
                    deploy_quintuplet=quintuplet, packaged_hardware=packaged,
                    routed_timing=timing, routed_status=routing, bus_skew=skew, host_interface=host,
                    runtime_conf=str(runtime), runtime_conf_sha256=sha256(DATA / 'runtime.conf'),
                    memory_timing={'model':'FASED latency-bandwidth pipe','read_latency_cycles':30,'write_latency_cycles':30,
                                   'max_reads':10,'max_writes':10,'rtl_runtime_validation':memory},
                    inspector=evidence(Path(__file__)), package_manifest=evidence(DATA / 'manifest.json'),
                    reviewed_at=datetime.now(timezone.utc).isoformat(),
                    verification_scope='Generated hardware, firmware ABI/RAM, routed timing, driver and bitstream packaging; FPGA boot and workload acceptance remain required')
    hardware['firmware'] = []
    from run_firesim_matrix import validate_firmware
    for artifact in args.firmware:
        record, case = firmware_elf(artifact.resolve(), cfg['harts'])
        validate_firmware(case, hardware)
        hardware['firmware'].append(record)
    if output.exists():
        previous = json.loads(output.read_text())
        for key in ['bitstream_sha256', 'driver_sha256', 'dts_sha256']:
            require(previous.get(key) == hardware[key], 'Refusing to overwrite a manifest for different hardware')
        if all(previous.get(k) == hardware[k] for k in ['bitstream_sha256','driver_sha256','runtime_conf_sha256']):
            for key in ['driver_tar','driver_tar_sha256','runtime_conf_bundle_name']:
                if key in previous:
                    hardware[key] = previous[key]
    runtime.write_bytes((DATA / 'runtime.conf').read_bytes())
    output.write_text(json.dumps(hardware, indent=2) + '\n')
    print(output)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--chipyard', type=Path, required=True)
    p.add_argument('--config', choices=list(manifest()['configurations']), required=True)
    p.add_argument('--staging-dir', type=Path, required=True)
    p.add_argument('--rtl', type=Path, required=True)
    p.add_argument('--elaboration-log', type=Path, required=True)
    p.add_argument('--elaboration-only', action='store_true')
    p.add_argument('--build-output', type=Path, help='cl_<quintuplet> directory containing firesim.tar.gz, driver/, vivado_proj/')
    p.add_argument('--bus-skew-report', type=Path)
    p.add_argument('--firmware', type=Path, action='append', help='Recorded firmware artifact directory; repeatable')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    try:
        check(args)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        p.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
