This README.md is out of date, please ignore
============================================

The full documentation for this example is [available on the ARM documentation site](https://cloud.mbed.com/docs/v1.2/mbed-cloud-tutorials/getting-started-with-mbed-cloud-client.html)

Basic Instructions For This C030 Hack
=====================================
1.  Clone this repo and `CD` to it.
2.  Run `git checkout ublox_c030_support`.
3.  Run `mbed deploy`.
4.  Replace the file `easy-connect.h` in the `easy-connect` directory with the file `easy-connect.h - REPLACE THE ONE IN easy-connect WITH THIS ONE`.
5.  Run `mbed target UBLOX_C030_U201` and `mbed toolchain GCC_ARM` (or whatever toolset you use).
6.  Navigate to https://cloud.mbed.com/docs/v1.2/mbed-cloud-tutorials/getting-started-with-mbed-cloud-client.html and work out how to download your specific developer credentials file, `mbed_cloud_dev_credentials.c`, replacing the empty one in the root of this build with it.
7.  Run `mbed compile`.
8.  Download the resulting build to your C030 board and it should register with the network and begin doing stuff.  If you attach a serial terminal running at 115200 you should see something like:

```
Starting example client
Start simple mbed Cloud Client
Start developer flow
Developer credentials already exists
[EasyConnect] IPv4 mode
[EasyConnect] Connecting using Cellular interface with default APN
[EasyConnect] Connected to Network successfully
[EasyConnect] IP address 10.123.62.106
[EasyConnect] Cellular, so no MAC address

Client registered

Endpoint Name: 015cc575ddf70000000000010010022c
Device Id: 015cc575ddf70000000000010010022c
increment resource value, new value of resource is 1
increment resource value, new value of resource is 2
increment resource value, new value of resource is 3
increment resource value, new value of resource is 4
...
```

Adding Firmware Update Over The Air To This C030 Hack
=====================================================
First, navigate to the `mbed-os\targets\TARGET_STM\TARGET_STM32F4\TARGET_STM32F437xG\device` directory and edit the linker files in the relevant toolchain sub-directory such that `MBED_APP_START 0x08000000` becomes `MBED_APP_START 0x08020400`.

Then follow the instructions at https://cloud.mbed.com/docs/v1.2/mbed-cloud-tutorials/getting-started-with-mbed-cloud-client.html#firmware-update but using `mbed-bootloader-ublox-c030.bin` as your bootloader binary (and there's no need for that diff application stuff since you have manually changed the relevant line above).  When you download the initial combined OTA-updateable image, you should see something like the following on your serial terminal:

```
[BOOT] Active firmware integrity check:

[BOOT] [++++                                                                  ]
[BOOT] [+++++++                                                               ]
[BOOT] [+++++++++++                                                           ]
[BOOT] [++++++++++++++                                                        ]
[BOOT] [++++++++++++++++++                                                    ]
[BOOT] [+++++++++++++++++++++                                                 ]
[BOOT] [++++++++++++++++++++++++                                              ]
[BOOT] [++++++++++++++++++++++++++++                                          ]
[BOOT] [+++++++++++++++++++++++++++++++                                       ]
[BOOT] [+++++++++++++++++++++++++++++++++++                                   ]
[BOOT] [++++++++++++++++++++++++++++++++++++++                                ]
[BOOT] [+++++++++++++++++++++++++++++++++++++++++                             ]
[BOOT] [+++++++++++++++++++++++++++++++++++++++++++++                         ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++                      ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++                  ]
[BOOT] [+++++++++++++++++++++++++++++++++++++++++++++++++++++++               ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++            ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++        ]
[BOOT] [+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++     ]
[BOOT] [+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] SHA256: 42C0012DAB0A1EB7FAC343A5F2724AEBADECB2EFBD03B6264C995D8C31E532E4
[BOOT] Version: 1499254444
[BOOT] Slot 0 firmware is of older date
[BOOT] Version: 1499083538
[BOOT] Active firmware up-to-date
[BOOT] Application's start address: 0x8020400
[BOOT] Application's jump address: 0x8051C25
[BOOT] Application's stack address: 0x20030000
[BOOT] Forwarding to application...
ðStarting example client
Start simple mbed Cloud Client
Start developer flow
Developer credentials already exists
[EasyConnect] IPv4 mode
[EasyConnect] Connecting using Cellular interface with default APN
[EasyConnect] Connected to Network successfully
[EasyConnect] IP address 10.123.171.97
[EasyConnect] Cellular, so no MAC address

Client registered

Endpoint Name: 015cc575ddf70000000000010010022c
Device Id: 015cc575ddf70000000000010010022c
increment resource value, new value of resource is 1
increment resource value, new value of resource is 2
increment resource value, new value of resource is 3
increment resource value, new value of resource is 4
...
```

...and, when you have completed the steps required to download a new firmware image and begun an update campaign for this device, the serial terminal will show something like the following (the single change here was to change the `printf()` "Starting example client" in `main.cpp` to be "Starting example client UPDATED!"):

```
Firmware download requested
Authorization granted

Downloading: [-                                                 ] 0 %
Downloading: [\                                                 ] 0 %
Downloading: [|                                                 ] 0 %
Downloading: [/                                                 ] 1 %
...
Downloading: [+++++++++++++++++++++++++++++++++++++++++++++++++|] 98 %
Downloading: [+++++++++++++++++++++++++++++++++++++++++++++++++/] 99 %
Downloading: [+++++++++++++++++++++++++++++++++++++++++++++++++-] 99 %
Downloading: [+++++++++++++++++++++++++++++++++++++++++++++++++\] 99 %
Downloading: [++++++++++++++++++++++++++++++++++++++++++++++++++] 100 %
Download completed
Firmware install requested
Authorization granted

[BOOT] Active firmware integrity check:

[BOOT] [++++                                                                  ]
[BOOT] [+++++++                                                               ]
...
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] SHA256: A4D18DB585C580AE96D7E7948CF0F0FE9B85B5CF2CCF75F6C9D8F69DE6FF9B68
[BOOT] Version: 1499261818
[BOOT] Slot 0 firmware integrity check:

[BOOT] [++++                                                                  ]
[BOOT] [+++++++                                                               ]
...
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] SHA256: 593D105CAA957B7031B2AC9F6E8F1AE08CCF3FC547EB9F045AB9F73297D4B0A4
[BOOT] Version: 1499261925
[BOOT] Update active firmware using slot 0:

[BOOT] [+                                                                     ]
[BOOT] [+                                                                     ]
[BOOT] [+                                                                     ]
[BOOT] [+                                                                     ]
[BOOT] [++                                                                    ]
...
[BOOT] [+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] Verify new active firmware:

[BOOT] [++++                                                                  ]
[BOOT] [+++++++                                                               ]
...
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++  ]
[BOOT] [++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++]
[BOOT] New active firmware is valid
[BOOT] Application's start address: 0x8020400
[BOOT] Application's jump address: 0x8051C25
[BOOT] Application's stack address: 0x20030000
[BOOT] Forwarding to application...
ðStarting example client UPDATED!
Start simple mbed Cloud Client
Start developer flow
Developer credentials already exists
[EasyConnect] IPv4 mode
[EasyConnect] Connecting using Cellular interface with default APN
[EasyConnect] Connected to Network successfully
[EasyConnect] IP address 10.123.169.248
[EasyConnect] Cellular, so no MAC address

Client registered

Endpoint Name: 015cc575ddf70000000000010010022c
Device Id: 015cc575ddf70000000000010010022c
increment resource value, new value of resource is 1
...
```
