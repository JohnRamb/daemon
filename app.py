#!/bin/python

# Import necessary modules
import sys
import socket
import os
import re
import time
from PyQt5.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, 
                             QLabel, QLineEdit, QComboBox, QCheckBox, QPushButton, 
                             QRadioButton, QListWidget, QMessageBox, QTextEdit, 
                             QSplitter, QFrame)
from PyQt5.QtCore import Qt, QThread, pyqtSignal

class SocketThread(QThread):
    """Thread for handling socket operations asynchronously"""
    response_signal = pyqtSignal(str)
    error_signal = pyqtSignal(str)

    def __init__(self, sock, command):
        super().__init__()
        self.sock = sock
        self.command = command

    def run(self):
        try:
            self.sock.settimeout(5.0)  # Set timeout for socket operations
            self.sock.sendall(self.command.encode())
            response = ""
            while True:
                chunk = self.sock.recv(1024).decode()
                if not chunk:
                    break
                response += chunk
                if len(chunk) < 1024:
                    break
            self.response_signal.emit(response)
        except socket.timeout:
            self.error_signal.emit('Socket operation timed out')
        except Exception as e:
            self.error_signal.emit(str(e))

class NetworkTester(QWidget):
    def __init__(self):
        super().__init__()
        self.socket_path = '/tmp/network_daemon.sock'
        self.sock = None
        self.interfaces = {}  # Dictionary to store interface parameters
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
        """Add message to log window with timestamp"""
        self.log_text.append(f"[{time.strftime('%H:%M:%S')}] {message}")
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
            if not os.path.exists(self.socket_path):
                error_msg = f'Socket file {self.socket_path} does not exist'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg}')
                return
            if self.sock:
                self.sock.close()
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(5.0)  # Set timeout for connection
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

        self.send_command('(enumerate())')

    def parse_add_message(self, response):
        """Parse add_iface and add_route messages"""
        self.log_message(f'Parsing add message: {response}')
        
        if response.startswith('(error'):
            error_msg = response[1:-1]
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return False

        # Pattern for add_iface and add_route
        pattern = r'\((add_iface|add_route|del_iface|del_route)\(iface=([^\s]+)\s+addr=([^\s]+)\s+mac=([^\s]+)\s+gateway=([^\s]+)\s+mask=([^\s]+)\s+flag=([^\s]+)\)\)'
        match = re.match(pattern, response)
        
        if match:
            cmd, iface, addr, mac, gateway, mask, flag = match.groups()
            iface = iface.strip('()')  # Clean interface name
            if cmd == 'add_iface':
                # Add interface to combo box and interfaces dictionary
                self.interfaces[iface] = {
                    'addr': addr, 'mac': mac, 'gateway': gateway, 'mask': mask, 'flag': flag
                }
                if iface not in [self.interface_combo.itemText(i) for i in range(self.interface_combo.count())]:
                    self.interface_combo.addItem(iface)
                    self.log_message(f'Added interface: {iface}')
                    self.status_label.setText(f'Status: Interface {iface} added')
                else:
                    self.log_message(f'Interface {iface} already exists')
                
                # Update interface info if the added interface is selected
                if self.interface_combo.currentText() == iface:
                    self.ip_edit.setText(addr if addr != 'none' else '')
                    self.mask_edit.setText(mask if mask != 'none' else '')
                    self.gateway_edit.setText(gateway if gateway != 'none' else '')
                    self.enable_check.setChecked('00011043' in flag)  # Example: Assume flag indicates up/down
                    self.log_message(f'Updated info for {iface}: addr={addr}, mac={mac}, gateway={gateway}, mask={mask}, flag={flag}')
                
            elif cmd == 'add_route':
                route_info = f'Route added: iface={iface}, addr={addr}, mac={mac}, gateway={gateway}, mask={mask}, flag={flag}'
                self.log_message(route_info)
                self.status_label.setText(f'Status: Route added for {iface}')
            return True
        else:
            self.log_message(f'ERROR: Failed to parse add message: {response}')
            return False

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
        
        # Ensure interface name is clean
        interface = interface.strip('()')
        self.log_message(f'Refreshing info for interface: {interface}')
        self.send_command(f'(status({interface}))')
        self.load_dns()

    def parse_interface_status(self, response):
        """Parse interface status"""
        self.log_message(f'Parsing interface status: {response}')
        
        if response.startswith('(error'):
            error_msg = response[1:-1]
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return
        
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
        
        if response.startswith('(on(') or response.startswith('(off(') or response.startswith('(dhcpOn('):
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
        """Send command through the socket asynchronously"""
        if not self.sock:
            self.reconnect_socket()
            if not self.sock:
                error_msg = 'Not connected to socket'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg}')
                return ''
        
        self.log_message(f'Sending command: {command}')
        self.thread = SocketThread(self.sock, command)
        self.thread.response_signal.connect(self.handle_response)
        self.thread.error_signal.connect(self.handle_error)
        self.thread.start()
        return ''

    def handle_response(self, response):
        """Handle response from socket thread"""
        self.log_message(f'Received response: {response}')
        self.status_label.setText(f'Status: Command executed - {response[:50]}...')
        if response.startswith('(add_iface(') or response.startswith('(add_route('):
            self.parse_add_message(response)
        elif response.startswith('(enumerate('):
            self.handle_enumerate_response(response)
        elif response.startswith('(status('):
            self.parse_interface_status(response)
        elif response.startswith('(on(') or response.startswith('(off(') or response.startswith('(dhcpOn('):
            self.parse_ip_config(response)

    def handle_error(self, error):
        """Handle error from socket thread"""
        error_msg = f'Failed to send command - {error}'
        self.status_label.setText(f'Status: {error_msg}')
        self.log_message(f'ERROR: {error_msg}')
        self.sock = None

    def handle_enumerate_response(self, response):
        """Handle response from enumerate command"""
        self.log_message(f'Raw enumerate response: {response}')
        
        if response.startswith('(error'):
            error_msg = response[1:-1]
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg}')
            return

        self.interface_combo.clear()
        self.interfaces.clear()  # Clear previous interfaces
        if response.startswith('(enumerate('):
            interfaces_str = response[10:-1]  # Remove "(enumerate(" and ")"
            self.log_message(f'Interfaces string: {interfaces_str}')
            
            try:
                # Use regex to extract interface details
                interface_pattern = r'iface=([^\s,\)]+)\s+addr=([^\s]+)\s+mac=([^\s]+)\s+gateway=([^\s]+)\s+mask=([^\s]+)\s+flag=([^\s]+)'
                interfaces = re.findall(interface_pattern, interfaces_str)
                
                if not interfaces:
                    error_msg = 'No valid interfaces found'
                    self.status_label.setText(f'Status: {error_msg}')
                    self.log_message(f'ERROR: {error_msg} - Response: {response}')
                    return
                    
                for iface, addr, mac, gateway, mask, flag in interfaces:
                    # Clean interface name
                    iface = iface.strip('()')
                    if iface:
                        self.interface_combo.addItem(iface)
                        self.interfaces[iface] = {
                            'addr': addr,
                            'mac': mac,
                            'gateway': gateway,
                            'mask': mask,
                            'flag': flag
                        }
                        self.log_message(f'Added interface: {iface} (addr={addr}, mac={mac}, gateway={gateway}, mask={mask}, flag={flag})')
                
                if self.interface_combo.count() > 0:
                    self.interface_combo.setCurrentIndex(0)
                    self.log_message(f'Found {self.interface_combo.count()} interfaces')
                else:
                    error_msg = 'No valid interfaces found after parsing'
                    self.status_label.setText(f'Status: {error_msg}')
                    self.log_message(f'ERROR: {error_msg} - Response: {response}')
                    
            except Exception as e:
                error_msg = f'Failed to parse interfaces: {str(e)}'
                self.status_label.setText(f'Status: {error_msg}')
                self.log_message(f'ERROR: {error_msg} - Response: {response}')
        else:
            error_msg = 'Unexpected enumerate response format'
            self.status_label.setText(f'Status: {error_msg}')
            self.log_message(f'ERROR: {error_msg} - Response: {response}')

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

        # Clean interface name
        interface = interface.strip('()')
        self.log_message(f'Applying settings for interface: {interface}')
        
        # Enable/disable network
        if self.enable_check.isChecked():
            self.send_command(f'(on({interface}))')
        else:
            self.send_command(f'(off({interface}))')

        # Configure addressing
        if self.dynamic_radio.isChecked():
            # Check if interface parameters are available
            if interface not in self.interfaces:
                QMessageBox.warning(self, 'Error', f'No parameters available for interface {interface}')
                self.log_message(f'ERROR: No parameters available for interface {interface}')
                return
            params = self.interfaces[interface]
            dhcp_command = f"(dhcpOn(iface={interface} addr={params['addr']} mac={params['mac']} gateway={params['gateway']} mask={params['mask']} flag={params['flag']}))"
            self.send_command(dhcp_command)
        else:
            ip = self.ip_edit.text()
            mask = self.mask_edit.text()
            gateway = self.gateway_edit.text()
            if not all([ip, mask, gateway]):
                QMessageBox.warning(self, 'Error', 'All static fields must be filled')
                self.log_message('ERROR: Static IP fields not complete')
                return
            self.send_command(f'(setStatic({interface},{ip},{mask},{gateway}))')

        # Refresh information after applying settings
        self.refresh_interface_info()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    ex = NetworkTester()
    sys.exit(app.exec_())