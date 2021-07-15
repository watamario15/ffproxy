#!/usr/bin/env bash
set -e

if [ $# -eq 1 ]; then
    if [ $1 = "first" ]; then
        sudo apt install libssl-dev
        ./configure
    else
        echo "No such option: $1"
    fi
fi
make
sudo make install

sudo sed -ie "s/#.*/.*/" /usr/local/share/ffproxy/db/access.ip
sudo sed -i -e "s/#bind_ipv4 yes/bind_ipv4 yes/" \
-e "s/#bind_ipv6 no/bind_ipv6 no/" \
-e "s/#port 8080/port 8080/" \
-e "s/#use_ipv6 no/use_ipv6 no/" \
-e "s/#use_syslog yes/use_syslog yes/" \
-e "s/#log_all_requests yes/log_all_requests yes/" \
-e "s/#forward_proxy_ipv6 no/forward_proxy_ipv6 no/" \
-e "s%#db_files_path /usr/local/share/ffproxy%db_files_path /usr/local/share/ffproxy%" \
-e "s/#use_keep_alive no/use_keep_alive no/" /usr/local/etc/ffproxy.conf

echo -e "\nffproxy started."
ffproxy -b