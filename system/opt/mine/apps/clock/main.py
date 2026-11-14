import os, sys; sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
import time
import mine_ui
from PyQt5.QtCore import QTimer
app = mine_ui.make_app()

class Clock(mine_ui.Page):
    def __init__(self):
        super().__init__("Clock")
        self.t = mine_ui.label("", 5.5, bold=True); self.d = mine_ui.label("", 1.3, muted=True); self.u = mine_ui.label("", 1.0, muted=True)
        self.body.addStretch(1)
        for w in (self.t, self.d, self.u): self.body.addWidget(w)
        self.body.addStretch(1)
        self.tick(); tm = QTimer(self); tm.timeout.connect(self.tick); tm.start(500)
    def tick(self):
        self.t.setText(time.strftime("%H:%M:%S")); self.d.setText(time.strftime("%A, %d %B %Y"))
        try: up = int(float(open("/proc/uptime").read().split()[0]))
        except OSError: up = 0
        self.u.setText(f"up {up // 3600}h {up % 3600 // 60}m {up % 60}s")

sys.exit(mine_ui.run(app, Clock()))
