#!/usr/bin/env python3
"""Inspect generated Chipyard hardware, rejecting incompatible Rocket platforms."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def cells(value):
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


def number(value):
    return int.from_bytes(value, "big")


def strings(value):
    return value.rstrip(b"\0").decode().split("\0")


def read_dts(path):
    """Use dtc for DTS syntax, then read the simple flattened-device-tree format."""
    blob = subprocess.check_output(["dtc", "-I", "dts", "-O", "dtb", str(path)],
                                   stderr=subprocess.PIPE)
    magic, _, pos, string_offset = struct.unpack_from(">4I", blob)
    if magic != 0xD00DFEED:
        raise ValueError("dtc returned invalid FDT")
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
    return {"base": number(props["reg"][:address_bytes]),
            "size": number(props["reg"][address_bytes:address_bytes + size_bytes])}


def core_clock(build_dir):
    rtl = build_dir / "gen-collateral/TestHarness.sv"
    text = rtl.read_text()
    signal = re.search(r"\.clock_uncore\s*\(([^)]+)\)", text).group(1).strip()
    for match in re.finditer(r"ClockSourceAtFreqMHz\s*#\(\s*\.PERIOD\(([^)]+)\)\s*\)\s*(\w+)\s*\((.*?)\n\s*\);", text, re.S):
        period, instance, connections = match.groups()
        clock = re.search(r"\.clk\s*\(([^)]+)\)", connections)
        if clock and clock.group(1).strip() == signal:
            return round(1e9 / float(period)), {"rtl": str(rtl.resolve()),
                    "rtl_sha256": sha256(rtl), "instance": instance,
                    "period_ns": float(period), "signal": signal}
    raise ValueError("Could not identify the RTL source driving clock_uncore")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--simulator", type=Path, required=True)
    parser.add_argument("--harts", type=int, choices=(1, 2, 4), required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--testdriver-period-ns", type=float, default=1.0)
    parser.add_argument("--simulator-threads", type=int, default=1)
    args = parser.parse_args()
    dts_files = list(args.build_dir.glob("*.dts"))
    if len(dts_files) != 1:
        raise ValueError(f"Expected one generated DTS, found {dts_files}")
    dts = dts_files[0].resolve()
    nodes = read_dts(dts)
    core_hz, clock_source = core_clock(args.build_dir)
    cpus = []
    memories, clints, plics = [], [], []
    for path, props in nodes.items():
        compatible = strings(props.get("compatible", b""))
        if props.get("status") == b"disabled\0":
            continue
        if props.get("device_type") == b"cpu\0":
            cpus.append({"path": path, "hart_id": number(props["reg"]),
                         "isa": strings(props["riscv,isa"])[0],
                         "dts_clock_hz": number(props["clock-frequency"]),
                         "clock_hz": core_hz})
        if props.get("device_type") == b"memory\0":
            memories.append({"path": path, **reg_region(path, props, nodes)})
        if any("clint" in name for name in compatible):
            clints.append({"path": path, **reg_region(path, props, nodes),
                           "reg": cells(props["reg"]),
                           "interrupts_extended": cells(props["interrupts-extended"])})
        if any("plic" in name for name in compatible):
            plics.append({"path": path, **reg_region(path, props, nodes),
                          "reg": cells(props["reg"]),
                          "interrupts_extended": cells(props["interrupts-extended"]),
                          "ndev": number(props["riscv,ndev"]),
                          "max_priority": number(props["riscv,max-priority"])})
    cpus.sort(key=lambda cpu: cpu["hart_id"])
    hart_ids = [cpu["hart_id"] for cpu in cpus]
    timer_hz = number(nodes["/cpus"]["timebase-frequency"])
    if hart_ids != list(range(args.harts)):
        raise ValueError(f"Unexpected hart IDs {hart_ids}")
    if timer_hz != 500_000:
        raise ValueError(f"Expected 500 kHz RTC, got {timer_hz}")
    if len(memories) != 1 or memories[0]["base"] != 0x80000000 or memories[0]["size"] != 256 * 1024**2:
        raise ValueError(f"Expected 256 MiB at 0x80000000, got {memories}")
    for cpu in cpus:
        extensions = cpu["isa"].split("_")[0].removeprefix("rv64")
        if not cpu["isa"].startswith("rv64") or not set("imafdc") <= set(extensions):
            raise ValueError(f"Firmware ISA requirements not met by {cpu}")
        if cpu["clock_hz"] != 500_000_000:
            raise ValueError(f"Expected 500 MHz CPU clock, got {cpu}")
    if len(clints) != 1 or len(plics) != 1:
        raise ValueError(f"Expected one CLINT and PLIC, got {clints}, {plics}")
    if (clints[0]["base"], clints[0]["size"]) != (0x02000000, 0x10000):
        raise ValueError(f"Unexpected CLINT mapping {clints[0]}")
    if (plics[0]["base"], plics[0]["size"], plics[0]["ndev"], plics[0]["max_priority"]) != (0x0C000000, 0x4000000, 1, 1):
        raise ValueError(f"Unexpected PLIC mapping {plics[0]}")
    phandle_hart = {number(nodes[cpu["path"] + "/interrupt-controller"]["phandle"]): cpu["hart_id"]
                   for cpu in cpus}
    for controller, expected_irqs in ((clints[0], (3, 7)), (plics[0], (11, 9))):
        pairs = controller["interrupts_extended"]
        contexts = [{"hart_id": phandle_hart[pairs[i]], "irq": pairs[i + 1]}
                    for i in range(0, len(pairs), 2)]
        expected = [{"hart_id": hart, "irq": irq} for hart in hart_ids for irq in expected_irqs]
        if contexts != expected:
            raise ValueError(f"Unexpected {controller['path']} interrupt contexts {contexts}")
        controller["contexts"] = contexts
    result = {
        "config": args.config, "harts": args.harts, "hart_ids": hart_ids,
        "timer_hz": timer_hz, "core_hz": core_hz, "core_clock_source": clock_source,
        "testdriver_hz": round(1e9 / args.testdriver_period_ns),
        "testdriver_period_ns": args.testdriver_period_ns,
        "simulator_threads": args.simulator_threads,
        "memory_base": memories[0]["base"], "memory_size": memories[0]["size"],
        "isa": cpus[0]["isa"], "cpus": cpus, "clint": clints[0], "plic": plics[0],
        "dts": str(dts), "dts_sha256": sha256(dts),
        "simulator": str(args.simulator.resolve()), "simulator_sha256": sha256(args.simulator),
        "build_provenance": str(args.provenance.resolve()),
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
