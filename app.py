# Import necessary modules
import sys
from PyQt5.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QComboBox, QCheckBox, QPushButton, QRadioButton, QListWidget, QMessageBox
from PyQt5.QtCore import Qt
import socket
import os

class NetworkTester(QWidget):
    def __init__(self):
        super().__init__()
        self.socket_path = '/tmp/network_daemon.sock'
        self.initUI()
        self.load_interfaces()

    def initUI(self):
        layout = QVBoxLayout()

        # Interface selection
        self.interface_label = QLabel('Interface:')
        self.interface_combo = QComboBox()
        layout.addWidget(self.interface_label)
        layout.addWidget(self.interface_combo)

        # Addressing type
        self.static_radio = QRadioButton('Static')
        self.dynamic_radio = QRadioButton('Dynamic')
        self.dynamic_radio.setChecked(True)
        addressing_layout = QHBoxLayout()
        addressing_layout.addWidget(self.static_radio)
        addressing_layout.addWidget(self.dynamic_radio)
        layout.addLayout(addressing_layout)

        # Static IP fields
        self.ip_label = QLabel('IP:')
        self.ip_edit = QLineEdit()
        self.mask_label = QLabel('Mask (prefix):')
        self.mask_edit = QLineEdit()
        self.gateway_label = QLabel('Gateway:')
        self.gateway_edit = QLineEdit()
        self.ip_edit.setEnabled(False)
        self.mask_edit.setEnabled(False)
        self.gateway_edit.setEnabled(False)
        layout.addWidget(self.ip_label)
        layout.addWidget(self.ip_edit)
        layout.addWidget(self.mask_label)
        layout.addWidget(self.mask_edit)
        layout.addWidget(self.gateway_label)
        layout.addWidget(self.gateway_edit)

        # DNS list
        self.dns_label = QLabel('DNS Servers:')
        self.dns_list = QListWidget()
        layout.addWidget(self.dns_label)
        layout.addWidget(self.dns_list)
        self.load_dns()

        # Network enable checkbox
        self.enable_check = QCheckBox('Enable Network')
        layout.addWidget(self.enable_check)

        # Apply button
        self.apply_btn = QPushButton('Apply')
        self.apply_btn.clicked.connect(self.apply_settings)
        layout.addWidget(self.apply_btn)

        self.setLayout(layout)
        self.setWindowTitle('Network Daemon Tester')
        self.show()

        # Connect radio buttons to enable/disable fields
        self.static_radio.toggled.connect(self.toggle_static_fields)

    def toggle_static_fields(self, checked):
        self.ip_edit.setEnabled(checked)
        self.mask_edit.setEnabled(checked)
        self.gateway_edit.setEnabled(checked)

    def load_interfaces(self):
        response = self.send_command('(enumerate())')
        if response.startswith('(enumerate('):
            interfaces = response[11:-2].split(',')
            for iface in interfaces:
                self.interface_combo.addItem(iface.split(':')[0])

    def load_dns(self):
        try:
            with open('/etc/resolv.conf', 'r') as f:
                for line in f:
                    if line.strip().startswith('nameserver'):
                        dns = line.strip().split(' ')[1]
                        self.dns_list.addItem(dns)
        except Exception as e:
            self.dns_list.addItem(f'Error loading DNS: {str(e)}')

    def send_command(self, command):
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(self.socket_path)
            sock.sendall(command.encode())
            response = sock.recv(1024).decode()
            sock.close()
            return response
        except Exception as e:
            QMessageBox.warning(self, 'Error', f'Failed to send command: {str(e)}')
            return ''

    def apply_settings(self):
        interface = self.interface_combo.currentText()
        if not interface:
            QMessageBox.warning(self, 'Error', 'Select an interface')
            return

        if self.enable_check.isChecked():
            self.send_command(f'(on({interface}))')
        else:
            self.send_command(f'(off({interface}))')

        if self.dynamic_radio.isChecked():
            response = self.send_command(f'(dhcpOn({interface}))')
        else:
            ip = self.ip_edit.text()
            mask = self.mask_edit.text()
            gateway = self.gateway_edit.text()
            response = self.send_command(f'(setStatic({interface},{ip},{mask},{gateway}))')

        QMessageBox.information(self, 'Response', response)

if __name__ == '__main__':
    app = QApplication(sys.argv)
    ex = NetworkTester()
    sys.exit(app.exec_())