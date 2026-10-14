"""Read-only independent DX9 static/native scene audit, plus the tree audit.

Usage: python verify_scene.py <compiled-level-directory> <scene-export-directory>
Only the Python standard library is required. Assertions must remain enabled.
"""
import array
import csv
import hashlib
import math
import struct
from pathlib import Path
from functools import lru_cache
from verify_native import chunks, string, records, u32, floats, audit as audit_trees


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def audit(source, output):
    level = chunks((source / 'level').read_bytes())
    visuals = {i: chunks(b) for i, b in chunks(level[3]).items()}
    shaders, offset = [], 4
    for _ in range(u32(level[2])):
        name, offset = string(level[2], offset)
        shaders.append(name)
    assert offset == len(level[2])
    geom = chunks((source / 'level.geom').read_bytes())
    sizes = (4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8)
    buffers, offset = [], 4
    for _ in range(u32(geom[9])):
        decl, stride = {}, 0
        while True:
            stream, at, kind, method, usage, index = struct.unpack_from('<HHBBBB', geom[9], offset)
            offset += 8
            if stream == 255:
                break
            assert stream == method == 0
            decl[usage, index] = at, kind
            stride = max(stride, at + sizes[kind])
        count = u32(geom[9], offset)
        offset += 4
        buffers.append((decl, stride, memoryview(geom[9])[offset:offset + count*stride]))
        offset += count*stride
    assert offset == len(geom[9])
    indices, offset = [], 4
    for _ in range(u32(geom[10])):
        count = u32(geom[10], offset)
        offset += 4
        data = array.array('H')
        data.frombytes(geom[10][offset:offset + count*2])
        indices.append(data)
        offset += count*2
    assert offset == len(geom[10])
    windows = []
    if 11 in geom:
        offset = 4
        for _ in range(u32(geom[11])):
            count = u32(geom[11], offset+16)
            offset += 20
            windows.append(struct.unpack_from('<IHH', geom[11], offset))
            offset += count*8
        assert offset == len(geom[11])

    source_meshes, seen = {}, {}
    for visual, v in visuals.items():
        if v[1][1] not in (0, 2):
            continue
        vi, base, count, ii, first, ni = struct.unpack('<6I', v[21])
        if v[1][1] == 2:
            assert u32(v[6], 16) > 0
            start, triangles, count = struct.unpack_from('<IHH', v[6], 20)
            first += start
            ni = triangles*3
        ids = indices[ii][first:first+ni]
        assert len(ids) == ni and ni % 3 == 0 and max(ids) < count
        source_meshes[visual] = vi, base, count, ids, struct.unpack_from('<H', v[1], 2)[0]
        seen[visual] = bytearray(ni//3)

    @lru_cache(maxsize=300000)
    def vertex(visual, index):
        vi, base, count, _, _ = source_meshes[visual]
        assert index < count
        decl, stride, data = buffers[vi]
        start = (base+index)*stride
        po, pt = decl[0, 0]
        no, nt = decl[3, 0]
        to, tt = decl[5, 0]
        assert pt == 2 and nt in (2, 4)
        pos = struct.unpack_from('<3f', data, start+po)
        if tt == 1:
            uv = struct.unpack_from('<2f', data, start+to)
        else:
            assert tt in (6, 7)
            a, b = struct.unpack_from('<2h', data, start+to)
            tangent, tk = decl[6, 0]
            binormal, bk = decl[7, 0]
            assert tk == bk == 4
            uv = (f32(f32(a+f32(data[start+tangent+3]/255))/1024),
                  f32(f32(b+f32(data[start+binormal+3]/255))/1024))
        normal = (struct.unpack_from('<3f', data, start+no) if nt == 2 else
                  tuple(data[start+no+2-j]*2/255-1 for j in range(3)))
        length = math.sqrt(sum(x*x for x in normal))
        return pos, uv, tuple(x/length for x in normal)

    models = records(output / 'static_models.csv')
    parts = {}
    for row in records(output / 'static_parts.csv'):
        parts[int(row['model']), int(row['mesh_index'])] = row
    model_by_id = {int(row['model']): row for row in models}
    checked_models = set()

    @lru_cache(maxsize=32)
    def model_geometry(model):
        row = model_by_id[model]
        path = output / 'objects' / Path(*row['object_reference'].split('\\'))
        body = chunks(chunks(path.with_suffix('.object').read_bytes())[0x7777])
        assert struct.unpack('<H', body[0x0900])[0] == 0x10 and u32(body[0x0903]) == 0
        surfaces, offset = {}, 4
        for _ in range(u32(body[0x0907])):
            fields = []
            for _ in range(6):
                value, offset = string(body[0x0907], offset)
                fields.append(value)
            assert fields[1:4] == ['editor\\wire', '__recovery_unmapped__', '__recovery_unmapped__']
            assert struct.unpack_from('<3I', body[0x0907], offset) == (0, 0x112, 1)
            offset += 12
            assert fields[0] not in surfaces
            surfaces[fields[0]] = fields
        assert offset == len(body[0x0907])
        normals = path.with_suffix('.normals.bin').read_bytes()
        assert normals[:8] == b'XRNORM01' and len(normals) == 12+u32(normals, 8)*12
        saved = floats(normals[12:])
        meshes = chunks(body[0x0910])
        assert len(meshes) == len(surfaces) == int(row['parts'])
        result, nv, nf = [], 0, 0
        for part in range(len(meshes)):
            m = chunks(meshes[part])
            points = floats(m[0x1005][4:])
            assert len(points) == u32(m[0x1005])
            faces = list(struct.iter_unpack('<6I', m[0x1006][4:]))
            assert len(faces) == u32(m[0x1006])
            assert all(f[::2] == f[1::2] and max(f) < len(points) for f in faces)
            refs = m[0x1008]
            assert u32(refs) == len(points) and len(refs) == 4+len(points)*9
            assert all(struct.unpack_from('<BII', refs, 4+i*9) == (1, 0, i) for i in range(len(points)))
            vm = m[0x1012]
            assert u32(vm) == 1
            _, start = string(vm, 4)
            assert vm[start:start+3] == bytes((2, 0, 0))
            count = u32(vm, start+3)
            uv = list(struct.iter_unpack('<2f', vm[start+7:start+7+count*8]))
            assert count == len(points) and len(vm) == start+7+count*12
            assert tuple(x[0] for x in struct.iter_unpack('<I', vm[start+7+count*8:])) == tuple(range(count))
            sf = m[0x1009]
            assert struct.unpack_from('<H', sf)[0] == 1
            surface, at = string(sf, 2)
            assert surface in surfaces and u32(sf, at) == len(faces)
            assert tuple(x[0] for x in struct.iter_unpack('<I', sf[at+4:])) == tuple(range(len(faces)))
            p = parts[model, part]
            shader = shaders[int(p['shader_id'])]
            assert shader == p['source_shader_and_textures']
            assert surfaces[surface][4] == (shader.partition('/')[2].partition(',')[0] or 'ed\\ed_dummy')
            result.append((points, uv, saved[nv:nv+count], faces))
            nv += count
            nf += len(faces)
        assert nv == len(saved) == int(row['vertices']) and nf == int(row['triangles'])
        checked_models.add(model)
        return result

    instances = {int(r['component']): r for r in records(output / 'static_instances.csv')}
    origins = {i: tuple(f32(float(r[a])) for a in ('x', 'y', 'z')) for i, r in instances.items()}
    counts = dict.fromkeys(instances, 0)
    previous, source_to_native, native_to_source, addressed = None, {}, {}, set()
    with (output / 'static_faces.csv').open(encoding='utf-8', newline='') as f:
        for row in csv.DictReader(f):
            component, model, part, face, visual, source_face, rotation, shader = map(int, row.values())
            assert int(instances[component]['model']) == model and 0 <= rotation < 3
            if previous != component:
                source_to_native, native_to_source, addressed = {}, {}, set()
                previous = component
            assert (part, face) not in addressed
            addressed.add((part, face))
            counts[component] += 1
            assert seen[visual][source_face] == 0
            seen[visual][source_face] = 1
            _, _, _, ids, source_shader = source_meshes[visual]
            assert source_shader == shader == int(parts[model, part]['shader_id'])
            points, uv, normals, faces = model_geometry(model)[part]
            for corner in range(3):
                source_index = ids[source_face*3+(corner+rotation)%3]
                native_index = faces[face][corner*2]
                pos, expected_uv, normal = vertex(visual, source_index)
                assert tuple(f32(x+y) for x, y in zip(points[native_index], origins[component])) == pos, 'Position changed'
                assert uv[native_index] == expected_uv, 'UV changed'
                assert max(abs(a-b) for a, b in zip(normals[native_index], normal)) < 1e-6, 'Normal archive changed'
                source_key, native_key = (visual, source_index), (part, native_index)
                assert source_to_native.setdefault(source_key, native_key) == native_key, 'Source vertex split'
                assert native_to_source.setdefault(native_key, source_key) == source_key, 'Source vertices welded'
    assert all(counts[i] == int(r['triangles']) == int(model_by_id[int(r['model'])]['triangles']) for i, r in instances.items())
    assert all(all(mask) for mask in seen.values()), 'Missing source triangles'
    assert checked_models == set(model_by_id)
    selection = chunks((output / 'geometry.selection').read_bytes())
    assert u32(selection[0x9df3]) == 5
    selected = chunks(selection[0x7708])
    static_seen, tree_records = set(), []
    for raw in selected.values():
        record = chunks(raw)
        assert u32(record[0x7703]) == 2
        body = chunks(record[0x7777])
        name, _ = string(body[0xF907])
        if name.startswith('surface_candidate_'):
            component = int(name.rsplit('_', 1)[1])
            assert component not in static_seen
            static_seen.add(component)
            row = instances[component]
            assert string(body[0x0902])[0] == model_by_id[int(row['model'])]['object_reference']
            assert struct.unpack('<9f', body[0xF903]) == origins[component]+(0., 0., 0., 1., 1., 1.)
        else:
            assert name.startswith('recovered_tree_visual_')
            tree_records.append(raw)
    assert static_seen == set(instances)
    if (output / 'vegetation').exists():
        expected = chunks(chunks((output / 'vegetation' / 'trees.selection').read_bytes())[0x7708])
        assert tree_records == list(expected.values())
        audit_trees(source, output / 'vegetation')
    else:
        assert not tree_records
    original = (source / 'level.cform').read_bytes()
    assert (output / 'original.cform').read_bytes() == original
    print(f'PASS: {sum(counts.values())} source static triangles covered exactly once; winding, float32 positions, UVs, normals, shaders and vertex sharing checked')
    print(f'PASS: {len(models)} static models, {len(instances)} static placements; {len(selected)} total selection objects')
    print('PASS: original collision byte-identical; SHA256 '+hashlib.sha256(original).hexdigest())


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('export', type=Path)
    args = parser.parse_args()
    audit(args.source, args.export)
