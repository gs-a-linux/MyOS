import sys
from PyQt5 import QtWidgets
from PyQt5.QtCore import Qt

# Explicitly import the generated UI class from ui.py
from ui import Ui_MainWindow
import subprocess
import ctypes
import os

RB_AUTOBOOT, RB_POWER_OFF = 0x01234567, 0x4321FEDC
class HomeWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        self.ui.off.clicked.connect(self.Turnoff)
        self.ui.adb.clicked.connect(self.startadb)
        self.cmd = RB_POWER_OFF
    def Turnoff(self):
         os.sync(); ctypes.CDLL(None, use_errno=True).reboot(self.cmd)
    def startadb(self):
        a = subprocess.getoutput(["ls /opt/mine/apps/home/shell/"])
        print(a.strip())
        self.ui.label_2.setText(a.strip())

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    window = HomeWindow()
    window.show()
    sys.exit(app.exec_())
