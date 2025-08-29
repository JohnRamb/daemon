#!/bin/python

# Import necessary modules
import sys
from PyQt5.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                             QLabel, QLineEdit, QComboBox, QCheckBox, QPushButton, 
                             QRadioButton, QListWidget, QMessageBox, QTextEdit, 
                             QSplitter, QFrame)
from PyQt5.QtCore import Qt
import socket
import os
import re

class NetworkTester(QWidget):
    def __init__(self):
        super().__init__()
        self.socket_path = '/tmp/network_daemon.sock'
        self.sock = None
        self.initUI()
        self.connect_to_socket()
        self.load_interfaces()

    def initUI(self):
        main_layout = QVBoxLayout()
        
        # Create splitter for main content and log
        splitter = QSplitter(Qt.Vertical)
        
        # Main content widget
        main_widget = QWidget()
        layout = QVBoxLayout(main_widget)
        
        # Interface selection
        self.interface_label = QLabel('Interface:')
        self.interface_combo = QComboBox()
        self.interface_combo.currentTextChanged.connect(self.on_interface_changed)
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

        # Network enable checkbox
        self.enable_check = QCheckBox('Enable Network')
        layout.addWidget(self.enable_check)

        # Status label
        self.status_label = QLabel('Status: Not connected')
        layout.addWidget(self.status_label)

        # Buttons layout
        buttons_layout = QHBoxLayout()
        
        # Apply button
        self.apply_btn = QPushButton('Apply')
        self.apply_btn.clicked.connect(self.apply_settings)
        buttons_layout.addWidget(self.apply_btn)

        # Refresh button
        self.refresh_btn = QPushButton('Refresh')
        self.refresh_btn.clicked.connect(self.refresh_interface_info)
        buttons_layout.addWidget(self.refresh_btn)

        # Reconnect button
        self.reconnect_btn = QPushButton('Reconnect')
        self.reconnect_btn.clicked.connect(self.reconnect_socket)
        buttons_layout.addWidget(self.reconnect_btn)

        # Clear log button
        self.clear_log_btn = QPushButton('Clear Log')
        self.clear_log_btn.clicked.connect(self.clear_log)
        buttons_layout.addWidget(self.clear_log_btn)

        layout.addLayout(buttons_layout)

        # Log window
        log_frame = QFrame()
        log_frame.setFrameStyle(QFrame.StyledPanel)
        log_layout = QVBoxLayout(log_frame)
        log_layout.addWidget(QLabel('Log:'))
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        log_layout.addWidget(self.log_text)

        # Add widgets to splitter
        splitter.addWidget(main_widget)
        splitter.addWidget(log_frame)
        splitter.setSizes([400, 200])  # Initial sizes

        main_layout.addWidget(splitter)
        self.setLayout(main_layout)
        self.setWindowTitle('Network Daemon Tester')
        self.resize(600, 800)
        self.show()

        # Connect radio buttons to enable/disable fields
        self.static_radio.toggled.connect(self.toggle_static_fields)

    def log_message(self, message):
        """Add message to log window"""
        self.log_text.append(f"{message}")
        # Auto-scroll to bottom
        self.log_text.verticalScrollBar().setValue(
            self.log_text.verticalScrollBar().maximum()
        )

    def clear_log(self):
        """Clear the log window"""
        self.log_text.clear()

    def connect_to_socket(self):
        """Establish connection to the socket"""
        try:
            if self.sock:
                self.sock.close()
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.socket_path)
            self.status_label.setText('Status: Connected to daemon')
            self.log_message('Connected to daemon socket')
        except Exception as e:
            error_msg = f'Connection failed - {str(e)}'
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            self.sock = None

    def reconnect_socket(self):
        """Reconnect to the socket"""
        self.log_message('Reconnecting to socket...')
        self.connect_to_socket()
        if self.sock:
            self.load_interfaces()

    def closeEvent(self, event):
        """Close socket connection when the program exits"""
        if self.sock:
            self.sock.close()
            self.log_message('Socket connection closed')
        event.accept()

    def toggle_static_fields(self, checked):
        """Enable/disable static IP fields"""
        self.ip_edit.setEnabled(checked)
        self.mask_edit.setEnabled(checked)
        self.gateway_edit.setEnabled(checked)

    def on_interface_changed(self, interface_name):
        """Handle interface selection change"""
        if interface_name:
            self.log_message(f'Interface changed to: {interface_name}')
            self.refresh_interface_info()

    def load_interfaces(self):
        """Load the list of interfaces from the daemon"""
        self.log_message('Loading interfaces...')
        if not self.sock:
            self.reconnect_socket()
            if not self.sock:
                error_msg = 'Cannot connect to socket'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg}')
                return

        response = self.send_command('(enumerate())')
        if response.startswith('(error'):
            error_msg = response[1:-1]  # Extract error message
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return

        if response and response.startswith('(enumerate('):
            self.interface_combo.clear()
            
            # Extract the content within (enumerate(...))
            interfaces_str = response[10:-1]  # Remove "(enumerate(" and ")"
            
            # Use regex to parse individual interface entries
            # Each entry is like: iface=lo addr=127.0.0.1 mac=00-00-00-00-00-00 gateway=none mask=8 flag=00010049
            interface_pattern = r'iface=([^\s]+)\s+addr=[^\s]+\s+mac=[^\s]+\s+gateway=[^\s]+\s+mask=[^\s]+\s+flag=[^\s]+'
            interfaces = re.findall(interface_pattern, interfaces_str)
            
            # Add interfaces to combo box
            for iface in interfaces:
                self.interface_combo.addItem(iface)
            
            if self.interface_combo.count() > 0:
                self.interface_combo.setCurrentIndex(0)
                self.log_message(f'Found {self.interface_combo.count()} interfaces')
            else:
                error_msg = 'No interfaces found'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg} - Response: {response}')
        else:
            error_msg = 'Failed to load interfaces'
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg} - Response: {response}')

    def refresh_interface_info(self):
        """Refresh information for the selected interface"""
        if not self.sock:
            self.reconnect_socket()
            if not self.sock:
                return
        
        interface = self.interface_combo.currentText()
        if not interface:
            self.log_message('ERROR: No interface selected')
            return
        
        self.log_message(f'Refreshing info for interface: {interface}')
        
        # Get interface status
        status_response = self.send_command(f'(status({interface}))')
        if status_response:
            self.parse_interface_status(status_response)
        
        # Get IP configuration
        ip_response = self.send_command(f'(getIP({interface}))')
        if ip_response:
            self.parse_ip_config(ip_response)
        
        # Load DNS
        self.load_dns()

    def parse_interface_status(self, response):
        """Parse interface status"""
        self.log_message(f'Parsing interface status: {response}')
        
        if response.startswith('(error'):
            error_msg = response[1:-1]
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return
        
        # Example response: "(status(eno1,up))" or "(status(eno1,down))"
        if 'up' in response.lower():
            self.enable_check.setChecked(True)
            self.status_label.setText('Status: Interface is UP')
            self.log_message('Interface status: UP')
        elif 'down' in response.lower():
            self.enable_check.setChecked(False)
            self.status_label.setText('Status: Interface is DOWN')
            self.log_message('Interface status: DOWN')
        else:
            self.log_message(f'ERROR: Unexpected status response: {response}')

    def parse_ip_config(self, response):
        """Parse IP configuration"""
        self.log_message(f'Parsing IP config: {response}')
        
        if response.startswith('(error'):
            error_msg = response[1:-1]
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return
        
        # Example DHCP response: "(dhcpOn(eno1:192.168.130.207:none:UP:192.168.130.196))"
        # Example static response: "(setStatic(eno1,192.168.1.100,24,192.168.1.1))"
        if response.startswith('(dhcpOn('):
            self.dynamic_radio.setChecked(True)
            self.toggle_static_fields(False)
            
            # Parse DHCP response
            match = re.search(r'\(dhcpOn\(([^:]+):([^:]+):([^:]+):([^:]+):([^)]+)\)\)', response)
            if match:
                iface, ip, mask, status, gateway = match.groups()
                self.ip_edit.setText(ip)
                self.mask_edit.setText(mask if mask != 'none' else '')
                self.gateway_edit.setText(gateway if gateway != 'none' else '')
                status_msg = f'DHCP configured - IP: {ip}, Gateway: {gateway}'
                self.status_label.setText(f'Status: {status_msg}')
                self.log_message(status_msg)
            else:
                self.log_message(f'ERROR: Failed to parse DHCP response: {response}')
                
        elif response.startswith('(setStatic('):
            self.static_radio.setChecked(True)
            self.toggle_static_fields(True)
            
            # Parse static response
            match = re.search(r'\(setStatic\(([^,]+),([^,]+),([^,]+),([^)]+)\)\)', response)
            if match:
                iface, ip, mask, gateway = match.groups()
                self.ip_edit.setText(ip)
                self.mask_edit.setText(mask)
                self.gateway_edit.setText(gateway)
                status_msg = f'Static configured - IP: {ip}, Mask: {mask}, Gateway: {gateway}'
                self.status_label.setText(f'Status: {status_msg}')
                self.log_message(status_msg)
            else:
                self.log_message(f'ERROR: Failed to parse static response: {response}')
        
        elif response.startswith('(on(') or response.startswith('(off('):
            if 'success' in response.lower():
                self.status_label.setText('Status: Operation completed successfully')
                self.log_message('Operation completed successfully')
            else:
                self.log_message(f'ERROR: Operation failed: {response}')
        else:
            self.log_message(f'ERROR: Unexpected IP config response: {response}')

    def load_dns(self):
        """Load DNS servers from system (fallback)"""
        try:
            self.dns_list.clear()
            with open('/etc/resolv.conf', 'r') as f:
                for line in f:
                    if line.strip().startswith('nameserver'):
                        dns = line.strip().split(' ')[1]
                        self.dns_list.addItem(dns)
            self.log_message(f'Loaded {self.dns_list.count()} DNS servers from resolv.conf')
        except Exception as e:
            error_msg = f'Error loading DNS: {str(e)}'
            self.dns_list.addItem(error_msg)
            self.log_message(f'ERROR: {error_msg}')

    def send_command(self, command):
        """Send command through the socket"""
        if not self.sock:
            self.reconnect_socket()
            if not self.sock:
                error_msg = 'Not connected to socket'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg}')
                return ''
        
        try:
            self.log_message(f'Sending command: {command}')
            self.sock.sendall(command.encode())
            response = self.sock.recv(1024).decode()
            self.log_message(f'Received response: {response}')
            self.status_label.setText(f'Status: Command executed - {response[:50]}...')
            return response
        except Exception as e:
            error_msg = f'Failed to send command - {str(e)}'
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            self.sock = None
            return ''

    def apply_settings(self):
        """Apply network settings"""
        if not self.sock:
            self.reconnect_socket()
            if not self.sock:
                QMessageBox.warning(self, 'Error', 'Not connected to socket')
                self.log_message('ERROR: Cannot apply settings - not connected to socket')
                return

        interface = self.interface_combo.currentText()
        if not interface:
            QMessageBox.warning(self, 'Error', 'Select an interface')
            self.log_message('ERROR: No interface selected')
            return

        self.log_message('Applying settings...')
        
        # Enable/disable network
        if self.enable_check.isChecked():
            response1 = self.send_command(f'(on({interface}))')
        else:
            response1 = self.send_command(f'(off({interface}))')

        # Configure addressing
        if self.dynamic_radio.isChecked():
            response2 = self.send_command(f'(dhcpOn({interface}))')
        else:
            ip = self.ip_edit.text()
            mask = self.mask_edit.text()
            gateway = self.gateway_edit.text()
            if not all([ip, mask, gateway]):
                QMessageBox.warning(self, 'Error', 'All static fields must be filled')
                self.log_message('ERROR: Static IP fields not complete')
                return
            response2 = self.send_command(f'(setStatic({interface},{ip},{mask},{gateway}))')

        # Refresh information after applying settings
        self.refresh_interface_info()

        # Show results
        result_msg = f'Network: {response1}\nAddressing: {response2}'
        QMessageBox.information(self, 'Results', result_msg)
        self.log_message(f'Apply results: {result_msg}')

if __name__ == '__main__':
    app = QApplication(sys.argv)
    ex = NetworkTester()
    sys.exit(app.exec_())