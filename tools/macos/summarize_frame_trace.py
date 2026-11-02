#!/usr/bin/env python3
"""Summarize bounded frame diagnostics without interpreting guest game data."""

import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(path, details=False):
    header = {}
    footer = {}
    draws = {}
    resources = {}
    record_counts = Counter()
    with path.open() as stream:
        for line in stream:
            parts = line.split()
            if not parts:
                continue
            kind = parts[0]
            fields = dict(part.split("=", 1) for part in parts[1:] if "=" in part)
            record_counts[kind] += 1
            if kind == "frame_trace":
                header = fields
            elif kind == "end_frame":
                footer = fields
            elif kind == "draw":
                draws[fields["id"]] = {"draw": fields, "images": []}
            elif kind == "image_resource":
                resources[fields["id"]] = fields
            elif kind in ("state", "image_ref") and fields.get("draw") in draws:
                draw = draws[fields["draw"]]
                if kind == "state":
                    draw["state"] = fields
                else:
                    resource = resources.get(fields["resource"])
                    if resource is None:
                        raise ValueError("Image reference has no resource definition")
                    fields = {**resource, **fields}
                    flags = int(fields["flags"], 0)
                    fields["unbacked"] = bool(flags & (1 << 18))
                    fields["decoding"] = bool(flags & (1 << 17))
                    fields["zero_filled"] = bool(flags & (1 << 20))
                    draw["images"].append(fields)
    if header.get("version") != "1":
        raise ValueError("Missing or unsupported frame trace header")
    result = {
        "file": str(path),
        "frame": header.get("frame"),
        "complete": footer.get("complete") == "true",
        "footer_present": bool(footer),
        "records": dict(record_counts),
        "draw_status": dict(Counter(d["draw"]["status"] for d in draws.values())),
        "unbacked_images": sorted({i["image_address"] for d in draws.values()
                                    for i in d["images"] if i["unbacked"]}),
        "decoding_images": sorted({i["image_address"] for d in draws.values()
                                    for i in d["images"] if i["decoding"]}),
    }
    if details:
        result["draws"] = list(draws.values())
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--details", action="store_true")
    args = parser.parse_args()
    print(json.dumps(summarize(args.trace, args.details), indent=2))
