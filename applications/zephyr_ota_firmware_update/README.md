# Zephyr - OTA Firmware Update over BLE #

![Type badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=Type&query=type&color=green)
![Technology badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=Technology&query=technology&color=green)
![License badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=License&query=license&color=green)
![SDK badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=Zephyr%20version&query=zephyr_verion&color=green)
[![Required board](https://img.shields.io/badge/Raspberrypi-Raspberry%20Pi%204-green)](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/)
![Build badge](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_build_status.json)
![Flash badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=Flash&query=flash&color=blue)
![RAM badge](https://img.shields.io/badge/dynamic/json?url=https://raw.githubusercontent.com/SiliconLabs/application_examples_ci/master/zephyr_applications/zephyr_ota_firmware_update_common.json&label=RAM&query=ram&color=blue)

## Summary ##

The vision of this project is to provide developers with clear examples of **OTA firmware updates** over Bluetooth Low Energy (BLE) with the Zephyr OS on various Silicon Labs development kits.

## Hardware Required ##

- [EFR32xG24 Development Kit - DK2601B](https://www.silabs.com/development-tools/wireless/efr32xg24-dev-kit?tab=overview)

- A Linux machine supports the BLE feature or a [Raspberry Pi 4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/) runs Linux.

**NOTE:**
Tested boards for working with this example:

| Board ID | Description  |
| -------- | ------------ |
| BRD2601B | [EFR32xG24 Development Kit - DK2601B](https://www.silabs.com/development-tools/wireless/efr32xg24-dev-kit?tab=overview) |

## Connections Required ##

The following picture shows the system view of how it works.

![hardware_connection](image/hardware_connection.png)

## Setup ##

### Building and flashing the example ###

The example is based on SMP Server example, which implements a Simple Management Protocol (SMP) server. SMP is a basic transfer encoding for use with the MCUmgr management protocol. The management subsystem allows remote management of Zephyr-enabled devices over transports, e.g. Bluetooth Low Energy (BLE), Serial (UART), and UDP over IP. 

This example uses BLE and the image management operation of MCUmgr to update new firmware for Zephyr-enabled devices over BLE.

The below steps describe how to build and run the `smp_svr` sample in Zephyr with `MCUboot` included.

1. Run **Command Prompt** as administrator and active Zephyr environment.

2. Change the current working directory to `~/zephyrproject/zephyr` directory.

3. Build the bootloader project using the commands below with each tested board

   - EFR32xG24 Development Kit - DK2601B: **`west build -p -b xg24_dk2601b --sysbuild samples/subsys/mgmt/mcumgr/smp_svr -- -DCONF_FILE="prj.conf overlay-bt.conf"`**

4. Flash the project to the board using **`west flash`** command.

When flashing the example successfully, the binary file flashed to slot 0 in the flash memory (primary slot). Normally, the bootloader will only run an image from the primary slot, so images must be built such that they can run from that fixed location in the flash.

![example_output](image/example_output.png)

### Installing MCU Manager (MCUmgr) tool ###

MCU Manager (MCUmgr) is the application tool that enables a user to communicate with and manage remote devices running an MCUmgr server.

The management subsystem is based on the Simple Management Protocol (SMP) provided by MCUmgr, an open-source project that provides a management subsystem that is portable across multiple real-time operating systems. For more information about MCUmgr and SMP, please see [this document](https://docs.zephyrproject.org/latest/services/device_mgmt/index.html#device-mgmt).

**Note:**

- The mcumgr command-line tool only works with Bluetooth Low Energy (BLE) on Linux and macOS. On Windows, there is no support for Device Firmware Upgrade over BLE yet.
  
To install the tool, you should follow the steps below:

- Install **go**.

- Run the following commands with the specified **go** version.

  - go version < 1.18 : `go get github.com/apache/mynewt-mcumgr-cli/mcumgr`

  - go version >= 1.18 : `go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest`

For configuring the transport, two command-line options are responsible for setting and configuring the transport layer to use when communicating with the managed device:

- `--conntype` : is used to choose the transport used. In this example, we use BLE transport.

- `--connstring` : is used to pass a comma-separated list of options in the `key=value` format, where each valid `key` depends on the particular `conntype`.

## How It Works ##

You can upload new images to the device. These images will be flashed to slot 1 (secondary slot) in the flash memory. To do that, run the command as below:

    ubuntu@ubuntu:~$ sudo ~/go/bin/mcumgr --conntype ble --connstring 'peer_name=Zephyr' image upload ~/zephyrproject/build/smp_svr/zephyr/zephyr.signed.bin
    283.88 KiB / 283.88 KiB    [============================================================================================================================================] 100.00% 50.06   KiB/s 5s
    Done

After finishing uploading the new image, you can verify that the new image has been written to the secondary slot by using `image list` command to show all of the images which are stored.

    ubuntu@ubuntu:~$ sudo ~/go/bin/mcumgr -r 3 --conntype ble --connstring 'peer_name=Zephyr' image list                    
    Images:
    image=0 slot=0
        version: 0.0.0
        bootable: true
        flags: active confirmed
        hash: 5725f009611c7265b8ec3d7e3c52c6e099f050daa2883cdf33492c66679728e8
    image=0 slot=1
        version: 0.0.0
        bootable: true
        flags:
        hash: 02698326529dc03122c11f6bcc4d7e2dfa8e74584d30d3f56cd4b4bf0d5716c9
    Split status: N/A (0)

By using hash code, MCUboot could clarify each image in the flash memory. If you want to test the new image, you could use `image test` command. We should use `-r (retries)` option to change the number of retries on timeout and make sure that the command run succeeds. The flags will be changed to `pending`, which means the image in the secondary slot should be run once on the next boot.

    ubuntu@ubuntu:~$ sudo ~/go/bin/mcumgr -r 3 --conntype ble --connstring 'peer_name=Zephyr' image test 02698326529dc03122c11f6bcc4d7e2dfa8e74584d30d3f56cd4b4bf0d5716c9
    Images:
    image=0 slot=0
        version: 0.0.0
        bootable: true
        flags: active confirmed
        hash: 5725f009611c7265b8ec3d7e3c52c6e099f050daa2883cdf33492c66679728e8
    image=0 slot=1
        version: 0.0.0
        bootable: true
        flags: pending
        hash: 02698326529dc03122c11f6bcc4d7e2dfa8e74584d30d3f56cd4b4bf0d5716c9
    Split status: N/A (0)

On the next reboot, the device will run the new image. Because of testing, the new firmware runs one time, and then when you reset the device, it swaps to the original image. 

![new_firmware_updated](image/new_firmware_updated.png)

If the new image works as your expectation, you can confirm that it is the image that you want to run on your device using `image confirm` command. After running this command, your device will run the new firmware permanently. Then the flags will be changed to `pending parmenant`, which means the secondary image will run on the next boot and all subsequent boots.

    ubuntu@ubuntu:~$ sudo ~/go/bin/mcumgr -r 3 --write-rsp --conntype ble --connstring 'peer_name=Zephyr' image confirm 02698326529dc03122c11f6bcc4d7e2dfa8e74584d30d3f56cd4b4bf0d5716c9
    Images:
    image=0 slot=0
        version: 0.0.0
        bootable: true
        flags: active confirmed
        hash: 5725f009611c7265b8ec3d7e3c52c6e099f050daa2883cdf33492c66679728e8
    image=0 slot=1
        version: 0.0.0
        bootable: true
        flags: pending permanent
        hash: 02698326529dc03122c11f6bcc4d7e2dfa8e74584d30d3f56cd4b4bf0d5716c9
    Split status: N/A (0)

![new_firmware_parmenant](image/new_firmware_parmenant.png)
