#!/bin/bash
# Share the PC's internet with the phone over the USB NCM link (not persistent).
set -e
sudo -n sysctl -q net.ipv4.ip_forward=1
sudo -n nft delete table ip op9 2>/dev/null || true
sudo -n nft -f - <<'NFT'
table ip op9 {
	chain postrouting {
		type nat hook postrouting priority srcnat; policy accept;
		ip saddr 172.16.42.0/24 ip daddr != 172.16.42.0/24 masquerade
	}
}
NFT
echo "NAT for 172.16.42.0/24 enabled"
