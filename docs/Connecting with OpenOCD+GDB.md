ls**Setting up Raspberry Pi:**

SSH into the raspberry Pi:
```
ssh 192.168.raspberry.pi
```

Clone the OpenOCD repository and compile, install (this is just for getting interface configs):
```
git clone http://openocd.zylin.com/openocd
./bootstrap
./configure --enable-sysfsgpio --enable-bcm2835gpio 
make
sudo make install
```
The configs are located in /usr/local/share/openocd/scripts/interface
Should be fine to uninstall the newly installed binary?

Actually download the compiled binaries because can't be bothered compiling (this didn't install configs for me for some reason thus the previous step):
```
Debian: apt install openocd
```

Clone the Project repository:
```
git clone https://github.com/archiecarrot123/osborne-floppy-emulator.git
```
Setup is done.

**Running Raspberry Pi:**

SSH into the Raspberry Pi and port forward:
```
ssh raspi.ip -L 3333:localhost:3333
```

Run OpenOCD with the configuration in the repos:
```
openocd --file osborne-floppy-emulator/docs/openocd.cfg
```

It should say something like this:
```
Open On-Chip Debugger 0.12.0  
Licensed under GNU GPL v2  
For bug reports, read  
       http://openocd.org/doc/doxygen/bugs.html  
Warn : TMS/SWDIO moved to GPIO 8 (pin 24). Check the wiring please!  
Info : Hardware thread awareness created  
Info : Hardware thread awareness created  
Info : Listening on port 6666 for tcl connections  
Info : Listening on port 4444 for telnet connections  
Info : BCM2835 GPIO JTAG/SWD bitbang driver  
Info : clock speed 4061 kHz  
Info : SWD DPIDR 0x0bc12477, DLPIDR 0x00000001  
Info : SWD DPIDR 0x0bc12477, DLPIDR 0x10000001  
Info : [rp2040.core0] Cortex-M0+ r0p1 processor detected  
Info : [rp2040.core0] target has 4 breakpoints, 2 watchpoints  
Info : [rp2040.core1] Cortex-M0+ r0p1 processor detected  
Info : [rp2040.core1] target has 4 breakpoints, 2 watchpoints  
Info : starting gdb server for rp2040.core0 on 3333  
Info : Listening on port 3333 for gdb connections
```
**Compiling the binary**
Back on your host machine make sure you /have the repo

Install CMake and the arm gcc toolchain:
```
Debian: apt install CMake gcc-arm-none-eabi
```

Add subrepositories:
```
git submodule init
git submodule update --recursive
```
once from the base of the repo once then from inside pico-sdk

Run CMake **on the root of the project**:
```
cmake -s .
```
(you can use --fresh if you already have some files from previous attempts)

```
make
```

**Connecting to Raspberry Pi via GDB:**

Install GDB:
```
Debian: apt instal gdb
```

```
gdb src/floppy.elf -ex "tar ext :3333"
```
