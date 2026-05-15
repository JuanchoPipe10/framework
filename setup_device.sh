#!/bin/bash
# setup_device.sh - Quick device setup for new Ultra96
# Sets hostname, IP, and prepares for framework installation

set -e

echo "=============================================="
echo "  Ultra96 Device Setup Wizard"
echo "=============================================="
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then 
    echo "ERROR: Please run as root (sudo ./setup_device.sh)"
    exit 1
fi

# Get device number
echo "Enter device/car number (e.g., 3 for car03):"
read -p "Device number: " DEVICE_NUM

if ! [[ "$DEVICE_NUM" =~ ^[0-9]+$ ]]; then
    echo "ERROR: Invalid number"
    exit 1
fi

DEVICE_ID="car$(printf "%02d" $DEVICE_NUM)"
DEVICE_IP="192.168.50.$((90 + DEVICE_NUM))"  # car01=92, car02=93, etc.
HOSTNAME="ultra96-${DEVICE_ID}"

echo ""
echo "Configuration:"
echo "  Device ID: $DEVICE_ID"
echo "  Hostname:  $HOSTNAME"
echo "  IP:        $DEVICE_IP"
echo ""
read -p "Is this correct? (y/n) [y]: " CONFIRM
CONFIRM=${CONFIRM:-y}

if [[ ! $CONFIRM =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

echo ""
echo "[1/4] Setting hostname..."
hostnamectl set-hostname $HOSTNAME
echo $HOSTNAME > /etc/hostname

echo "[2/4] Configuring static IP..."
# Backup original config
cp /etc/network/interfaces /etc/network/interfaces.backup 2>/dev/null || true

# Check if using NetworkManager or interfaces file
if systemctl is-active --quiet NetworkManager; then
    echo "Using NetworkManager..."
    nmcli con mod "$(nmcli -t -f NAME con show --active | head -1)" \
        ipv4.method manual \
        ipv4.addresses ${DEVICE_IP}/24 \
        ipv4.gateway 192.168.50.1 \
        ipv4.dns "8.8.8.8,8.8.4.4"
else
    echo "Using /etc/network/interfaces..."
    cat > /etc/network/interfaces.d/wlan0 << EOF
auto wlan0
iface wlan0 inet static
    address ${DEVICE_IP}
    netmask 255.255.255.0
    gateway 192.168.50.1
    dns-nameservers 8.8.8.8 8.8.4.4
EOF
fi

echo "[3/4] Configuring SSH..."
# Enable root login for easier management (optional)
sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
systemctl restart ssh || systemctl restart sshd

echo "[4/4] Installing dependencies..."
if command -v apt-get &> /dev/null; then
    apt-get update
    apt-get install -y gcc make git
elif command -v opkg &> /dev/null; then
    opkg update
    opkg install gcc make
fi

echo ""
echo "=============================================="
echo "  Setup Complete!"
echo "=============================================="
echo ""
echo "Device: $DEVICE_ID"
echo "IP:     $DEVICE_IP"
echo ""
echo "IMPORTANT: Reboot for changes to take effect"
echo ""
read -p "Reboot now? (y/n) [y]: " REBOOT
REBOOT=${REBOOT:-y}

if [[ $REBOOT =~ ^[Yy]$ ]]; then
    echo "Rebooting in 5 seconds..."
    sleep 5
    reboot
else
    echo "Please reboot manually: sudo reboot"
fi
