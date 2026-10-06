#!/usr/bin/env python3
"""Generate matching editable OmniGraffle and SVG runtime diagrams (stdlib only)."""

import html
import math
import plistlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT = 1620, 1180
INK, MUTED, EDGE, CLOCK = '#182c43', '#4f647b', '#526b83', '#9885b5'
PALETTES = {
    'cpu': ('#edf5fc', '#c0d8ec', '#285c84'),
    'vio': ('#fff5e6', '#ebd3a5', '#8c601e'),
    'eye': ('#edf7ef', '#bfdcc7', '#327146'),
    'gpu': ('#f2eefa', '#d2c4e5', '#725097'),
    'state': ('#f3f6f9', '#ced9e3', '#4f647b'),
}
native, svg, text_boxes = [], [], []
next_id = 1000


def ident():
    global next_id
    next_id += 1
    return next_id


def rgb(color):
    return {c: str(int(color[i:i + 2], 16) / 255)
            for c, i in zip('rgb', (1, 3, 5))}


def bounds(x, y, w, h):
    return '{{%g, %g}, {%g, %g}}' % (x, y, w, h)


def rtf(value, size, color, bold=False):
    escaped = ''.join('\\' + c if c in '\\{}' else
                      c if ord(c) < 128 else r'\u%d?' % ord(c)
                      for c in value)
    r, g, b = (int(color[i:i + 2], 16) for i in (1, 3, 5))
    return (r'{\rtf1\ansi\deff0{\fonttbl{\f0 Helvetica;}}'
            + r'{\colortbl;\red%d\green%d\blue%d;}' % (r, g, b)
            + r'\pard\ql\f0\cf1\fs%d ' % round(2 * size)
            + (r'\b ' if bold else '') + escaped + '}')


def text(x, y, value, size=13, color=MUTED, bold=False, width=500):
    text_boxes.append((value, size, bold, width))
    native.append({
        'Class': 'ShapedGraphic', 'ID': ident(), 'Shape': 'Rectangle',
        'Bounds': bounds(x, y, width, size * 1.45), 'LayerIndex': 0,
        'Text': {'Text': rtf(value, size, color, bold),
                 'VerticalPad': 0, 'HorizontalPad': 0, 'Align': 0},
        'FontInfo': {'Font': 'Helvetica-Bold' if bold else 'Helvetica', 'Size': size},
        'Style': {'fill': {'Draws': 'NO'}, 'stroke': {'Draws': 'NO'},
                  'shadow': {'Draws': 'NO'}}, 'Wrap': 'NO', 'Flow': 'Clip',
    })
    svg.append(f'<text x="{x}" y="{y + size}" font-size="{size}" '
               f'font-weight="{700 if bold else 400}" fill="{color}">'
               f'{html.escape(value)}</text>')


def rect(x, y, w, h, fill, stroke='none', radius=10, object_id=None):
    obj_id = object_id if object_id is not None else ident()
    native.append({
        'Class': 'ShapedGraphic', 'ID': obj_id,
        'Shape': 'RoundRect' if radius else 'Rectangle',
        'Bounds': bounds(x, y, w, h), 'LayerIndex': 0,
        'Style': {'fill': {'Draws': 'YES', 'Color': rgb(fill)},
                  'stroke': {'Draws': 'NO' if stroke == 'none' else 'YES',
                             'Color': rgb(stroke if stroke != 'none' else fill),
                             'Width': 1.2, 'CornerRadius': radius},
                  'shadow': {'Draws': 'NO'}},
    })
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
               f'rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="1.2"/>')
    return obj_id


NODES = {
    'clock': (60, 112, 1460, 64, 100),
    'cam': (60, 230, 280, 102, 101),
    'imu': (60, 370, 280, 112, 102),
    'vio': (420, 230, 330, 180, 103),
    'int': (815, 250, 290, 120, 104),
    'pp': (1180, 250, 330, 140, 105),
    'render': (815, 550, 290, 150, 106),
    'frame': (815, 785, 290, 105, 107),
    'eye': (60, 690, 280, 115, 108),
    'ritnet': (420, 690, 330, 165, 109),
    'warp': (1180, 800, 330, 150, 110),
    'display': (1180, 1010, 330, 110, 111),
}
EDGES = []


def edge(source, target, points, dashed=False):
    color = CLOCK if dashed else EDGE
    obj_id = ident()
    EDGES.append((source, target, obj_id))
    stroke = {'HeadArrow': 'FilledArrow', 'TailArrow': '0', 'LineType': 0,
              'Width': 1.7, 'Color': rgb(color)}
    if dashed:
        stroke['Pattern'] = 1
    native.append({
        'Class': 'LineGraphic', 'ID': obj_id, 'LayerIndex': 0,
        'Tail': {'ID': NODES[source][4]}, 'Head': {'ID': NODES[target][4]},
        'Points': ['{%g, %g}' % p for p in points],
        'Style': {'stroke': stroke, 'shadow': {'Draws': 'NO'}},
    })
    coords = ' '.join('%g,%g' % p for p in points)
    dash = ' stroke-dasharray="6 5"' if dashed else ''
    svg.append(f'<polyline points="{coords}" fill="none" stroke="{color}" '
               f'stroke-width="1.7" stroke-linejoin="round"{dash}/>')
    # Explicit polygons also render in SVG readers without marker support.
    x, y = points[-1]
    dx, dy = x - points[-2][0], y - points[-2][1]
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    arrow = [(x, y), (x - 9 * ux - 4 * uy, y - 9 * uy + 4 * ux),
             (x - 9 * ux + 4 * uy, y - 9 * uy - 4 * ux)]
    arrow_points = ' '.join('%g,%g' % p for p in arrow)
    svg.append(f'<polygon points="{arrow_points}" fill="{color}"/>')


def card(key, title, palette, lines, badge):
    x, y, w, h, card_id = NODES[key]
    start = len(native)
    fill, border, accent = PALETTES[palette]
    rect(x, y, w, h, fill, border, object_id=card_id)
    text(x + 20, y + 15, title, 18, INK, True, w - 40)
    text(x + 20, y + 43, badge, 11, accent, True, w - 40)
    for n, line in enumerate(lines):
        text(x + 20, y + 65 + n * 19, line, 13, MUTED, width=w - 40)
    children = native[start:]
    del native[start:]
    native.append({'Class': 'Group', 'ID': ident(), 'LayerIndex': 0,
                   'Graphics': list(reversed(children)), 'Bounds': bounds(x, y, w, h)})


def generate():
    # Edges are behind the cards; each endpoint remains connected to a native shape.
    edge('clock', 'render', [(60, 145), (30, 145), (30, 595), (815, 595)], True)
    edge('clock', 'warp', [(1520, 144), (1570, 144), (1570, 825), (1510, 825)], True)
    edge('clock', 'display', [(1520, 144), (1570, 144), (1570, 1052), (1510, 1052)], True)
    edge('cam', 'vio', [(340, 280), (420, 280)])
    edge('imu', 'vio', [(340, 400), (380, 400), (380, 375), (420, 375)])
    edge('imu', 'int', [(340, 450), (960, 450), (960, 370)])
    edge('vio', 'int', [(750, 300), (815, 300)])
    edge('int', 'pp', [(1105, 300), (1180, 300)])
    edge('pp', 'render', [(1260, 390), (1260, 488), (960, 488), (960, 550)])
    edge('pp', 'warp', [(1390, 390), (1390, 800)])
    edge('eye', 'ritnet', [(340, 742), (420, 742)])
    edge('ritnet', 'warp', [(585, 855), (585, 950), (1145, 950), (1145, 900), (1180, 900)])
    edge('render', 'frame', [(960, 700), (960, 785)])
    edge('frame', 'warp', [(1105, 842), (1180, 842)])
    edge('warp', 'display', [(1345, 950), (1345, 1010)])

    text(60, 32, 'XRSight-RTOS', 34, INK, True, 1000)
    text(62, 77, 'Plugin dependencies and execution backends', 16, MUTED, width=900)
    rect(*NODES['clock'][:4], '#f5f2f9', '#dfd6ea', object_id=100)
    text(80, 126, 'SHARED TARGET CLOCK', 12, '#725097', True, 270)
    text(370, 126, 'CLINT mtime  /  120 Hz virtual display timeline', 15, INK, True, 1000)
    text(370, 150, 'Independent absolute schedules; GPU waits release the CPU.', 12, MUTED, width=1000)
    text(60, 201, 'MOTION TRACKING', 11, '#637d96', True, 600)
    text(815, 519, 'RENDER & DISPLAY', 11, '#8668a5', True, 600)
    text(60, 655, 'ASYNCHRONOUS EYE TRACKING', 11, '#4d855d', True, 600)

    card('cam', 'offline_cam', 'cpu', ['Stereo replay; bounded image queue'], 'CPU  /  TIMESTAMP-PACED')
    card('imu', 'offline_imu', 'cpu', ['IMU replay; separate queues', 'for VIO and integration'], 'CPU  /  TIMESTAMP-PACED')
    card('vio', 'openvins', 'vio', [
        'FP64 estimator + image processing',
        'Eigen / scalar OpenBLAS / RVV BLAS',
        'or FP32 Gemmini GEMM / GEMV',
        'with scalar or RVV conversion/packing',
        'Publishes the latest VIO baseline'], 'CPU  /  RVV  /  FP32 GEMMINI')
    card('int', 'imu_integrator', 'cpu', [
        'VIO baseline + retained IMU history', 'Publishes coherent prediction state'], 'CPU  /  FP64 EIGEN')
    card('pp', 'pose_prediction', 'cpu', [
        'Requested-time prediction service', 'Runs in the caller\'s thread',
        '50 ms horizon; stale-state checks'], 'CPU  /  RK4 & QUATERNION MATH')
    card('render', 'render_loop', 'gpu', [
        'Starts at vsync + 1 ms', 'Predicts the following vsync',
        'GPU delay: 6.944445 ms', 'Publishes a dummy stereo frame'], 'CPU  +  ASYNCHRONOUS GPU MODEL')
    card('frame', 'Latest completed frame', 'state', [
        'Immutable descriptor + saved pose'], 'CPU RAM  /  RETAINED SNAPSHOT')
    card('eye', 'offline_eye', 'eye', [
        'One repeated 240 x 160 image', '120 Hz; notifications coalesce'], 'CPU  /  EMBEDDED SAMPLE')
    card('ritnet', 'eye_tracking / RITNet', 'eye', [
        'Inference worker pinned to hart 0', 'INT8 Gemmini on custom2',
        'Completion fence after every operation', 'Publishes image-space eye centroid'], 'CPU WORKER  +  INT8 GEMMINI')
    card('warp', 'timewarp', 'gpu', [
        'Starts at upcoming vsync - 2 ms', 'Fresh pose + latest frame + latest eye',
        'CPU correction; GPU delay: 1 ms', 'Eye result is recorded, not applied'], 'CPU  +  ASYNCHRONOUS GPU MODEL')
    card('display', 'Presentation probe', 'state', [
        'Newest eligible completion at vsync', 'New / repeated / no output'], 'CPU  /  MAIN CONSUMER LOOP')

    # Short connector labels; timing policy also appears inside the worker cards.
    text(348, 260, 'stereo', 10, width=70)
    text(348, 408, 'IMU', 10, width=60)
    text(479, 460, 'Independent IMU queue', 12, width=350)
    text(756, 281, 'baseline', 10, width=58)
    text(1118, 281, 'state', 10, width=57)
    text(998, 466, 'Pose for the next vsync', 12, width=245)
    text(1410, 576, 'Fresh pose', 12, width=115)
    text(1410, 595, 'for warp vsync', 12, width=125)
    text(349, 704, 'latest', 10, width=68)
    text(349, 718, 'image', 10, width=68)
    text(674, 926, 'Latest completed eye result; reuse allowed', 12, width=430)
    text(980, 735, 'GPU completion', 12, width=200)
    text(1114, 806, 'frame', 10, width=64)
    text(1114, 820, 'reuse', 10, width=64)
    text(1206, 973, 'Warp completion', 12, width=135)
    text(606, 573, 'Vsync + 1 ms', 12, CLOCK, width=190)
    text(1519, 848, '-2 ms', 10, CLOCK, width=50)
    text(1519, 1070, 'vsync', 10, CLOCK, width=55)

    rect(60, 1010, 1045, 110, '#f8fafc', '#e3e9ef', radius=8)
    text(80, 1025, 'READING THE DIAGRAM', 11, INK, True, 300)
    text(325, 1023, 'Solid: data or returned pose', 12, EDGE, width=350)
    text(685, 1023, 'Dashed: scheduled wake / observation', 12, CLOCK, width=395)
    text(80, 1051, 'BLAS selection is global across eligible Eigen operations; OpenVINS is the main consumer.', 12, width=1000)
    text(80, 1072, 'FP32 BLAS is mixed precision; both Gemmini workers use hart 0 (FP32 custom3 / INT8 custom2).', 12, width=1000)
    text(80, 1093, 'Render and timewarp model GPU latency. Presentation is modeled; no shaders or physical display.', 12, width=1000)
    text(60, 1140, 'Native editable shapes, grouped cards and attached connectors', 11, '#738397', width=1000)


def validate(document):
    def flatten(items):
        for item in items:
            yield item
            yield from flatten(item.get('Graphics', []))
    objects = list(flatten(document['GraphicsList']))
    ids = [o['ID'] for o in objects]
    assert len(ids) == len(set(ids)), 'Duplicate graphic ID'
    assert len(EDGES) == 15
    for obj in objects:
        if obj['Class'] == 'LineGraphic':
            assert obj['Head']['ID'] in ids and obj['Tail']['ID'] in ids
        if 'Text' in obj:
            assert obj['Text']['Text'].startswith('{\\rtf1\\ansi'), 'Invalid RTF prefix'
            assert not obj['Text']['Text'].startswith('{\\\\rtf1'), 'Over-escaped RTF'


if __name__ == '__main__':
    generate()
    document = {
        'GraphDocumentVersion': 6, 'FileType': 'flat',
        'ApplicationVersion': ['com.omnigroup.OmniGraffle', '138.14.0.129428'],
        'Creator': 'XRSight-RTOS', 'SheetTitle': 'Runtime dependencies and backends',
        'CanvasSize': '{%g, %g}' % (WIDTH, HEIGHT), 'CanvasOrigin': '{0, 0}',
        'GraphicsList': list(reversed(native)),
        'Layers': [{'Name': 'Editable runtime diagram', 'View': 'YES', 'Print': 'YES', 'Lock': 'NO'}],
        'ActiveLayerIndex': 0, 'AutoAdjust': False, 'ReadOnly': 'NO',
        'PrintOnePage': True, 'HPages': 1, 'VPages': 1, 'GridInfo': {},
        'MasterSheets': [], 'UniqueID': 1, 'Zoom': 0.75,
    }
    validate(document)
    path = ROOT / 'docs/eye-tracking-flow.graffle'
    path.write_bytes(plistlib.dumps(document, sort_keys=False))
    defs = '<defs>' + ''.join(
        f'<marker id="{key}" viewBox="0 0 10 10" refX="9" refY="5" '
        f'markerWidth="6" markerHeight="6" orient="auto-start-reverse">'
        f'<path d="M 0 0 L 10 5 L 0 10 z" fill="{color}"/></marker>'
        for key, color in [('arrow', EDGE), ('clockArrow', CLOCK)]) + '</defs>'
    (ROOT / 'docs/diagrams/illixr-backends.svg').write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" '
        f'viewBox="0 0 {WIDTH} {HEIGHT}" font-family="Helvetica, Arial, sans-serif">'
        '<title>XRSight-RTOS plugin dependencies and execution backends</title>'
        '<desc>Independent render and timewarp workers share a display timeline. '
        'Pose prediction is an on-demand service; eye inference publishes asynchronously.</desc>'
        '<rect width="100%" height="100%" fill="white"/>' + defs + '\n'.join(svg) + '</svg>\n')
    # Optional local font-metric check, without adding a runtime dependency.
    try:
        from PIL import ImageFont
        for value, size, bold, width in text_boxes:
            font = ImageFont.truetype('/usr/share/fonts/truetype/liberation2/LiberationSans-'
                                     + ('Bold' if bold else 'Regular') + '.ttf', size)
            assert font.getlength(value) <= width, f'Label exceeds box: {value}'
    except ImportError:
        pass
    print(f'Wrote {path}: 12 nodes, 15 connected edges, 11 grouped plugin/state cards')
