# Zephyr - SoC - Throughput #

![Type badge](https://img.shields.io/badge/Type-Virtual%20Application-green)
![Technology badge](https://img.shields.io/badge/Technology-Zephyr-green)
![License badge](https://img.shields.io/badge/License-Zlib-green)
![SDK badge](https://img.shields.io/badge/Zephyr%20version-v4.2.0-green)
![Build badge](https://img.shields.io/badge/Build-passing-green)
![Flash badge](https://img.shields.io/badge/Flash-215.71%20KB-blue)
![RAM badge](https://img.shields.io/badge/RAM-31.59%20KB-blue)

## Summary ##

The example demonstrates how the Zephyr OS is supported by the development kits of Silicon Labs. This example allows measuring data throughput between EFR32 devices as well as between an EFR32 and a smartphone running the Simplicity Connect mobile app.

## Zephyr version ##

- [Zephyr 4.2.0](https://github.com/zephyrproject-rtos/zephyr/tree/v4.2.0)

## Hardware Required ##

- 1x [Bluetooth Low Energy Development Kit](https://www.silabs.com/development-tools/wireless/bluetooth). Tested boards for working with this example:

   | Board ID | Description  |
   | -------- | ------------ |
   | BRD2601B | [xG24-DK2601B](https://www.silabs.com/development-tools/wireless/efr32xg24-dev-kit?tab=overview) EFR32xG24 Dev Kit |
   | ABX00112 | [Arduino Nano Matter](https://store.arduino.cc/products/nano-matter) Arduino Nano Matter Board |

- 1x Smartphone runs the Simplicity Connect Mobile App

## Connections Required ##

Depending on the use case, one or two Silicon Labs kits will be needed. Devices transmit data to the EFR Dev Kit via Bluetooth Low Energy (BLE).

![connection](image/connection.png)

## Setup ##

To run the example, you should follow the below steps:

1. Run **Command Prompt** as administrator, initialize the workspace for the project and download the required package, please refer to the [setting up environment](../../README.md#setting-up-environment) section.

2. Change the current working directory to the `zephyrproject` directory using the `cd` command.

3. Build this project by the following commands with each tested board.

   - EFR32xG24 Dev Kit - BRD2601B: **`west build -p -b xg24_dk2601b zephyr_applications/applications/zephyr_soc_throughput`**
   - Arduino Nano Matter Board - ABX00112: **`west build -p -b arduino_nano_matter zephyr_applications/applications/zephyr_efr_connect_demo_blinky`**

4. Flash the project to the board using **`west flash`** command.

> [!NOTE]
>
> - Make sure that the Zephyr OS environment has already been installed. For installing the Zephyr OS environment, you can refer to [this guide](../../README.md#setting-up-environment).
>
> - You need to install the SEGGER RTT J-Link driver to flash this project to the board. For further information, please refer to [this section](../../README.md#flash-the-application).
>
> - If the Arduino Nano Matter board is being used, you need to install the OpenOCD tool to flash this project to the target board. For further information, please refer to [this guide](../../README.OpenOCD.md) for instructions on compiling, installing, and running OpenOCD for flashing and debugging.

## How It Works ##

This example implements a GATT service that can be used to measure application data throughput using acknowledged or unacknowledged transactions, namely indications or notifications.

This throughput measurement setup has two use cases:

1. EFR32 (SoC) <-> Mobile phone + Simplicity Connect
2. EFR32 (SoC) <-> EFR32 (SoC)

### EFR32 (SoC) <-> Mobile phone + Simplicity Connect ###

In this use case, only one developer kit is required. After flashing the example code to the board, the firmware boots as a peripheral device by default, and the mobile phone acts as a central device.

Open Simplicity Connect application, then navigate to the Demo view and select the item Throughput. After clicking of the icon, a pop-up appears, which shows all the devices that are running the **Bluetooth - SoC Throughput** firmware. Select the Throughput Test option from the Device List.

![Demo view](image/demo_view.png) ![Pop up](image/pop_up.png)

Data can be pushed to the smartphone by pressing PB0 on the mainboard. The behavior is determined by the button press duration:

- Short press switches between sending notifications or indications. The default out of boot is sending notifications.
- Long press is used to send data.
- To boot into central mode instead of peripheral mode, keep the button pressed as you reset the device.

From the mobile application, data can be pushed to the EFR32 board by tapping the Start button at the bottom of the screen. The data transfer type (notifications/indications) can be selected at the top of the connection parameters window.

![Throughput demo](image/throughput_demo.png)

The below animation demonstrates how to transfer data by long-pressing the button of the Dev Kit. Data is sent from an EFR32xG24 Dev Kit to the Simplicity Connect Mobile application.

![Throughput demo animation](image/demo.gif)

### EFR32 (SoC) <-> EFR32 (SoC) ###

This device setup requires two developer kits, which can exchange data via BLE.

In this this use case, the **Bluetooth - SoC Throughput** example must be flashed on both dev kits. By default, the firmware boots in peripheral mode, however it can boot into central mode by keeping PB0 pressed, while resetting the device. The role can be read from the first line on the display.

![Kits booted](image/kit_boot1.png)

![Kits booted](image/kit_boot1.png)

Throughput testing can start as soon as the devices establish a connection and subscribe to indications/notifications on each other's GATT database. The status is indicated on the second line of the display. When both side show "ST: Subscribed" then testing is ready to start.

Notifications can be pushed by pressing PB0. The data can be sent from either device since both have the throughput service on their GATT database, and they subscribe to notifications/indications on the GATT of the remote device.

Data is sent as long as the button is pressed on the device, and the status is "ST: Testing" during that time.

![Kits status](image/kit_status.png)

Once the button is released, the average throughput is calculated and shown on the display, as well as, how many packets were sent. This information is visible in the two last lines of the display.

![Kits log](image/kit_log.png)

### CLI (Command Line Interface) ###

The **Bluetooth - SoC Throughput** example has a CLI that is used to report activity as well as to modify parameters in runtime. You can easily access the CLI with a terminal emulator such as Tera Term or from [Simplicity Studio](https://www.silabs.com/developers/simplicity-studio) by right-clicking the debug adapter and selecting Launch Console. On the Serial 1 tab, type "help" on the command prompt.

 ![CLI](image/cli.png)

The available command set allows multiple options such as changing connection parameters, MTU size, TX power, among others.

On devices without display, the CLI is used for printing a virtual display, which mimics the physical display on the main board.

 ![Log](image/log.png)
