#!/usr/bin/env python3
"""Generates main-board.kicad_pcb from the parts in generate_schematic.py.

Run with KiCad's Python, which provides pcbnew. Places parts, routes each net with a
simple grid router, and adds GND pours on both layers. All pads are through-hole, so a
route can start on either layer. Fill the pour afterwards with kicad-cli pcb drc
--refill-zones --save-board.
"""

import heapq
import math
import random
from pathlib import Path

import pcbnew

from generate_schematic import PARTS

HERE = Path(__file__).resolve().parent
FOOTPRINT_ROOT = Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")

WIDTH, HEIGHT = 190.0, 102.0
GRID = 0.5
CLEARANCE = 0.3  # 0.2 mm DRC rule plus margin for grid rounding
EDGE_MARGIN = 1.0
VIA_SIZE, VIA_DRILL = 1.2, 0.6
VIA_COST = 40
BACK_COST = 3.0
TURN_COST = 0.8  # per 45 degrees, so routes stay straight
ORDER_ATTEMPTS = 12

# Soldered-on modules, both lying flat. The 0.96 in SSD1306 OLED (Hosyond, about 27 x 27 mm)
# has its pins centred along the top edge. The KY-040 encoder (19 x 26 mm, pins bent straight
# down) has its pins along the bottom edge with the dial facing up.
OLED_AREA = (8.0, 8.0, 35.3, 35.8)
ENCODER_AREA = (40.0, 9.0, 59.0, 35.0)

# Pad-1 position (mm from the board's top-left corner) and rotation of every part.
PLACEMENT = {
    "U1": (32, 59, 0),
    "U2": (70, 32, 0), "U3": (105, 32, 0), "U4": (140, 32, 0),
    "J2": (85.24, 11, 180), "J3": (120.24, 11, 180), "J4": (155.24, 11, 180),
    "J1": (175.08, 11, 180), "F1": (158, 26, 0), "C1": (170, 38, 0),
    "D1": (160, 50, 0), "J11": (178, 70, 90),
    "J10": (162, 78, 270), "J9": (162, 88, 0),
    "R1": (118, 95, 0), "Q1": (135, 95, 0),
    # Placed so their pins line up with the Nano pins they use.
    "J6": (54, 96, 0), "J8": (15, 92, 90),
    # OLED and encoder modules, upright with the Nano at the bottom: pins left to right.
    "J7": (OLED_AREA[0] + 9.84, OLED_AREA[1] + 2.0, 90),
    "J5": (ENCODER_AREA[0] + 4.42, ENCODER_AREA[3] - 2.0, 90),
}
MOUNTING_HOLES = [(5, 5), (WIDTH - 5, 5), (5, HEIGHT - 5), (WIDTH - 5, HEIGHT - 5)]

# The Nano ESP32's Wi-Fi antenna (end of its NORA-W106 module, away from the USB port, per the
# ABX00083 datasheet topology) sits over this area: no tracks, vias or pour on either layer.
ANTENNA_AREA = (29.5, 52.5, 49.7, 60.3)

# Copper-free spot on the front for the taped-down relay module (43 x 17 mm plus room).
RELAY_AREA = (108.0, 66.0, 155.0, 90.0)

# Track width in mm; the rest use the default. 2-2.5 mm suits 5 A, 1 mm suits a 2 A motor.
DEFAULT_WIDTH = 0.3
NET_WIDTHS = {"GND": 0.6, "+12V_IN": 2.5, "+12V": 2.0, "SOLENOID+": 1.5, "VBUS": 0.6, "+3V3": 0.5}
MOTOR_WIDTH = 1.0
ROUTE_FIRST = ["+12V_IN", "+12V", "SOLENOID+"]

LABELS = [
    (77, 3, "SLIDER MOTOR"), (112, 3, "PAN MOTOR"), (147, 3, "TILT MOTOR"),
    (77.6, 18.5, "B2  B1  A2  A1"), (112.6, 18.5, "B2  B1  A2  A1"), (147.6, 18.5, "B2  B1  A2  A1"),
    (76, 25, "EN side: left, top pin"), (111, 25, "EN side: left, top pin"), (146, 25, "EN side: left, top pin"),
    (172.5, 3, "12V IN"), (172.5, 18.5, "-    +"), (166, 67, "SOLENOID\nTOP -\nBOTTOM +"),
    (131, 78, "RELAY MODULE: TAPE HERE"),
    (21.65, 38, "OLED: GND VCC SCL SDA"), (21.65, 25, "OLED MODULE\nSCREEN UP"),
    (49.5, 37.5, "ENCODER: GND + SW DT CLK"), (49.5, 20, "ENCODER\nMODULE\nDIAL UP"),
    (56, 92, "LIMIT"), (56, 100.3, "SW GND"),
    (15, 80.5, "CAMERA"), (6.5, 88.3, "SCL\nSDA\nGND\nVCC"),
    (176, 80.5, "SOLDER WIRES\nTOP: RELAY NO\nBOTTOM: RELAY COM"), (174, 91, "RELAY\nVCC\nGND\nIN"),
]


def mm(v):
    return pcbnew.FromMM(v)


def point(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def width_of(net):
    if net in NET_WIDTHS:
        return NET_WIDTHS[net]
    if net.startswith(("SLIDE_", "PAN_", "TILT_")) and net[-2:] in ("A1", "A2", "B1", "B2"):
        return MOTOR_WIDTH
    return DEFAULT_WIDTH


def net_of(part, number):
    for pin_number, _, net in part["left"] + part["right"]:
        if pin_number == number:
            return net
    raise KeyError(f'{part["ref"]} has no pin {number}')


class Router:
    def __init__(self):
        self.cols = int(WIDTH / GRID) + 1
        self.rows = int(HEIGHT / GRID) + 1
        self.obstacles = []  # (x, y, radius, net, layers)
        self.tracks = {}  # net -> cells already routed

    def add(self, x, y, radius, net, layers=(0, 1)):
        self.obstacles.append((x, y, radius, net, layers))

    def blocked_map(self, net, half_width):
        blocked = [bytearray(self.cols * self.rows) for _ in (0, 1)]
        margin = half_width + EDGE_MARGIN
        for row in range(self.rows):
            for col in range(self.cols):
                x, y = col * GRID, row * GRID
                if x < margin or y < margin or x > WIDTH - margin or y > HEIGHT - margin:
                    blocked[0][row * self.cols + col] = blocked[1][row * self.cols + col] = 1
        for (x1, y1, x2, y2), layers in ((RELAY_AREA, (0,)), (ANTENNA_AREA, (0, 1))):
            for row in range(self.rows):
                for col in range(self.cols):
                    x, y = col * GRID, row * GRID
                    if x1 - half_width <= x <= x2 + half_width and y1 - half_width <= y <= y2 + half_width:
                        for layer in layers:
                            blocked[layer][row * self.cols + col] = 1
        for ox, oy, radius, other, layers in self.obstacles:
            if other == net:
                continue
            reach = radius + half_width + CLEARANCE
            span = int(reach / GRID) + 1
            cx, cy = round(ox / GRID), round(oy / GRID)
            for row in range(max(0, cy - span), min(self.rows, cy + span + 1)):
                for col in range(max(0, cx - span), min(self.cols, cx + span + 1)):
                    if math.hypot(col * GRID - ox, row * GRID - oy) < reach:
                        for layer in layers:
                            blocked[layer][row * self.cols + col] = 1
        return blocked

    def search(self, blocked, via_blocked, starts, goals):
        """A* over (layer, col, row, heading). Returns the path from a start to a goal."""
        goal_cells = set(goals)

        def estimate(col, row):
            return min(max(abs(col - g[0]), abs(row - g[1])) for g in goal_cells)

        no_heading = 8
        best = {}
        came = {}
        queue = []
        for layer, col, row in starts:
            state = (layer, col, row, no_heading)
            best[state] = 0.0
            heapq.heappush(queue, (estimate(col, row), 0.0, state))
        steps = [(1, 0, 1.0), (1, 1, 1.414), (0, 1, 1.0), (-1, 1, 1.414),
                 (-1, 0, 1.0), (-1, -1, 1.414), (0, -1, 1.0), (1, -1, 1.414)]
        while queue:
            _, cost, state = heapq.heappop(queue)
            if cost > best.get(state, math.inf):
                continue
            layer, col, row, heading = state
            if (col, row) in goal_cells:
                path = [state]
                while state in came:
                    state = came[state]
                    path.append(state)
                return [s[:3] for s in reversed(path)]
            moves = []
            layer_cost = BACK_COST if layer else 1.0
            for index, (dx, dy, step) in enumerate(steps):
                c, r = col + dx, row + dy
                if not (0 <= c < self.cols and 0 <= r < self.rows) or blocked[layer][r * self.cols + c]:
                    continue
                turn = 0
                if heading != no_heading:
                    turn = min((index - heading) % 8, (heading - index) % 8)
                    if turn > 2:
                        continue
                moves.append(((layer, c, r, index), step * layer_cost + turn * TURN_COST))
            if not via_blocked[0][row * self.cols + col] and not via_blocked[1][row * self.cols + col]:
                moves.append(((1 - layer, col, row, no_heading), VIA_COST))
            for nxt, step in moves:
                total = cost + step
                if total < best.get(nxt, math.inf):
                    best[nxt] = total
                    came[nxt] = state
                    heapq.heappush(queue, (total + estimate(nxt[1], nxt[2]), total, nxt))
        return None

    def route(self, net, pads):
        """Connects pads [(x, y)] with a tree. Returns (segments, vias) or raises."""
        width = width_of(net)
        half = width / 2
        blocked = self.blocked_map(net, half)
        via_blocked = self.blocked_map(net, VIA_SIZE / 2)
        cell = lambda p: (round(p[0] / GRID), round(p[1] / GRID))
        remaining = [cell(p) for p in pads]
        pad_at = {cell(p): p for p in pads}
        for col, row in remaining:  # a pad's own cell is always reachable
            blocked[0][row * self.cols + col] = blocked[1][row * self.cols + col] = 0
        first = remaining.pop(0)
        tree = {(0, *first), (1, *first)}
        segments, vias = [], []
        while remaining:
            path = self.search(blocked, via_blocked, tree, remaining)
            if path is None:
                raise RuntimeError(f"no route for {net} to {remaining}")
            end = (path[-1][1], path[-1][2])
            remaining.remove(end)
            for a, b in zip(path, path[1:]):
                if a[0] != b[0]:
                    vias.append((a[1] * GRID, a[2] * GRID))
                    self.add(a[1] * GRID, a[2] * GRID, VIA_SIZE / 2, net)
                else:
                    segments.append((a[0], a[1] * GRID, a[2] * GRID, b[1] * GRID, b[2] * GRID))
            for layer, col, row in path:
                self.add(col * GRID, row * GRID, half, net, (layer,))
            tree.update(path)
            tree.update({(0, end[0], end[1]), (1, end[0], end[1])})
        stubs = [(0, x, y, c * GRID, r * GRID) for (c, r), (x, y) in pad_at.items()
                 if abs(c * GRID - x) > 1e-6 or abs(r * GRID - y) > 1e-6]
        return segments + stubs, vias


def merge(segments):
    """Joins consecutive collinear grid steps so the board has fewer track pieces."""
    merged = []
    for seg in segments:
        if merged:
            layer, x1, y1, x2, y2 = merged[-1]
            if (layer == seg[0] and (x2, y2) == (seg[1], seg[2])
                    and abs((x2 - x1) * (seg[4] - y2) - (y2 - y1) * (seg[3] - x2)) < 1e-9
                    and (x2 - x1) * (seg[3] - x2) + (y2 - y1) * (seg[4] - y2) > 0):
                merged[-1] = (layer, x1, y1, seg[3], seg[4])
                continue
        merged.append(seg)
    return merged


def add_outline(board):
    corners = [(0, 0), (WIDTH, 0), (WIDTH, HEIGHT), (0, HEIGHT)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        shape = pcbnew.PCB_SHAPE(board)
        shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
        shape.SetStart(point(*a))
        shape.SetEnd(point(*b))
        shape.SetLayer(pcbnew.Edge_Cuts)
        shape.SetWidth(mm(0.1))
        board.Add(shape)


def add_silk_rectangle(board, x1, y1, x2, y2):
    corners = [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        shape = pcbnew.PCB_SHAPE(board)
        shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
        shape.SetStart(point(*a))
        shape.SetEnd(point(*b))
        shape.SetLayer(pcbnew.F_SilkS)
        shape.SetWidth(mm(0.2))
        board.Add(shape)


def add_label(board, x, y, text):
    label = pcbnew.PCB_TEXT(board)
    label.SetText(text)
    label.SetPosition(point(x, y))
    label.SetLayer(pcbnew.F_SilkS)
    label.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
    label.SetTextThickness(mm(0.18))
    board.Add(label)


def add_zone(board, net, layer, x1, y1, x2, y2, keepout=False):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(layer)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]:
        outline.Append(mm(x), mm(y))
    if keepout:
        zone.SetIsRuleArea(True)
        zone.SetDoNotAllowTracks(True)
        zone.SetDoNotAllowVias(True)
        zone.SetDoNotAllowFootprints(False)
        zone.SetDoNotAllowPads(False)
        zone.SetDoNotAllowZoneFills(True)
    else:
        zone.SetNetCode(net.GetNetCode())
        zone.SetMinThickness(mm(0.25))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(zone)
    return zone


def main():
    board = pcbnew.BOARD()
    nets = {}
    router = Router()
    pads_by_net = {}

    for name in sorted({p[2] for part in PARTS for p in part["left"] + part["right"] if p[2]}):
        nets[name] = pcbnew.NETINFO_ITEM(board, f"/{name}")  # eeschema prefixes label nets
        board.Add(nets[name])

    for part in PARTS:
        ref = part["ref"]
        lib, fp_name = part["fp"].split(":")
        footprint = pcbnew.FootprintLoad(str(FOOTPRINT_ROOT / f"{lib}.pretty"), fp_name)
        footprint.SetFPID(pcbnew.LIB_ID(lib, fp_name))
        footprint.SetReference(ref)
        footprint.SetValue(part["value"])
        x, y, angle = PLACEMENT[ref]
        footprint.SetPosition(point(x, y))
        footprint.SetOrientationDegrees(angle)
        board.Add(footprint)
        for pad in footprint.Pads():
            pos = pad.GetPosition()
            px, py = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
            size = pad.GetSize(pcbnew.F_Cu)
            radius = math.hypot(pcbnew.ToMM(size.x), pcbnew.ToMM(size.y)) / 2  # covers square pads
            net = net_of(part, pad.GetNumber())
            router.add(px, py, radius, net or f"unused-{ref}-{pad.GetNumber()}")
            if net:
                pad.SetNet(nets[net])
                pads_by_net.setdefault(net, []).append((px, py))

    for x, y in MOUNTING_HOLES:
        hole = pcbnew.FootprintLoad(str(FOOTPRINT_ROOT / "MountingHole.pretty"), "MountingHole_3.2mm_M3")
        hole.SetReference(f"H{MOUNTING_HOLES.index((x, y)) + 1}")
        hole.SetPosition(point(x, y))
        hole.SetBoardOnly(True)
        board.Add(hole)
        router.add(x, y, 2.0, "mounting-hole")

    add_outline(board)
    add_silk_rectangle(board, *RELAY_AREA)
    add_label(board, 39.6, 50.6, "WIFI ANTENNA: NO COPPER")
    add_silk_rectangle(board, *OLED_AREA)
    add_silk_rectangle(board, *ENCODER_AREA)
    for x, y, text in LABELS:
        add_label(board, x, y, text)

    signals = [n for n in pads_by_net if n != "GND" and n not in ROUTE_FIRST]
    rng = random.Random(7)
    best = None
    for attempt in range(ORDER_ATTEMPTS):
        rng.shuffle(signals)
        order = ROUTE_FIRST + sorted(signals, key=lambda n: -width_of(n)) if attempt == 0 else ROUTE_FIRST + signals
        trial = Router()
        trial.obstacles = list(router.obstacles)
        routed, failed = {}, []
        for net in order:
            try:
                routed[net] = trial.route(net, pads_by_net[net])
            except RuntimeError as err:
                failed.append(str(err))
        vias = sum(len(v) for _, v in routed.values())
        length = sum(math.hypot(x2 - x1, y2 - y1) for segs, _ in routed.values() for _, x1, y1, x2, y2 in segs)
        score = (len(failed), vias, length)
        print(f"attempt {attempt}: failed {len(failed)}, vias {vias}, length {length:.0f} mm", flush=True)
        if best is None or score < best[0]:
            best = (score, routed, failed, trial)
    score, routed, failed, trial = best
    # GND tracks last, so every GND pad is tied together even where tracks split the pours.
    routed["GND"] = trial.route("GND", pads_by_net["GND"])

    for net, (segments, vias) in routed.items():
        for layer, x1, y1, x2, y2 in merge(segments):
            track = pcbnew.PCB_TRACK(board)
            track.SetStart(point(x1, y1))
            track.SetEnd(point(x2, y2))
            track.SetWidth(mm(width_of(net)))
            track.SetLayer(pcbnew.B_Cu if layer else pcbnew.F_Cu)
            track.SetNet(nets[net])
            board.Add(track)
        for x, y in vias:
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(point(x, y))
            via.SetWidth(mm(VIA_SIZE))
            via.SetDrill(mm(VIA_DRILL))
            via.SetNet(nets[net])
            board.Add(via)

    add_zone(board, nets["GND"], pcbnew.B_Cu, 0.2, 0.2, WIDTH - 0.2, HEIGHT - 0.2)
    add_zone(board, nets["GND"], pcbnew.F_Cu, 0.2, 0.2, WIDTH - 0.2, HEIGHT - 0.2)  # rejoins back-pour islands
    add_zone(board, None, pcbnew.F_Cu, *RELAY_AREA, keepout=True)
    add_zone(board, None, pcbnew.F_Cu, *ANTENNA_AREA, keepout=True)
    add_zone(board, None, pcbnew.B_Cu, *ANTENNA_AREA, keepout=True)

    board.Save(str(HERE / "main-board.kicad_pcb"))
    print("\n".join(failed) or f"routed {len(routed)} nets, score {score}")


if __name__ == "__main__":
    main()
