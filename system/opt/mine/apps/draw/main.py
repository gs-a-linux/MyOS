import os, sys; sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
import math
import mine_ui
from PyQt5.QtCore import Qt, QPoint
from PyQt5.QtGui import QPainter, QPen, QColor
from PyQt5.QtWidgets import QWidget, QHBoxLayout
app = mine_ui.make_app()
COLORS = ["#ffffff", "#2E9E5B", "#C98A1B", "#B03A3A", "#4aa3ff"]

class Canvas(QWidget):                      # touches arrive as mouse events
    def __init__(self):
        super().__init__(); self.strokes = []; self.ci = 0
    def mousePressEvent(self, e): self.strokes.append((COLORS[self.ci], [e.pos()])); self.update()
    def mouseMoveEvent(self, e):
        if self.strokes: self.strokes[-1][1].append(e.pos()); self.update()
    def paintEvent(self, _):
        p = QPainter(self); p.fillRect(self.rect(), QColor("#0b1117")); p.setRenderHint(QPainter.Antialiasing)
        for col, pts in self.strokes:
            p.setPen(QPen(QColor(col), max(4, mine_ui.U // 3), Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
            if len(pts) == 1: p.drawPoint(pts[0])
            for a, b in zip(pts, pts[1:]): p.drawLine(a, b)

class Draw(mine_ui.Page):
    def __init__(self):
        super().__init__("Draw")
        self.c = Canvas(); self.body.setContentsMargins(0, 0, 0, 0); self.body.setSpacing(0); self.body.addWidget(self.c, 1)
        row = QHBoxLayout(); row.setContentsMargins(mine_ui.U, mine_ui.U, mine_ui.U, mine_ui.U)
        row.addWidget(mine_ui.button("Colour", "ok", self.colour)); row.addWidget(mine_ui.button("Clear", "warn", self.clear))
        self.body.addLayout(row)
    def colour(self): self.c.ci = (self.c.ci + 1) % len(COLORS)
    def clear(self): self.c.strokes.clear(); self.c.update()

w = Draw()
if os.environ.get("QT_QPA_PLATFORM", "").startswith("offscreen"):                    # demo scribble for the preview
    w.c.strokes.append(("#2E9E5B", [QPoint(120 + i * 12, 400 + int(120 * math.sin(i / 5))) for i in range(45)]))
sys.exit(mine_ui.run(app, w))
