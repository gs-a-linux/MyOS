import sys
from PyQt5 import QtWidgets
from ui import Ui_MainWindow

class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)

        # Connect the button click signal to your function
        self.ui.pushButton.clicked.connect(self.on_button_clicked)

    def on_button_clicked(self):
        # Update the label text when clicked
        self.ui.label.setText("Clicked successfully!")

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())
