# The ffproxy improving project in the PBL-A course, 2021
[![Open in Visual Studio Code](https://open.vscode.dev/badges/open-in-vscode.svg)](https://open.vscode.dev/watamario15/5_PBL-A)

Description
===========

ffproxy is a filtering HTTP/HTTPS proxy server. It is able to filter by host,
URL, and header.  Custom header entries can be filtered and added.
It can even drop its privileges and optionally chroot() to some directory.
Logging to syslog() is supported, as is using another auxiliary proxy server.
An HTTP accelerator feature (acting as a front-end to an HTTP server) is
included.  Contacting IPv6 servers as well as binding to IPv6 is supported
and allows transparent IPv6 over IPv4 browsing (and vice versa).

Website:    http://faith.eu.org/programs.html

Build & Run
===========

```sh
# cd /where/you/want/to/put/everything/in
git clone https://github.com/watamario15/5_PBL-A.git
cd 5_PBL-A
./build.sh first
```

After the first build, you can speed up the build process by simply running `./build.sh` with no options. This excludes the execution of `./configure`.