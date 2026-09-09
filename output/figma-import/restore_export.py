"""Restore this Figma export's lost IDs against the calibrated source; fail on ambiguity."""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from svg_geometry import flatten_path, join_station_centers

ROOT = Path(__file__).resolve().parents[2]
NS = {'s': 'http://www.w3.org/2000/svg'}
ET.register_namespace('', NS['s'])
original = ET.parse(ROOT / 'data/calibration/network-labeled.svg').getroot()
export = ET.parse(ROOT / 'data/calibration/network-figma.svg').getroot()
assert export.get('viewBox') == '0 0 4096 4096'
assert not any('transform' in e.attrib for e in export.iter())
paths = export.findall('.//s:path', NS)
lines = {}
old_edges = {}
for g in original.findall('s:g', NS):
    if re.fullmatch(r'line-\d+', g.get('id', '')):
        lid = int(g.get('id')[5:])
        color = re.search(r'stroke:(#[0-9A-Fa-f]+)', g.get('style'))[1]
        lines[lid] = color
        for p in g.findall('s:path', NS):
            old_edges[int(p.get('id')[5:])] = (lid, p, flatten_path(p.get('d')))
old_anchors = {(int(c.get('data-station-id')), int(c.get('data-line-id'))): c
               for c in original.findall('.//s:circle', NS)}
anchors = {}
max_marker_shift = 0
for p in paths:
    if not (p.get('fill') == 'white' and p.get('stroke')):
        continue
    # Exported circles use only absolute M/C/Z; extrema lie on the endpoints.
    assert set(re.findall('[A-Za-z]', p.get('d'))) <= set('MCZ')
    values = [float(n) for n in re.findall(r'[-+]?\d*\.?\d+', p.get('d'))]
    xs, ys = values[::2], values[1::2]
    center = complex((min(xs)+max(xs))/2, (min(ys)+max(ys))/2)
    candidates = sorted((abs(center-complex(float(c.get('cx')),float(c.get('cy')))), key)
                        for key,c in old_anchors.items() if lines[key[1]] == p.get('stroke'))
    distance, key = candidates[0]
    assert distance < 35 and candidates[1][0] - distance > 1 and key not in anchors, (key, distance)
    anchors[key] = center
    max_marker_shift = max(max_marker_shift, distance)
assert set(old_anchors)-set(anchors) == {(17,3)}
anchors[17,3] = anchors[17,19]  # Shared Guangzhou South station marker in the export.

# Explicitly identified merged paths; retain all intermediate routing stations.
merged = {
    'M1396 3119V2987': ([32,33], False),
    'M1408 3231L1409.02 3050': ([187,188], True),
    'M2421 2689L2606 2878': ([287,288,289], False),
}

def project(points, point):
    length = 0
    choices = []
    for a,b in zip(points, points[1:]):
        delta = b-a
        if not abs(delta):
            continue
        t = max(0, min(1, ((point-a).real*delta.real+(point-a).imag*delta.imag)/abs(delta)**2))
        q = a+t*delta
        choices.append((abs(q-point), length+t*abs(delta), q))
        length += abs(delta)
    return min(choices)

def split(points, ids):
    lid = old_edges[ids[0]][0]
    cuts = [(0,points[0])]
    for eid in ids[:-1]:
        sid = int(old_edges[eid][1].get('data-to-id'))
        distance, position, q = project(points, anchors[sid,lid])
        assert distance < 15 and position > cuts[-1][0]
        cuts.append((position,q))
    positions = [0]
    for a,b in zip(points,points[1:]): positions.append(positions[-1]+abs(b-a))
    cuts.append((positions[-1], points[-1]))
    for eid,(start,a),(end,b) in zip(ids,cuts,cuts[1:]):
        yield eid,[a]+[p for d,p in zip(positions,points) if start<d<end]+[b]

restored = {}
merged_seen = set()
for p in paths:
    if not p.get('stroke') or p.get('fill'):
        continue
    points = flatten_path(p.get('d'))
    if p.get('d') in merged:
        ids,reverse = merged[p.get('d')]
        if reverse: points.reverse()
        assignments = list(split(points,ids))
        merged_seen.add(p.get('d'))
    else:
        candidates=sorted((abs(points[0]-old[0])+abs(points[-1]-old[-1]),eid)
                          for eid,(lid,_,old) in old_edges.items() if lines[lid]==p.get('stroke'))
        distance,eid = candidates[0]
        assert distance < 60 and candidates[1][0]-distance > 1, (eid,distance)
        assignments=[(eid,points)]
    for eid,points in assignments:
        assert eid not in restored
        lid,element,_ = old_edges[eid]
        assert lines[lid] == p.get('stroke')
        a,b = anchors[int(element.get('data-from-id')),lid],anchors[int(element.get('data-to-id')),lid]
        assert max(abs(points[0]-a),abs(points[-1]-b)) < 40, eid
        restored[eid] = join_station_centers(points,a,b)
assert merged_seen == set(merged) and set(restored) == set(old_edges)

# Keep readable station labels and their prior placement hints, not outlined glyphs.
for g in list(original):
    if g.get('id') == 'reference': original.remove(g)
for key,c in old_anchors.items():
    p = anchors[key]
    c.set('cx',f'{p.real:.6f}'); c.set('cy',f'{p.imag:.6f}')
for eid,points in restored.items():
    old_edges[eid][1].set('d', 'M '+' L '.join(f'{x:.6f},{y:.6f}' for x,y in points))
output = ROOT/'data/calibration/network-figma-import.svg'
ET.ElementTree(original).write(output,encoding='utf-8',xml_declaration=True)
report={'export_paths':417,'restored_edges':len(restored),'station_line_anchors':len(anchors),
        'max_marker_shift_px':max_marker_shift,'merged_edge_groups':[v[0] for v in merged.values()],
        'recovered_anchor':{'station_id':17,'line_id':3,'from_line_id':19},
        'labels':'Prior names and placement hints retained; exported outline text is not imported.'}
(ROOT/'output/figma-import/restoration.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
