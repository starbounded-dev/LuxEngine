#!/usr/bin/env python3
"""Writes SharedMeshNodes.gltf: one cube mesh placed by three nodes (glTF instancing).

Node 0 is translated, node 2 is the child of a rotated parent, node 3 is mirrored (negative scale).
Importing it must give three placements, not one: drag the file into the Content Browser and
add it to a scene; three cubes must appear (and collide), not one.
"""
import base64
import json
import struct
from pathlib import Path

positions, normals, indices = [], [], []
faces = [((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)), ((0, 1, 0), (0, 0, 1)),
         ((0, -1, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0)), ((0, 0, -1), (1, 0, 0))]
for n, u in faces:
    v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
    base = len(positions)
    for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        positions.append(tuple(0.5 * (n[i] + su * u[i] + sv * v[i]) for i in range(3)))
        normals.append(n)
    indices += [base, base + 1, base + 2, base, base + 2, base + 3]

blob = b"".join(struct.pack("<3f", *p) for p in positions)
normal_offset = len(blob)
blob += b"".join(struct.pack("<3f", *n) for n in normals)
index_offset = len(blob)
blob += b"".join(struct.pack("<H", i) for i in indices)
while len(blob) % 4:
    blob += b"\0"

gltf = {
    "asset": {"version": "2.0", "generator": "LuxEngine tests/meshes/make_shared_node_fixture.py"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1, 3]}],
    "nodes": [
        {"name": "Left", "mesh": 0, "translation": [-3.0, 0.0, 0.0]},
        {"name": "RotatedParent", "rotation": [0.0, 0.38268343, 0.0, 0.92387953], "translation": [3.0, 0.0, 0.0], "children": [2]},
        {"name": "RotatedChild", "mesh": 0, "translation": [0.0, 1.0, 0.0]},
        {"name": "Mirrored", "mesh": 0, "translation": [0.0, 0.0, -3.0], "scale": [-1.0, 1.0, 1.0]},
    ],
    "meshes": [{"name": "Cube", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
    "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": normal_offset},
        {"buffer": 0, "byteOffset": normal_offset, "byteLength": index_offset - normal_offset},
        {"buffer": 0, "byteOffset": index_offset, "byteLength": len(indices) * 2},
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": [-0.5] * 3, "max": [0.5] * 3},
        {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
        {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
    ],
}
out = Path(__file__).resolve().parent / "SharedMeshNodes.gltf"
out.write_text(json.dumps(gltf, indent=1) + "\n", newline="\n")
print(out)
