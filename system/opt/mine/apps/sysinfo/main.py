import os, sys; sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
import platform, glob
import mine_ui
from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtWidgets import QLabel
from PyQt5 import QtCore
app = mine_ui.make_app()

def rd(p, default="?"):
    try: return open(p).read().strip()
    except OSError: return default

def meminfo():
    m = {}
    for l in rd("/proc/meminfo", "").splitlines():
        k, _, v = l.partition(":"); m[k] = int(v.split()[0]) if v.split() else 0
    t, a = m.get("MemTotal", 0), m.get("MemAvailable", 0)
    return f"{(t - a) // 1024} / {t // 1024} MiB used"

class Info(mine_ui.Page):
    def __init__(self):
        super().__init__("System info")
        self.l = QLabel(); self.l.setTextInteractionFlags(Qt.NoTextInteraction)
        self.l.setStyleSheet(f"font-family:'DejaVu Sans Mono'; font-size:{int(mine_ui.U*.95)}px;")
        self.body.addWidget(self.l); self.body.addStretch(1)
        self.tick(); t = QTimer(self); t.timeout.connect(self.tick); t.start(2000)
    def tick(self):
        s = app.primaryScreen().geometry(); u = platform.uname()
        fb = f"{rd('/sys/class/graphics/fb0/virtual_size')} @ {rd('/sys/class/graphics/fb0/bits_per_pixel')} bpp"
        bat = next((rd(p + "/capacity") + "%" for p in glob.glob("/sys/class/power_supply/*") if rd(p + "/type") == "Battery"), "n/a")
        rows = [("kernel", u.release), ("machine", u.machine), ("python", platform.python_version()),
                ("Qt", QtCore.QT_VERSION_STR), ("PyQt", QtCore.PYQT_VERSION_STR),
                ("screen", f"{s.width()}x{s.height()}"), ("fb0", fb), ("memory", meminfo()),
                ("load", rd("/proc/loadavg").split(" ")[0]), ("battery", bat)]
        self.l.setText("\n".join(f"{k:<8} {v}" for k, v in rows))

sys.exit(mine_ui.run(app, Info()))
