"""One-off generator for the demo tree meshes."""

import math
from pathlib import Path

OUT = Path(__file__).resolve().parent


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    if length < 1e-12:
        return (0.0, 1.0, 0.0)
    return tuple(c / length for c in v)


def add(a, b):
    return tuple(a[i] + b[i] for i in range(3))


def scale(v, s):
    return tuple(c * s for c in v)


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


class MeshBuilder:
    def __init__(self):
        self.vertices = []
        self.normals = []
        self.faces = []

    def add_vertex(self, position, normal):
        self.vertices.append(position)
        self.normals.append(normalize(normal))
        return len(self.vertices) - 1

    def add_cylinder(self, start, end, radius0, radius1, sides=8, stacks=4):
        axis = normalize(tuple(end[i] - start[i] for i in range(3)))
        helper = (0.0, 0.0, 1.0) if abs(axis[1]) > 0.9 else (0.0, 1.0, 0.0)
        tangent = normalize(cross(helper, axis))
        bitangent = cross(axis, tangent)
        base = len(self.vertices)
        for stack in range(stacks + 1):
            t = stack / stacks
            center = tuple(start[i] + (end[i] - start[i]) * t for i in range(3))
            radius = radius0 + (radius1 - radius0) * t
            for side in range(sides):
                angle = 2.0 * math.pi * side / sides
                normal = add(scale(tangent, math.cos(angle)), scale(bitangent, math.sin(angle)))
                self.add_vertex(add(center, scale(normal, radius)), normal)
        for stack in range(stacks):
            for side in range(sides):
                nxt = (side + 1) % sides
                i0 = base + stack * sides + side
                i1 = base + stack * sides + nxt
                i2 = base + (stack + 1) * sides + side
                i3 = base + (stack + 1) * sides + nxt
                self.faces.append((i0, i3, i1))
                self.faces.append((i0, i2, i3))

    def add_icosahedron(self, center, radius):
        phi = (1.0 + math.sqrt(5.0)) / 2.0
        raw = [
            (0, 1, phi), (0, -1, phi), (0, 1, -phi), (0, -1, -phi),
            (1, phi, 0), (-1, phi, 0), (1, -phi, 0), (-1, -phi, 0),
            (phi, 0, 1), (phi, 0, -1), (-phi, 0, 1), (-phi, 0, -1),
        ]
        verts = [normalize(v) for v in raw]
        faces = [
            (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
            (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
            (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
            (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
        ]
        base = len(self.vertices)
        for vertex in verts:
            self.add_vertex(add(center, scale(vertex, radius)), vertex)
        for face in faces:
            self.faces.append(tuple(base + index for index in face))

    def face_outward(self):
        fixed = []
        for i0, i1, i2 in self.faces:
            a = self.vertices[i0]
            b = self.vertices[i1]
            c = self.vertices[i2]
            normal = cross((b[0] - a[0], b[1] - a[1], b[2] - a[2]), (c[0] - a[0], c[1] - a[1], c[2] - a[2]))
            expected = add(self.normals[i0], add(self.normals[i1], self.normals[i2]))
            if normal[0] * expected[0] + normal[1] * expected[1] + normal[2] * expected[2] < 0.0:
                fixed.append((i0, i2, i1))
            else:
                fixed.append((i0, i1, i2))
        self.faces = fixed

    def write(self, path, comment):
        self.face_outward()
        lines = [f"# {comment}"]
        for vertex in self.vertices:
            lines.append("v {:.5f} {:.5f} {:.5f}".format(*vertex))
        for normal in self.normals:
            lines.append("vn {:.5f} {:.5f} {:.5f}".format(*normal))
        for face in self.faces:
            parts = " ".join(f"{index + 1}//{index + 1}" for index in face)
            lines.append("f " + parts)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"{path.name}: {len(self.faces)} triangles")


def fitted_size(builder):
    xs = [v[0] for v in builder.vertices]
    ys = [v[1] for v in builder.vertices]
    zs = [v[2] for v in builder.vertices]
    extent = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    scale = 1.5 / extent
    height = (max(ys) - min(ys)) * scale
    width = max(max(xs) - min(xs), max(zs) - min(zs)) * scale
    print(f"  fitted height {height:.3f}, width {width:.3f}")


trunk = MeshBuilder()
trunk.add_cylinder((0.0, 0.0, 0.0), (0.0, 1.2, 0.0), 0.1, 0.042, sides=8, stacks=6)
trunk.add_cylinder((0.0, 0.58, 0.0), (0.42, 0.96, 0.08), 0.036, 0.015, sides=6, stacks=3)
trunk.add_cylinder((0.0, 0.7, 0.0), (-0.42, 1.05, -0.08), 0.034, 0.014, sides=6, stacks=3)
trunk.add_cylinder((0.0, 0.84, 0.0), (0.08, 1.22, -0.4), 0.03, 0.013, sides=6, stacks=3)
trunk.add_cylinder((0.0, 0.78, 0.0), (-0.06, 1.16, 0.4), 0.028, 0.012, sides=6, stacks=3)
trunk.write(OUT / "tree-trunk.obj", "Low-poly tree trunk and branches")
fitted_size(trunk)

crown = MeshBuilder()
clumps = [
    ((0.0, 1.28, 0.0), 0.4),
    ((0.42, 1.05, 0.1), 0.32),
    ((-0.42, 1.12, -0.08), 0.32),
    ((0.08, 1.28, -0.4), 0.3),
    ((-0.06, 1.22, 0.4), 0.28),
    ((0.16, 1.48, 0.02), 0.26),
    ((-0.18, 0.98, 0.16), 0.22),
]
for center, radius in clumps:
    crown.add_icosahedron(center, radius)
crown.write(OUT / "tree-crown.obj", "Low-poly tree crown")
fitted_size(crown)
