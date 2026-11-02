"""Check offline first-person arm OBJs against the visible camera near plane.

Generate poses with the viewmodel tool's VM_AIM / VM_SHOT / VM_RELOAD, then run:
  python3 tools/viewmodel/check_clearance.py <output directory> [...]

The game uses a 0.15m near plane and pushes viewmodel coordinates out4x.
This checks ViewmodelFov30/62/110 at16:9; a nonzero exit identifies a visible
arm triangle crossing that plane. It verifies geometry, not game animation.
"""

import math
from pathlib import Path
import sys


def segment_in_rectangle(a, b, half_width, half_height):
    low, high = 0.0, 1.0
    for axis, limit in [(0, half_width), (1, half_height)]:
        delta = b[axis] - a[axis]
        if abs(delta) < 1e-12:
            if abs(a[axis]) > limit:
                return False
        else:
            first = (-limit - a[axis]) / delta
            second = (limit - a[axis]) / delta
            low = max(low, min(first, second))
            high = min(high, max(first, second))
    return low <= high


def visible_crossings(path, fov):
    vertices, triangles = [], []
    for line in path.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields and fields[0] == "f":
            triangles.append([int(value.split("/")[0]) - 1 for value in fields[1:]])
    near = 0.15 / 4.0
    half_height = near * math.tan(math.radians(fov / 2.0))
    half_width = half_height * 16.0 / 9.0
    hits = 0
    for triangle in triangles:
        crossings = []
        for start, end in zip(triangle, triangle[1:] + triangle[:1]):
            a, b = vertices[start], vertices[end]
            if (a[1] - near) * (b[1] - near) < 0:
                t = (near - a[1]) / (b[1] - a[1])
                crossings.append((a[0] + t * (b[0] - a[0]), a[2] + t * (b[2] - a[2])))
        if len(crossings) == 2 and segment_in_rectangle(*crossings, half_width, half_height):
            hits += 1
    return hits


def main(directories):
    failures = 0
    for directory in directories:
        folder = Path(directory)
        gun = (folder / "gun.txt").read_text().strip()
        arms = [path for path in sorted(folder.glob("*_fp.obj")) if path.name != gun + "_fp.obj"]
        if not arms:
            raise SystemExit(f"no right-arm meshes in {folder}")
        blocked = []
        for arm in arms:
            hits = max(visible_crossings(arm, fov) for fov in (30, 62, 110))
            if hits:
                blocked.append((arm.name, hits))
        print(f"{folder}: {len(arms)} outfits; visible near-plane crossings: {blocked}")
        failures += len(blocked)
    return bool(failures)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: check_clearance.py <viewmodel output directory> [...]")
    raise SystemExit(main(sys.argv[1:]))
