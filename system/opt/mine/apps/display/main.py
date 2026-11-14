import os, sys; sys.path.insert(0, os.environ.get("MINE_LIB", "/opt/mine/lib"))
import mine_ui
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPainter, QColor
from PyQt5.QtWidgets import QLabel, QWidget
app = mine_ui.make_app()

RUN = os.environ.get("MINE_RUN", "/run")
MODES = ["pan", "both", "put", "none"]
RATES = [10, 20, 30, 60]

def kv(name):
    d = {}
    try:
        for l in open(f"{RUN}/{name}"):
            k, _, v = l.strip().partition("="); d[k] = v
    except OSError:
        pass
    return d

def send(cmd):                                   # talk to /sbin/fb-pump through a command file
    tmp = f"{RUN}/fbpump.cmd.tmp"
    try:
        with open(tmp, "w") as f: f.write(cmd + "\n")
        os.replace(tmp, f"{RUN}/fbpump.cmd")
    except OSError as e:
        print("cannot talk to fb-pump:", e, file=sys.stderr)

class Bar(QWidget):
    """Moving bar + frame counter. If the picture freezes while this should be moving, the DISPLAY is stuck."""
    def __init__(self):
        super().__init__(); self.n = 0; self.setMinimumHeight(mine_ui.U * 4)
        t = QTimer(self); t.timeout.connect(self.step); t.start(16)
    def step(self): self.n += 1; self.update()
    def paintEvent(self, _):
        p = QPainter(self); p.fillRect(self.rect(), QColor("#0b1117"))
        w = max(1, self.width() - 80); x = (self.n * 6) % (2 * w); x = x if x < w else 2 * w - x
        p.fillRect(x, 8, 80, self.height() - 16, QColor("#2E9E5B"))
        p.setPen(QColor("#ffffff")); p.drawText(12, self.height() - 14, f"frame {self.n}")

class Display(mine_ui.Page):
    def __init__(self):
        super().__init__("Display")
        self.txt = QLabel(); self.txt.setTextInteractionFlags(Qt.NoTextInteraction)
        self.txt.setStyleSheet(f"font-family:'DejaVu Sans Mono'; font-size:{int(mine_ui.U*.8)}px;")
        self.body.addWidget(self.txt); self.body.addWidget(Bar())
        self.body.addWidget(mine_ui.button("Reset display (blank + unblank)", "warn", lambda: send("reset")))
        self.mode_btn = mine_ui.button("", None, self.next_mode); self.body.addWidget(self.mode_btn)
        self.rate_btn = mine_ui.button("", None, self.next_rate); self.body.addWidget(self.rate_btn)
        self.body.addWidget(mine_ui.button("Unblank + backlight on", "ok", lambda: send("unblank")))
        self.body.addStretch(1)
        self.tick(); t = QTimer(self); t.timeout.connect(self.tick); t.start(1000)

    def cur(self):
        s = kv("fbpump.status")
        return s.get("mode", "?"), int(s.get("hz", "0") or 0)
    def next_mode(self):
        m, _ = self.cur(); send("mode " + MODES[(MODES.index(m) + 1) % len(MODES)] if m in MODES else "mode pan")
    def next_rate(self):
        _, h = self.cur(); send("hz %d" % (RATES[(RATES.index(h) + 1) % len(RATES)] if h in RATES else 30))

    def tick(self):
        i, s = kv("fbpump.info"), kv("fbpump.status")
        if not i and not s:
            self.txt.setText("fb-pump is not running\n(no files in %s)" % RUN)
        else:
            self.txt.setText("\n".join([
                f"fb      {i.get('name','?')}  {i.get('res','?')}  virt {i.get('virt','?')}  {i.get('bpp','?')}bpp",
                f"stride  {i.get('stride','?')}   smem {i.get('smem','?')}",
                f"pump    mode={s.get('mode','?')}  {s.get('hz','?')} Hz  pans={s.get('pans','?')}  puts={s.get('puts','?')}",
                f"        errors={s.get('errors','?')}  errno={s.get('last_errno','?')}  resets={s.get('resets','?')}  auto-fallback={s.get('auto_fallback','?')}"]))
        m, h = self.cur()
        self.mode_btn.setText(f"Refresh mode: {m.upper()}   (tap to change)"); self.rate_btn.setText(f"Refresh rate: {h} Hz   (tap to change)")

sys.exit(mine_ui.run(app, Display()))
