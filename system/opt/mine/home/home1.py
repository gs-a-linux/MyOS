#!/usr/bin/env python3
"""HOME SCREEN / app launcher. Written in Python + Qt, lives at /opt/mine/home/home.py.

How apps start: only one Qt program can own the framebuffer at a time, so tapping a tile
writes the app's command into /run/launch and quits. The C launcher (/sbin/init) sees the
request, runs that app, and starts this home screen again when the app exits.

An app = a folder in /opt/mine/apps/<id>/ with app.json  {"name","icon","color","exec"}  and main.py.
"""
import os, sys, json, glob, time, ctypes
sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
import mine_ui
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel,
                             QPushButton, QScrollArea, QScroller)

APPS = os.environ.get("MINE_APPS", "/opt/mine/apps")
LAUNCH = os.environ.get("MINE_LAUNCH", "/run/launch")
RB_AUTOBOOT, RB_POWER_OFF = 0x01234567, 0x4321FEDC

def load_apps():
    apps = []
    for jf in sorted(glob.glob(os.path.join(APPS, "*", "app.json"))):
        d = os.path.dirname(jf)
        try:
            j = json.load(open(jf))
        except Exception as e:                                   # a broken app.json must not break the home screen
            j = {"name": os.path.basename(d) + " (bad app.json)", "icon": "!", "color": "#B03A3A"}
            print("app.json error:", jf, e, file=sys.stderr)
        j.setdefault("name", os.path.basename(d)); j.setdefault("icon", "\u25A0"); j.setdefault("color", "#2D5BD0")
        j.setdefault("exec", f"{sys.executable} -u {os.path.join(d, 'main.py')}")
        apps.append(j)
    return apps

def battery():
    for p in glob.glob("/sys/class/power_supply/*"):
        try:
            if open(p + "/type").read().strip() == "Battery":
                return int(open(p + "/capacity").read())
        except (OSError, ValueError):
            pass
    return None

class Tile(QPushButton):
    def __init__(self, app, on_launch):
        super().__init__()
        U = mine_ui.U
        self.setStyleSheet(f"QPushButton {{ background:{app['color']}; border-radius:{int(U*.9)}px; }}"
                           f"QPushButton:pressed {{ background:#ffffff33; }}")
        self.setMinimumHeight(int(U * 7))
        lay = QVBoxLayout(self); lay.setSpacing(0)
        for text, size in ((app["icon"], 3.0), (app["name"], 1.1)):
            l = QLabel(text); l.setAlignment(Qt.AlignCenter); l.setAttribute(Qt.WA_TransparentForMouseEvents)
            l.setStyleSheet(f"background:transparent; font-size:{int(U*size)}px;"); lay.addWidget(l)
        self.clicked.connect(lambda: on_launch(app["exec"]))

class Home(QWidget):
    def __init__(self):
        super().__init__()
        U = mine_ui.U; self.armed = None
        w, h = mine_ui.screen_size(QApplication.instance())
        root = QVBoxLayout(self); root.setContentsMargins(0, 0, 0, 0); root.setSpacing(0)
        self.status = QLabel(); self.status.setObjectName("title"); root.addWidget(self.status)
        self.clock = mine_ui.label("", 3.6, bold=True); self.date = mine_ui.label("", 1.0, muted=True)
        root.addSpacing(U); root.addWidget(self.clock); root.addWidget(self.date); root.addSpacing(U)

        grid_host = QWidget(); grid = QGridLayout(grid_host); grid.setContentsMargins(U, 0, U, 0); grid.setSpacing(U)
        cols = 2 if h >= w else 4
        for i, a in enumerate(load_apps()):
            grid.addWidget(Tile(a, self.launch), i // cols, i % cols)
        for c in range(cols): grid.setColumnStretch(c, 1)
        grid.setRowStretch(grid.rowCount(), 1)
        scroll = QScrollArea(); scroll.setWidgetResizable(True); scroll.setFrameShape(QScrollArea.NoFrame)
        scroll.setWidget(grid_host); QScroller.grabGesture(scroll.viewport(), QScroller.LeftMouseButtonGesture)
        root.addWidget(scroll, 1)

        row = QHBoxLayout(); row.setContentsMargins(U, U, U, U); row.setSpacing(U)
        self.btns = []
        for text, kind, cmd in (("Reboot", "warn", RB_AUTOBOOT), ("Power off", "danger", RB_POWER_OFF)):
            b = mine_ui.button(text, kind); b.clicked.connect(lambda _=False, b=b, c=cmd: self.power(b, c))
            b._label = text; self.btns.append(b); row.addWidget(b)
        root.addLayout(row)
        self.tick(); t = QTimer(self); t.timeout.connect(self.tick); t.start(1000)

    def tick(self):
        bat = battery()
        self.status.setText("My OS" + ("" if bat is None else f"      battery {bat}%"))
        self.clock.setText(time.strftime("%H:%M:%S")); self.date.setText(time.strftime("%A, %d %B %Y"))

    def launch(self, cmd):
        try:
            with open(LAUNCH, "w") as f: f.write(cmd + "\n")
        except OSError as e:
            print("cannot write", LAUNCH, e, file=sys.stderr); return
        QApplication.quit()

    def power(self, btn, cmd):                         # first tap arms, second tap within 3 s fires
        if self.armed is not btn:
            self.disarm(); self.armed = btn; btn.setText("Tap again to confirm"); QTimer.singleShot(3000, self.disarm); return
        os.sync(); ctypes.CDLL(None, use_errno=True).reboot(cmd)

    def disarm(self):
        self.armed = None
        for b in self.btns: b.setText(b._label)

if __name__ == "__main__":
    app = mine_ui.make_app()
    sys.exit(mine_ui.run(app, Home()))
