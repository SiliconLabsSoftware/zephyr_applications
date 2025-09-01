<table border="0">
  <tr>
    <td align="left" valign="middle">
    <h1>EFR32 Zephyr Application Examples</h1>
  </td>
  <td align="left" valign="middle">
    <a href="https://www.silabs.com/wireless/bluetooth">
      <img src="http://pages.silabs.com/rs/634-SLU-379/images/WGX-transparent.png"  title="Silicon Labs Wireless Gecko MCUs" alt="EFM32 32-bit Microcontrollers" width="250"/>
    </a>
  </td>
  </tr>
</table>

# Silicon Labs Zephyr Applications #

[![Version Badge](https://img.shields.io/badge/-v2.1.0-green)](https://github.com/SiliconLabsSoftware/zephyr_applications/releases)
![License badge](https://img.shields.io/badge/License-Zlib-green)
![Zephyr badge](https://img.shields.io/badge/Zephyr-4.2.0-green)
![Zephyr SDK badge](https://img.shields.io/badge/Zephyr_SDK-0.17.2-green)

This repository contains example projects that demonstrate various applications running on Zephyr OS supported on Silicon Labs Development Kits.
All examples in this repository are considered to be EXPERIMENTAL QUALITY, which implies that the code provided in the repository has not been formally tested and is provided as-is. It is not suitable for production environments.

## Examples ##

| No | Example name | Link to example |
|:--:|:-------------|:---------------:|
|  1 |Zephyr - MikroBus Demo | [Click Here](./applications/zephyr_mikrobus_demo/)|
|  2 |Zephyr - Si7210 Hall Effect Magnetic Sensor | [Click Here](./applications/zephyr_si7210_onboard_led/)|
|  3 |Zephyr - Qwiic Connector | [Click Here](./applications/zephyr_qwiic_connector/)|
|  4 |Zephyr - Simplicity Connect Demo - Blinky | [Click Here](./applications/zephyr_efr_connect_demo_blinky/)|
|  5 |Zephyr - Modified SoC Thermometer | [Click Here](./applications/zephyr_modified_soc_thermometer/)|
|  6 |Zephyr - SoC Throughput | [Click Here](./applications/zephyr_soc_throughput/)|
|  7 |Zephyr - BTHome v2 - xG24/xG27 Dev Kit Sensors with LVGL | [Click Here](./applications/zephyr_bthome_v2/)|
|  8 |Zephyr - OTA Firmware Update over BLE | [Click Here](./applications/zephyr_ota_firmware_update/)|
|  9 |Zephyr - ST7789 Display Demo | [Click Here](./applications/zephyr_st7789_demo/)|


## Requirements ##

- [Zephyr 4.2.0](https://github.com/zephyrproject-rtos/zephyr/tree/v4.2.0)
- [Zephyr SDK 0.17.2](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html#zephyr-sdk-version-compatibility)
 

## Setting up environment ##

Follow the getting started guide here: [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) with 2 sections:

1. [Select and Update OS](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#select-and-update-os)

2. [Install dependencies](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies)  
  The current minimum required version for the main dependencies are:

    | Tool | Min. Version |
    |:--:|:---------------:|
    |  [Cmake](https://cmake.org/) | 3.20.5 |
    |  [Python](https://www.python.org/) | 3.10 |
    |  [Devicetree complier](https://www.devicetree.org/) | 1.4.6 |

## Setting up the workspace ##

The application structure is based on the [Zephyr example application](https://github.com/zephyrproject-rtos/example-application). Follow steps below to setup and build

1. Create a python virtual environment and activate that environment.

    - For Windows:
      - To create a python virtual environment on windows, run the following commands by using Batchfile or Powershell

        | Command | Batchfile | Powershell |
        | --- | --- | --- |
        | Goto user home directory | `cd %HOMEPATH%` | `cd $Env:HOMEPATH` |
        | Create virtual environment | `python -m venv zephyrproject\.venv` |  `python -m venv zephyrproject\.venv` |
        | Active virtual environment | `zephyrproject\.venv\Scripts\activate.bat` | `zephyrproject\.venv\Scripts\activate.ps1` |

    - For Ubuntu:
      - `python3 -m venv ~/zephyrproject/.venv`
      - `source ~/zephyrproject/.venv/bin/activate`

    - For MacOS:
      - `python3 -m venv ~/zephyrproject/.venv`
      - `source ~/zephyrproject/.venv/bin/activate`

2. Install **west**.

   `pip install west`

3. Clone the Zephyr applications repository of Silicon Labs.

   `cd zephyrproject`

   `git clone https://github.com/SiliconLabs/zephyr_applications.git`

4. Initialize the workspace by running the following commands.

   `west init -l zephyr_applications`

   `west update`

5. Install additional Python dependencies.

   `pip install -r zephyr/scripts/requirements.txt`

6. Install the Zephyr SDK
  
    `cd ~/zephyrproject/zephyr`  
    `west sdk install --version 0.17.2`

7. Fetch and store the Silicon Labs pre-built libraries

   `west blobs fetch hal_silabs`

8. Build & running the example application

The example applications is located at `applications` folder. Use west command to build and flash to the board.

- Build:

  `west build -p -b <board_id> <application_path>`

> [!IMPORTANT]
>
> **Ensure You're Using Zephyr SDK 0.17.2**  
> If you have multiple versions of the Zephyr SDK installed, make sure to use version 0.17.2 when building examples. You can do this by explicitly setting the ZEPHYR_SDK_INSTALL_DIR environment variable
>
> - macOS / Ubuntu: `export ZEPHYR_SDK_INSTALL_DIR=path/to/zephyr-sdk-0.17.2`
> - Windows Command Line: `set ZEPHYR_SDK_INSTALL_DIR=path\to\zephyr-sdk-0.17.2`  
> - PowerShell: `$env:ZEPHYR_SDK_INSTALL_DIR = "path\to\zephyr-sdk-0.17.2"`
>
> For more detailed instructions on how to set environment variables, please visit: [Developing with Zephyr » Environment Variables](https://docs.zephyrproject.org/latest/develop/env_vars.html#env-vars-zephyrrc)  
> You can verify this by checking the build output log to confirm which Zephyr SDK version is being used.  
> `-- Found host-tools: zephyr 0.17.2 (/path/to/zephyr-sdk-0.17.2)`

- Flash:

  `west flash`

> [!TIP]
>
> - Refer to the example application documentation (the README file) to get more details on how to build and run each example applications.
> - For more information regarding setting up the environment, please refer to [this guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) on Zephyr's homepage.
> - We would advise using the Python version 3.10.

## How to make your own application ##

The following guides are relevant to how to be more familiar with Zephyr OS and make a simple application.

### Getting started with basic examples ###

You can start with a variety of examples in the `zephyrproject/zephyr/sample` directory. It might be best to approach Zephyr and understand how it works.

The typical example outline as below:

```txt
  +--boards                       > Contains devicetree overlay files for each supported boards.
  +--build                        > Contains the binary and system files of the example.
  +--src                          > Contains source code files of the example.
  +--CmakeLists.txt               > Contains a set of directives and instructions describing the project's source files and targets
  +--Kconfig                      > Configure the Zephyr kernel and subsystems at build time to adapt them for specific application and platform needs.
  +--prj.config                   > This is a Kconfig fragment that specifies application-specific values for one or more Kconfig options.
  +--README.md                   > Readme file of the project.
```

If you want to develop your project, you should focus on some files that specify your projects, like the devicetree overlay file, CmakeLists.txt, Kconfig, and prj.conf.

### Build the application ###

The `build` command helps you build Zephyr applications from the source. The easiest way is to go to your application’s root directory (i.e. the folder containing the application’s CMakeLists.txt file) and then run the following command:

`west build -b <BOARD_ID>`

In case, you don't know the exact board's ID, you can get the supported board list using the `west boards` command.

We recommend setting the application source directory explicitly and giving its path as a positional argument:

`west build -b <BOARD_ID> <zephyr_applications/applications/...>`

For further information related to building the application, see [this section](https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html#building-west-build) for more details.

### Flash the application ###

> [!IMPORTANT]
>
> **If you are using the Arduino Nano Matter board:**
>
> Refer to [this guide](./README.OpenOCD.md) for instructions on compiling, installing, and running OpenOCD for flashing and debugging.

From a Zephyr build directory, re-build the binary and flash it to your board:

`west flash`

To specify the build directory, you have to use `--build-dir` (or `-d`):

`west flash --build-dir path/to/build/directory`

If you don’t specify the build directory, `west flash` searches for one in the `build` directory in your current working directory.

> [!NOTE]
>
> To flash the binary to the board, we advise installing and using the **SEGGER RTT J-Link** driver. You can download it [here](https://www.segger.com/downloads/jlink/).

### Logging data ##

You can launch Console, which is integrated into Simplicity Studio or use a third-party terminal tool such as Putty, TeraTerm or similar tools.

## References ##

Getting started with the [Developing with Zephyr](https://docs.zephyrproject.org/latest/develop/index.html) section on Zephyr's homepage.

For more information, visit the [Developer documentation](https://docs.silabs.com/application-examples/latest/) portal of Silicon Laboratories.

## Reporting Bugs/Issues and Posting Questions and Comments ##

To report bugs in the Application Examples projects, please create a new "Issue" in the "Issues" section of this repo. Please reference the board, project, and source files associated with the bug, and reference line numbers. If you are proposing a fix, also include information on the proposed fix. Since these examples are provided as-is, there is no guarantee that these examples will be updated to fix these issues.

Questions and comments related to these examples should be made by creating a new "Issue" in the "Issues" section of this repo.
