"""Independent, read-only audit of native recovery against a compiled source level.

Usage: python verify_native.py <source-level-directory> <native-export-directory>
Uses only the Python standard library. Covers the supported DX9/tree profile.
"""
import argparse
import array
import csv
import math
import struct
from pathlib import Path


def chunks(blob):
    result = {}
    offset = 0
    while offset < len(blob):
        tag, size = struct.unpack_from('<II', blob, offset)
        offset += 8
        assert not tag & 0x80000000 and offset + size <= len(blob)
        assert tag not in result
        result[tag] = blob[offset:offset + size]
        offset += size
    assert offset == len(blob)
    return result


def string(blob, offset=0):
    end = blob.index(0, offset)
    return blob[offset:end].decode('utf-8', errors='surrogateescape'), end + 1


def records(path):
    with path.open(encoding='utf-8', errors='surrogateescape', newline='') as f:
        return list(csv.DictReader(f))


def u32(blob, offset=0):
    return struct.unpack_from('<I', blob, offset)[0]


def floats(blob):
    return list(struct.iter_unpack('<3f', blob))


def compose(placement):
    x, y, z, pitch, heading, bank, sx, sy, sz = placement
    ch, sh = math.cos(heading), math.sin(heading)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cb, sb = math.cos(bank), math.sin(bank)
    return ((ch*cb-sp*sh*sb)*sx, -cp*sb*sx, (sp*ch*sb+sh*cb)*sx, 0,
            (sp*sh*cb+ch*sb)*sy, cp*cb*sy, (sh*sb-sp*ch*cb)*sy, 0,
            -cp*sh*sz, sp*sz, cp*ch*sz, 0, x, y, z, 1)


def audit(source, output):
    source_chunks = chunks((source / 'level').read_bytes())
    source_visuals = {i: chunks(b) for i, b in chunks(source_chunks[3]).items()}
    shader_names = []
    offset = 4
    for _ in range(u32(source_chunks[2])):
        name, offset = string(source_chunks[2], offset)
        shader_names.append(name)
    assert offset == len(source_chunks[2])
    geom = chunks((source / 'level.geom').read_bytes())
    vb, offset = [], 4
    sizes = (4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8)
    for _ in range(u32(geom[9])):
        decl = {}
        stride = 0
        while True:
            stream, at, kind, method, usage, index = struct.unpack_from('<HHBBBB', geom[9], offset)
            offset += 8
            if stream == 255:
                break
            assert stream == 0 and method == 0
            decl[usage, index] = (at, kind)
            stride = max(stride, at + sizes[kind])
        count = u32(geom[9], offset)
        offset += 4
        vb.append((decl, stride, memoryview(geom[9])[offset:offset + count*stride]))
        offset += count*stride
    assert offset == len(geom[9])
    ib, offset = [], 4
    for _ in range(u32(geom[10])):
        count = u32(geom[10], offset)
        offset += 4
        ib.append(memoryview(geom[10])[offset:offset + count*2])
        offset += count*2
    assert offset == len(geom[10])
    swis = []
    if 11 in geom:
        offset = 4
        for _ in range(u32(geom[11])):
            count = u32(geom[11], offset + 16)
            offset += 20
            swis.append(struct.unpack_from('<IHH', geom[11], offset))
            offset += count*8
        assert offset == len(geom[11])

    instances = records(output / 'instances.csv')
    by_geometry = {}
    by_parent = {}
    covered = set()
    for row in instances:
        visual = int(row['visual'])
        assert visual not in covered
        covered.add(visual)
        by_geometry.setdefault(int(row['geometry_prototype']), visual)
        by_parent.setdefault(int(row['parent_visual']), []).append(row)
        expected = struct.unpack_from('<26f', source_visuals[visual][12])
        recorded = [float(row[f'matrix_{i}']) for i in range(16)] + [float(row[f'tree_lighting_{i}']) for i in range(10)]
        assert all(struct.pack('<f', a) == struct.pack('<f', b) for a, b in zip(expected, recorded))
    source_trees = {i for i, v in source_visuals.items() if v[1][1] in (7, 11)}
    assert covered == source_trees, 'Missing or duplicated source tree visual'

    cache = {}

    def geometry(prototype):
        if prototype in cache:
            return cache[prototype]
        visual = source_visuals[by_geometry[prototype]]
        vi, base, count, ii, first, indices = struct.unpack('<6I', visual[21])
        lod_offset = 0
        if visual[1][1] == 11:
            lod_offset, triangles, count = swis[u32(visual[20])]
            indices = triangles*3
        decl, stride, data = vb[vi]
        po, pt = decl[0, 0]
        no, nt = decl[3, 0]
        to, tt = decl[5, 0]
        assert pt == 2 and nt in (2, 4) and tt in (1, 6, 7)
        points, uv, normals = [], [], []
        for i in range(count):
            start = (base+i)*stride
            points.append(struct.unpack_from('<3f', data, start+po))
            if tt == 1:
                uv.append(struct.unpack_from('<2f', data, start+to))
            else:
                a, b = struct.unpack_from('<2h', data, start+to)
                uv.append((a/2048, b/2048))
            if nt == 2:
                normal = struct.unpack_from('<3f', data, start+no)
            else:
                normal = tuple(data[start+no+2-j]*2/255-1 for j in range(3))
            length = math.sqrt(sum(x*x for x in normal))
            normals.append(tuple(x/length for x in normal))
        ids = array.array('H')
        ids.frombytes(ib[ii][(first+lod_offset)*2:(first+lod_offset+indices)*2])
        cache[prototype] = points, uv, normals, ids
        return cache[prototype]

    parts = records(output / 'model_parts.csv')
    models = records(output / 'prototypes.csv')
    model_geometry = {}
    mesh_count = vertex_count = triangle_count = 0
    for row in models:
        model = int(row['model'])
        reference = row['object_reference']
        object_path = output / 'objects' / Path(*reference.split('\\'))
        body = chunks(chunks(object_path.with_suffix('.object').read_bytes())[0x7777])
        assert struct.unpack('<H', body[0x0900])[0] == 0x10 and u32(body[0x0903]) == 16
        surfaces = {}
        offset = 4
        for _ in range(u32(body[0x0907])):
            fields = []
            for _ in range(6):
                field, offset = string(body[0x0907], offset)
                fields.append(field)
            assert fields[1] == 'editor\\wire'
            assert fields[2] == fields[3] == '__recovery_unmapped__'
            assert fields[0] not in surfaces
            surfaces[fields[0]] = fields
            flags, fvf, channels = struct.unpack_from('<3I', body[0x0907], offset)
            offset += 12
            assert flags == 0 and fvf == 0x112 and channels == 1
        assert offset == len(body[0x0907])
        meshes = chunks(body[0x0910])
        assert len(meshes) == int(row['parts']) == len(surfaces)
        model_rows = sorted((p for p in parts if int(p['model']) == model), key=lambda p: int(p['mesh_index']))
        assert len(model_rows) == len(meshes)
        normal_file = object_path.with_suffix('.normals.bin').read_bytes()
        assert normal_file[:8] == b'XRNORM01'
        saved_normals = floats(normal_file[12:])
        assert len(saved_normals) == u32(normal_file, 8)
        normal_offset = nv = nf = 0
        model_geometry[model] = []
        for part in model_rows:
            mesh = chunks(meshes[int(part['mesh_index'])])
            expected_points, expected_uv, expected_normals, expected_indices = geometry(int(part['geometry_prototype']))
            points = floats(mesh[0x1005][4:])
            assert len(points) == u32(mesh[0x1005]) == len(expected_points)
            assert points == expected_points, 'Native model positions differ from the source VB'
            face_words = array.array('I')
            face_words.frombytes(mesh[0x1006][4:])
            assert face_words[::2] == expected_indices and face_words[1::2] == expected_indices
            assert u32(mesh[0x1006])*6 == len(face_words)
            refs = mesh[0x1008]
            assert u32(refs) == len(points) and len(refs) == 4+len(points)*9
            for i in range(len(points)):
                assert struct.unpack_from('<BII', refs, 4+i*9) == (1, 0, i)
            vmap = mesh[0x1012]
            assert u32(vmap) == 1
            _, start = string(vmap, 4)
            assert vmap[start:start+3] == bytes((2, 0, 0))
            count = u32(vmap, start+3)
            uv = list(struct.iter_unpack('<2f', vmap[start+7:start+7+count*8]))
            assert uv == expected_uv, 'Native UVs changed or V was flipped'
            assert len(vmap) == start+7+count*12
            vertex_ids = array.array('I')
            vertex_ids.frombytes(vmap[start+7+count*8:])
            assert list(vertex_ids) == list(range(count))
            sface = mesh[0x1009]
            assert struct.unpack_from('<H', sface)[0] == 1
            surface, after = string(sface, 2)
            assert surface in surfaces and u32(sface, after) == u32(mesh[0x1006])
            source_visual = source_visuals[by_geometry[int(part['geometry_prototype'])]]
            source_shader = shader_names[struct.unpack_from('<H', source_visual[1], 2)[0]]
            assert source_shader == part['source_shader_and_textures']
            texture = source_shader.partition('/')[2].partition(',')[0] or 'ed\\ed_dummy'
            assert surfaces[surface][4] == texture
            for i, (a, b) in enumerate(zip(saved_normals[normal_offset:], expected_normals)):
                assert max(abs(x-y) for x, y in zip(a, b)) < 1e-6
            normal_offset += len(points)
            nv += len(points)
            nf += len(expected_indices)//3
            model_geometry[model].append((int(part['geometry_prototype']), points))
        assert normal_offset == len(saved_normals)
        assert nv == int(row['vertices']) and nf == int(row['triangles'])
        mesh_count += len(meshes)
        vertex_count += nv
        triangle_count += nf

    selection = chunks((output / 'trees.selection').read_bytes())
    assert u32(selection[0x9df3]) == 5
    selected = chunks(selection[0x7708])
    max_error = 0.0
    parents_seen = set()
    for record in selected.values():
        record = chunks(record)
        assert u32(record[0x7703]) == 2
        body = chunks(record[0x7777])
        name, _ = string(body[0xF907])
        parent = int(name.rsplit('_', 1)[1])
        assert parent not in parents_seen
        parents_seen.add(parent)
        rows = by_parent[parent]
        assert all(int(r['selection_included']) == 1 for r in rows)
        model = int(rows[0]['model'])
        assert string(body[0x0902])[0] == models[model]['object_reference']
        parent_source = source_visuals[parent]
        if parent_source[1][1] == 6:
            children = set(struct.unpack_from('<'+'I'*u32(parent_source[10]), parent_source[10], 4))
            assert children == {int(r['visual']) for r in rows}, 'Whole tree source children changed'
        else:
            assert len(rows) == 1 and int(rows[0]['visual']) == parent
        expected_matrix = struct.unpack_from('<16f', source_visuals[int(rows[0]['visual'])][12])
        matrix = compose(struct.unpack('<9f', body[0xF903]))
        for _, points in model_geometry[model]:
            for point in points:
                for j in range(3):
                    error = abs(sum(point[i]*(matrix[4*i+j]-expected_matrix[4*i+j]) for i in range(3)) + matrix[12+j]-expected_matrix[12+j])
                    max_error = max(max_error, error)
        assert max_error <= 1e-4, 'Selection placements move source vertices'
    expected_parents = {int(r['parent_visual']) for r in instances if int(r['selection_included'])}
    assert parents_seen == expected_parents
    print(f'PASS: {len(models)} native objects, {mesh_count} meshes/material surfaces, {len(selected)} complete instance placements')
    print(f'PASS: {len(covered)} source tree visuals covered exactly once; local vertices, indices, UVs, normals and source tree lighting checked')
    print(f'Shared model vertices: {vertex_count}; triangles: {triangle_count}')
    print(f'Maximum independent placement error: {max_error:.9g} m')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('export', type=Path)
    args = parser.parse_args()
    audit(args.source, args.export)
