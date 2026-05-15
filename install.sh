#!/bin/bash
# install.sh - Framework Installation Script
# Run as root on Ultra96

set -e

INSTALL_DIR="/opt/car-framework"
SERVICE_NAME="car-framework"

echo "=============================================="
echo "  Car Communication Framework Installer"
echo "=============================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "ERROR: Please run as root (sudo ./install.sh)"
    exit 1
fi

# Detect device ID automatically
detect_device_id() {
    # Try to read from /etc/hostname
    HOSTNAME=$(cat /etc/hostname 2>/dev/null || echo "unknown")
    
    # Check if it matches pattern (ultra96-car01, ultra96-car02, etc.)
    if [[ $HOSTNAME =~ ultra96-car([0-9]+) ]]; then
        echo "car${BASH_REMATCH[1]}"
    elif [[ $HOSTNAME =~ car([0-9]+) ]]; then
        echo "car${BASH_REMATCH[1]}"
    else
        # Fallback: use last octet of IP
        IP=$(hostname -I | awk '{print $1}')
        LAST_OCTET=$(echo $IP | cut -d. -f4)
        echo "car${LAST_OCTET}"
    fi
}

DEVICE_ID=$(detect_device_id)
echo "Detected Device ID: $DEVICE_ID"
echo ""

read -p "Is this correct? (y/n) [y]: " CONFIRM
CONFIRM=${CONFIRM:-y}

if [[ ! $CONFIRM =~ ^[Yy]$ ]]; then
    read -p "Enter device ID (e.g., car03): " DEVICE_ID
fi

echo ""
echo "Installing framework for device: $DEVICE_ID"
echo ""

# 1. Create installation directory
echo "[1/7] Creating installation directory..."
mkdir -p $INSTALL_DIR
mkdir -p $INSTALL_DIR/config
mkdir -p $INSTALL_DIR/logs

# 2. Build framework
echo "[2/7] Building framework..."
make clean
make

# 3. Copy files
echo "[3/7] Copying files..."
cp bin/framework $INSTALL_DIR/
cp -r config/* $INSTALL_DIR/config/
chmod +x $INSTALL_DIR/framework

# 4. Create device-specific config
echo "[4/7] Creating device configuration..."
cat > $INSTALL_DIR/config/device.conf << EOF
# Device Configuration
DEVICE_ID=$DEVICE_ID
DEVICE_TYPE=ultra96
TCP_PORT=60000

# Network Configuration
SUBNET=192.168.50.0/24
EOF

# 5. Create systemd service
echo "[5/7] Creating systemd service..."
cat > /etc/systemd/system/${SERVICE_NAME}.service << EOF
[Unit]
Description=Car Communication Framework
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=$INSTALL_DIR
ExecStart=$INSTALL_DIR/framework $DEVICE_ID 60000 $INSTALL_DIR/config/devices.conf
Restart=always
RestartSec=10
StandardOutput=append:$INSTALL_DIR/logs/framework.log
StandardError=append:$INSTALL_DIR/logs/framework-error.log

[Install]
WantedBy=multi-user.target
EOF

# 6. Enable service
echo "[6/7] Enabling service..."
systemctl daemon-reload
systemctl enable ${SERVICE_NAME}.service

# 7. Create management script
echo "[7/7] Creating management scripts..."
cat > /usr/local/bin/car-framework << 'EOF'
#!/bin/bash
# Car Framework Management Script

case "$1" in
    start)
        systemctl start car-framework
        echo "Framework started"
        ;;
    stop)
        systemctl stop car-framework
        echo "Framework stopped"
        ;;
    restart)
        systemctl restart car-framework
        echo "Framework restarted"
        ;;
    status)
        systemctl status car-framework
        ;;
    logs)
        tail -f /opt/car-framework/logs/framework.log
        ;;
    config)
        nano /opt/car-framework/config/devices.conf
        ;;
    *)
        echo "Usage: car-framework {start|stop|restart|status|logs|config}"
        exit 1
        ;;
esac
EOF

chmod +x /usr/local/bin/car-framework

echo ""
echo "=============================================="
echo "  Installation Complete!"
echo "=============================================="
echo ""
echo "Device ID: $DEVICE_ID"
echo "Installation: $INSTALL_DIR"
echo "Logs: $INSTALL_DIR/logs/"
echo ""
echo "Management Commands:"
echo "  car-framework start    - Start the framework"
echo "  car-framework stop     - Stop the framework"
echo "  car-framework restart  - Restart the framework"
echo "  car-framework status   - Check status"
echo "  car-framework logs     - View live logs"
echo "  car-framework config   - Edit device list"
echo ""
echo "Framework will auto-start on boot."
echo ""
read -p "Start framework now? (y/n) [y]: " START_NOW
START_NOW=${START_NOW:-y}

if [[ $START_NOW =~ ^[Yy]$ ]]; then
    systemctl start ${SERVICE_NAME}
    sleep 2
    systemctl status ${SERVICE_NAME} --no-pager
fi

echo ""
echo "Installation finished!"
