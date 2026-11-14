"""mine_ui - a tiny helper layer over PyQt5 for YOUR apps on this OS.

    import os, sys; sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
    import mine_ui
    app = mine_ui.make_app()
    class MyApp(mine_ui.Page):
        def __init__(self):
            super().__init__("My app")          # header bar with a Back button
            self.body.addWidget(...)            # put your widgets in self.body
    sys.exit(mine_ui.run(app, MyApp()))

Back = QApplication.quit() -> the launcher then shows the home screen again.
"""
import os, sys
from string import Template
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton

U = 24                                   # base unit in pixels, set by make_app()

_STYLE = Template("""
 QWidget { background:#101820; color:#ffffff; font-size:${f}px; }
 QLabel#title { background:#2D5BD0; padding:${p}px; font-size:${f2}px; }
 QLabel#muted { color:#A0B0C0; }
 QPushButton { background:#2D5BD0; border:none; border-radius:${r}px; padding:${p}px; color:#ffffff; }
 QPushButton:pressed { background:#1d3f94; }
 QPushButton#back { background:#1d3f94; border-radius:0; padding:${p}px ${p2}px; }
 QPushButton#ok { background:#2E9E5B; } QPushButton#ok:pressed { background:#1B6B3C; }
 QPushButton#warn { background:#C98A1B; } QPushButton#warn:pressed { background:#8A5E10; }
 QPushButton#danger { background:#B03A3A; } QPushButton#danger:pressed { background:#7A2626; }
""")

def screen_size(app):
    p = os.environ.get("MINE_PREVIEW_SIZE")           # e.g. 800x1280, only for desktop previews
    if p:
        w, h = p.lower().split("x"); return int(w), int(h)
    g = app.primaryScreen().geometry(); return g.width(), g.height()

def make_app():
    global U
    app = QApplication(sys.argv)
    app.setFont(QFont("DejaVu Sans"))
    w, h = screen_size(app)
    U = max(12, min(w, h) // 34)
    app.setStyleSheet(_STYLE.substitute(f=U, f2=int(U * 1.35), p=int(U * .7), p2=int(U * 1.3), r=int(U * .6)))
    return app

class Page(QWidget):
    """Header bar (Back + title) and an empty `self.body` layout for your content."""
    def __init__(self, title, back=True):
        super().__init__()
        outer = QVBoxLayout(self); outer.setContentsMargins(0, 0, 0, 0); outer.setSpacing(0)
        bar = QHBoxLayout(); bar.setContentsMargins(0, 0, 0, 0); bar.setSpacing(0)
        if back:
            b = QPushButton("\u25C0 Back"); b.setObjectName("back"); b.clicked.connect(QApplication.quit); bar.addWidget(b)
        t = QLabel(title); t.setObjectName("title"); bar.addWidget(t, 1)
        outer.addLayout(bar)
        self.body = QVBoxLayout(); self.body.setContentsMargins(U, U, U, U); self.body.setSpacing(U)
        outer.addLayout(self.body, 1)

def label(text, size=1.0, bold=False, muted=False, center=True):
    l = QLabel(text)
    if muted: l.setObjectName("muted")
    if center: l.setAlignment(Qt.AlignCenter)
    l.setStyleSheet(f"font-size:{int(U * size)}px;" + ("font-weight:bold;" if bold else ""))
    return l

def button(text, kind=None, slot=None):
    b = QPushButton(text)
    if kind: b.setObjectName(kind)
    if slot: b.clicked.connect(slot)
    return b

def run(app, widget):
    """Fullscreen on the device; renders PNG when QT_QPA_PLATFORM=offscreen (desktop preview)."""
    if os.environ.get("QT_QPA_PLATFORM", "").startswith("offscreen"):
        w, h = screen_size(app); widget.resize(w, h); widget.show(); app.processEvents()
        widget.grab().save(os.environ.get("PREVIEW_PNG", "/tmp/preview.png")); return 0
    try:                                             # tells /sbin/splash to stop; our first frame replaces it
        open(os.environ.get("MINE_UIREADY", "/run/ui.ready"), "w").close()
    except OSError:
        pass
    widget.setCursor(Qt.BlankCursor); widget.showFullScreen()
    return app.exec_()
