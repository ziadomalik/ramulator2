#!/usr/bin/env python3
"""
Converts an .mtrc file to a CSV file according to the format defined in:
src/dram_controller/impl/plugin/mtrc/mtrc.h

CSV Format:
CLK, CMD, CMD_LATENCY, REQ_ARRIVE, REQ_DEPART, REQ_ISSUE_DURATION,
REQ_SOURCE_ID, REQ_TYPE_ID, CHANNEL, RANK, BANKGROUP, BANK, ROW, COLUMN

NOTE: THIS FILE IS VIBE-CODED BY AI, BE CAREFUL WITH IT.
"""

import struct
import sys
import csv

HEADER_SIZE = 40
ENTRY_SIZE = 64
MAGIC = b"RAM2\x00"  # 5 bytes including null terminator


def read_header(f):
    """Read the MTRC header (24 bytes)."""
    data = f.read(HEADER_SIZE)
    if len(data) < HEADER_SIZE:
        raise ValueError("File too small to contain header")

    # Layout:
    # 5s magic, B version, B num_commands, B reserved, Q num_entries, Q dict_offset,
    # i ncl, i ncwl, h num_channels, h num_ranks, h num_bankgroups, h num_banks
    (
        magic,
        version,
        num_commands,
        reserved,
        num_entries,
        dict_offset,
        ncl,
        ncwl,
        num_channels,
        num_ranks,
        num_bankgroups,
        num_banks,
    ) = struct.unpack("<5sBBBQQiihhhh", data)

    if magic != MAGIC:
        raise ValueError(f"Invalid magic: {magic!r}, expected {MAGIC!r}")

    return {
        "magic": magic,
        "version": version,
        "num_commands": num_commands,
        "num_entries": num_entries,
        "dict_offset": dict_offset,
        "ncl": ncl,
        "ncwl": ncwl,
        "num_channels": num_channels,
        "num_ranks": num_ranks,
        "num_bankgroups": num_bankgroups,
        "num_banks": num_banks,
    }


def read_dictionary(f, header):
    """Read the command dictionary from the end of the file."""
    f.seek(header["dict_offset"])
    commands = []

    for _ in range(header["num_commands"]):
        length_byte = f.read(1)
        if not length_byte:
            break
        length = struct.unpack("B", length_byte)[0]
        cmd_string = f.read(length).decode("utf-8")
        latency_bytes = f.read(4)
        if len(latency_bytes) < 4:
            break
        cmd_latency = struct.unpack("<i", latency_bytes)[0]
        commands.append({"name": cmd_string, "latency": cmd_latency})

    return commands


def read_entries(f, header):
    """Read all trace entries (64 bytes each)."""
    f.seek(HEADER_SIZE)  # Start right after header
    entries = []

    for _ in range(header["num_entries"]):
        data = f.read(ENTRY_SIZE)
        if len(data) < ENTRY_SIZE:
            break

        # Layout:
        # q clk, q req_arrive, q req_depart, h channel, h rank, i bankgroup, i bank, i row, i column,
        # i req_source_id, i req_type_id, i req_issue_duration, B cmd_id, 7x reserved
        (
            clk,
            req_arrive,
            req_depart,
            channel,
            rank,
            bankgroup,
            bank,
            row,
            column,
            req_source_id,
            req_type_id,
            req_issue_duration,
            cmd_id,
        ) = struct.unpack("<qqqhhiiiiiiiB7x", data)
        addr_vec = [channel, rank, bankgroup, bank, row, column]

        entries.append(
            {
                "clk": clk,
                "addr_vec": addr_vec,
                "cmd_id": cmd_id,
                "req_arrive": req_arrive,
                "req_depart": req_depart,
                "req_source_id": req_source_id,
                "req_type_id": req_type_id,
                "req_issue_duration": req_issue_duration,
            }
        )

    return entries


def mtrc_to_csv(input_path, output_path=None):
    """Convert an MTRC file to CSV."""
    if output_path is None:
        output_path = input_path.replace(".mtrc", ".csv")

    with open(input_path, "rb") as f:
        header = read_header(f)
        print(f"MTRC Version: {header['version']}")
        print(f"Number of commands: {header['num_commands']}")
        print(f"Number of records: {header['num_entries']}")
        print(f"Dictionary offset: {header['dict_offset']}")
        print(f"nCL: {header['ncl']}, nCWL: {header['ncwl']}")
        print(
            "Layout: channels={}, ranks={}, bankgroups={}, banks={}".format(
                header["num_channels"],
                header["num_ranks"],
                header["num_bankgroups"],
                header["num_banks"],
            )
        )

        commands = read_dictionary(f, header)
        print(f"Commands: {commands}")

        entries = read_entries(f, header)

    # Write CSV
    with open(output_path, "w", newline="") as f:
        for entry in entries:
            if entry["cmd_id"] < len(commands):
                cmd = commands[entry["cmd_id"]]
                cmd_string = cmd["name"]
                cmd_latency = cmd["latency"]
            else:
                cmd_string = f"CMD_{entry['cmd_id']}"
                cmd_latency = -1

            row = [
                entry["clk"],
                cmd_string,
                cmd_latency,
                entry["req_arrive"],
                entry["req_depart"],
                entry["req_issue_duration"],
                entry["req_source_id"],
                entry["req_type_id"],
            ] + list(entry["addr_vec"])
            f.write(", ".join(str(x) for x in row) + "\n")

    print(f"Written {len(entries)} entries to {output_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input.mtrc> [output.csv]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    mtrc_to_csv(input_path, output_path)
