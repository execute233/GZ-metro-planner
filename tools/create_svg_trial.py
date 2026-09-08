"""Create an isolated 4096px SVG tracing trial; never updates runtime data."""

import base64
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'data' / 'calibration'
SVG = 'http://www.w3.org/2000/svg'
INK = 'http://www.inkscape.org/namespaces/inkscape'
SOD = 'http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd'
ET.register_namespace('', SVG)
ET.register_namespace('inkscape', INK)
ET.register_namespace('sodipodi', SOD)


def element(parent, tag, **attrs):
    return ET.SubElement(parent, f'{{{SVG}}}{tag}', attrs)


def layer(root, key, label, hidden=False):
    node = element(root, 'g', id=key)
    node.set(f'{{{INK}}}groupmode', 'layer')
    node.set(f'{{{INK}}}label', label)
    if hidden:
        node.set('style', 'display:none')
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--full-line2', action='store_true',
                        help='Create a separate full Line 2 drawing')
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    target = OUT / ('line2-full.svg' if args.full_line2 else 'line2-trial.svg')
    if target.exists():
        raise SystemExit(f'Refusing to overwrite editable artwork: {target}')
    network = json.loads((ROOT / 'data/source/network.json').read_text('utf-8'))
    stations = {s['id']: s for s in network['stations']}
    # Manually sampled from the full-resolution image. These are Line 2's
    # visual anchors inside numbered station badges, not OCR label centers.
    anchors = {
        47: (1114, 1994), 48: (1140, 1899), 49: (1191, 1848),
        50: (1243, 1797), 51: (1302, 1730), 52: (1370, 1669),
        53: (1430, 1609), 54: (1490, 1549), 55: (1575, 1465),
    }
    if args.full_line2:
        anchors.update({
            17: (1413, 3137), 34: (1413, 3051), 35: (1396, 2983),
            36: (1396, 2932), 37: (1396, 2881), 38: (1396, 2812),
            39: (1396, 2753), 40: (1396, 2684), 41: (1396, 2591),
            42: (1371, 2522), 43: (1320, 2471), 44: (1242, 2402),
            9: (1114, 2224), 45: (1114, 2155), 46: (1114, 2096),
            47: (1114, 1976), 55: (1593, 1456),
            # The straight blue centerline passes between the two numbered
            # badges here. Do not bend the line toward the upper badge.
            51: (1302, 1738),
        })
    root = ET.Element(f'{{{SVG}}}svg', {
        'width': '4096', 'height': '4096', 'viewBox': '0 0 4096 4096',
        'version': '1.1',
    })
    section = '广州南站—嘉禾望岗' if args.full_line2 else '广州火车站—嘉禾望岗'
    element(root, 'title').text = f'2号线人工描线试验：{section}'
    element(root, 'desc').text = (
        f'4096×4096 原图像素坐标；{len(anchors)} 个线路视觉锚点、{len(anchors) - 1} 个区间。'
        '人工目视初校，待复核；未写入 network.json 或 MBTiles。'
    )
    background = layer(root, 'reference', '01 原图（锁定）')
    background.set(f'{{{SOD}}}insensitive', 'true')
    image = element(background, 'image', x='0', y='0', width='4096', height='4096')
    image.set('href', 'data:image/png;base64,' + base64.b64encode(
        (ROOT / 'data/railway.png').read_bytes()).decode('ascii'))
    # xlink is supported by older Inkscape versions as well as SVG 2 viewers.
    image.set('{http://www.w3.org/1999/xlink}href', image.attrib.pop('href'))
    old = layer(root, 'old-trace', '02 旧自动描线（仅对比）', True)
    old.set('style', 'display:none;fill:none;stroke:#ff3300;stroke-width:3')
    lines = layer(root, 'line-3', '03 校准线路：2号线（洋红对照）')
    lines.set('style', 'fill:none;stroke:#e000ba;stroke-width:3;stroke-linecap:round;stroke-linejoin:round')
    for edge in network['edges']:
        a, b = edge['from_id'], edge['to_id']
        if edge['line_id'] != 3 or a not in anchors or b not in anchors:
            continue
        old_d = 'M ' + ' L '.join(f'{x},{y}' for x, y in edge['points'])
        element(old, 'path', id=f'old-edge-{edge["id"]}', d=old_d)
        x, y = anchors[a]
        nx, ny = anchors[b]
        d = f'M {x},{y} L {nx},{ny}'
        if a == 47:
            d = 'M 1114,1994 L 1114,1952 C 1114,1929 1124,1915 1140,1899'
        elif a == 44:
            d = 'M 1242,2393 L 1133,2284 C 1121,2272 1114,2263 1114,2244 L 1114,2215'
        elif a == 41:
            d = 'M 1396,2582 L 1396,2568 C 1396,2547 1386,2537 1371,2522'
        if args.full_line2:
            # Join centered interchange anchors within their badge footprints;
            # preserve the source centerline outside those small areas.
            d = {
                17: 'M 1413,3137 L 1413,3051',
                34: 'M 1413,3051 C 1413,3022 1396,3012 1396,2983',
                41: 'M 1396,2591 L 1396,2568 C 1396,2547 1386,2537 1371,2522',
                43: 'M 1320,2471 L 1256,2407 L 1242,2402',
                44: 'M 1242,2402 L 1228,2379 L 1133,2284 C 1121,2272 1114,2263 1114,2244 L 1114,2224',
                47: 'M 1114,1976 L 1114,1952 C 1114,1929 1124,1915 1140,1899',
                54: 'M 1490,1549 L 1561,1479 L 1593,1456',
            }.get(a, d)
        path = element(lines, 'path', id=f'edge-{edge["id"]}', d=d)
        path.set('data-line-id', '3')
        path.set('data-from-id', str(a))
        path.set('data-to-id', str(b))
        path.set(f'{{{INK}}}label', f'{stations[a]["name"]}—{stations[b]["name"]}')
    marks = layer(root, 'anchors', '04 站点视觉锚点（2号线）')
    labels = layer(root, 'labels', '05 独立站名（默认隐藏）', True)
    for sid, (x, y) in anchors.items():
        marker = element(marks, 'circle', id=f'station-{sid}-line-3',
                         cx=str(x), cy=str(y), r='5', fill='none',
                         stroke='#e000ba', **{'stroke-width': '1.5'})
        marker.set('data-station-id', str(sid))
        marker.set('data-line-id', '3')
        marker.set(f'{{{INK}}}label', stations[sid]['name'])
        label = element(labels, 'text', id=f'label-{sid}', x=str(x + 18),
                        y=str(y - 12), fill='#e000ba',
                        **{'font-size': '18', 'font-family': 'Microsoft YaHei,sans-serif'})
        label.text = stations[sid]['name']
    ET.indent(root)
    ET.ElementTree(root).write(target, encoding='utf-8', xml_declaration=True)
    print(target)


if __name__ == '__main__':
    main()
