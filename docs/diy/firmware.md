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
2. Make sure you have the [framework and all the libraries](https://andreisperid.github.io/E-TKT/credits/libraries.html) installed on your computer (I use [Visual Studio Code](https://code.visualstudio.com/) with [PlatformIO](https://platformio.org/) and recommend it!);
3. Flash the code into the PCB using an USB-C cable \*;
4. Upload the files ("data" folder) to the ESP32 using SPIFFS \*.

*\* don't forget to hold the \*FN button to enter DFU mode*

Steps 3 and 4 go together, from the same copy of the repository.
The app is the only place the device is given a network, so an older app in front of this firmware leaves no way to do that, and a newer app in front of an older firmware asks for what is not there.
Networks are not part of either upload: they are given to the device afterwards, in the app, as the 📶 connection page says.


