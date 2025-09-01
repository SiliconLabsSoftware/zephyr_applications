# Set up OpenOCD for the Arduino Matter board #

## Install Dependencies ##

> [!IMPORTANT]
> Install the following packages one by one, check if installation was successful and then proceed to the next package. Resolve reported problems before moving to the next step.

### For Windows Users ###

You can build OpenOCD for Windows natively with either MinGW-w64/MSYS
or Cygwin. Lauch the **MSYS2 MINGW64** shell and run this command to update MSYS2 and all packages:

```bash
pacman -Syu
```

Install the packages required to compile OpenOCD:

```bash
pacman -S mingw-w64-x86_64-libtool
pacman -S autoconf
pacman -S automake
pacman -S texinfo
pacman -S pkg-config
pacman -S mingw-w64-x86_64-libusb
```

### For Linux Users ###

Install the packages required to compile OpenOCD:

```bash
sudo apt-get install make
sudo apt-get install libtool
sudo apt-get install pkg-config
sudo apt-get install autoconf
sudo apt-get install automake
sudo apt-get install texinfo
sudo apt-get install libusb-1.0
```

## Download Sources of OpenOCD ##

The Arduino Nano Matter contains an SAMD11 with **CMSIS-DAP**, allowing flashing, debugging, logging, etc. over the USB port. Doing so requires a version of OpenOCD that includes support for the flash on the MG24 MCU, rather than the **SEGGER RTT J-Link**.

Until those changes are merged into the mainline OpenOCD, you can either use the version bundled with Arduino or install the specific branch from the OpenOCD Arduino fork. To do so, clone the repository and check out the **`arduino-0.12.0-rtx5`** branch:

```bash
# Clone the OpenOCD Arduino fork
  git clone https://github.com/facchinm/OpenOCD.git

# Navigate into the cloned repository
  cd OpenOCD

# Check out the specific branch
  git checkout arduino-0.12.0-rtx5
```

## Build OpenOCD ##

Proceed to configure and build OpenOCD:

```bash
./bootstrap
./configure --enable-cmsis-dap-v2 --disable-werror
make
make install
```

The build process may take some time, depending on your computer.

Once the `make` process is successfully completed, use this command to check where the OpenOCD executable is located:

```bash
$ which openocd
/<your-installation-location>/bin/openocd
```

> [!NOTE]
> When building OpenOCD from source, the default installation locations for the compiled binaries and associated files are typically as follows:
>
> | Platform      | Executables  | Configuration scripts and interface definitions      |
> | --------      | ------------ | ---------------------------------------------------- |
> | Linux         | `/usr/local/bin`        | `/usr/local/share/openocd/scripts`        |
> | Windows 10/11 | `/c/msys64/mingw64/bin` | `/c/msys64/mingw64/share/openocd/scripts` |
>
> For more information regarding the installation of OpenOCD , please refer to [this guide](https://github.com/facchinm/OpenOCD/blob/arduino-0.12.0-rtx5/README) on the OpenOCD's homepage.

## Run OpenOCD with `west flash` ##

When flashing or debugging, you may need to specify to `west` the locations of OpenOCD and its scripts.

**Linux Example**

```bash
west flash \
  --openocd=/<your-installation-location>/local/bin/openocd \
  --openocd-search=/<your-installation-location>/share/openocd/scripts/
```

For Linux users, you must also specify the OpenOCD binary explicitly with the `--openocd` option to override the version included in the Zephyr SDK.

**Windows Example**

```bash
west flash ^
  --openocd=<your-installation-location>/bin/openocd ^
  --openocd-search=<your-installation-location>/share/openocd/scripts/
```
