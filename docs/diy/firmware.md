---
layout: default
title: 💻 firmware
nav_order: 5
description: "E-TKT"
parent: 🛠️ do it yourself!
---

# 💻 **firmware**

----

💬 *if you want to know more about the code, please check the [firmware](https://github.com/andreisperid/E-TKT/blob/main/src/LabelMaker.cpp) and the [app scripts](https://github.com/andreisperid/E-TKT/blob/main/data/script.js)*.

----

![_DSC0743](https://user-images.githubusercontent.com/15098003/196303763-939a3349-6e17-4484-b9b3-b4690aa199b1.jpg)

----

1. Clone the [repository](https://github.com/andreisperid/E-TKT);
2. Make sure you have the [framework and all the libraries](https://andreisperid.github.io/E-TKT/credits/libraries.html) installed on your computer (I use [Visual Studio Code](https://code.visualstudio.com/) with [PlatformIO](https://platformio.org/) and recommend it!), and [Node.js](https://nodejs.org/), which the build checks the app's script with;
3. Flash the code into the PCB using an USB-C cable \*.

*\* don't forget to hold the \*FN button to enter DFU mode*

The app (the files in the "data" folder) is part of the firmware.
The build puts it in, so there is nothing more to upload, and the app a machine serves is always the one its firmware was built with.
A machine that has a firmware from before the app was part of it needs this one flashed over USB, not over the network.
The firmware is larger with the app in it, and only a flash over USB divides the machine's flash again to make the room.
The machine keeps its calibration, its roll and its networks.
Networks are not part of the upload: they are given to the device afterwards, in the app, as the 📶 connection page says.


