---
layout: default
title: 📶 connection
nav_order: 7
description: "E-TKT"
parent: 🛠️ do it yourself!
---

# 📶 **connection**

----

The device is worked from an app that it serves itself, to the browser of a phone, a tablet or a computer.
There are two ways to that app: over a 2.4GHz Wireless Local Area Network that the device joins, or over the device's own network.
Neither needs the internet.
The device does not wait for either: it starts, shows how to reach it, and prints from its button with no network at all.

## 1. join the device's own network

- turn it on;
- a device that has not been given a network opens its own right away, named "E-TKT-" and four characters that tell it from the next one, such as "E-TKT-9C4F";
- the screen shows that name, the password of the network, and a QR code;
- scan the QR code with the phone's camera to join, or pick the network in the phone's wifi settings and type the password;
- the phone may say that the network has no internet, which is true: stay on it.

## 2. open the app

- once a phone has joined, the screen shows the address 192.168.4.1 and a QR code for it;
- scan that code, or type http://192.168.4.1 in the browser;
- no page opens by itself as it did before, because there is no captive portal: the app is all there is;
- after two minutes the screen goes back to the code that joins the network, which is what the next phone needs, and the address stays the same.

## 3. give it a network, or leave it on its own

- in the app, open "Setup" and look for "Network";
- "Add network" lists the networks the device hears: pick one, type its password, and add it;
- the device remembers up to four networks, and tries them in turn until one lets it on;
- "Reached over" says which way the device is set up: "A network", or "Its own";
- where the wifi cannot be relied on, such as a basement or a hall full of phones, choose "Its own": the device then joins nothing, and its own network is always open.

## 4. success, now scan the QR code or manually access the URL to open the app

![image](https://user-images.githubusercontent.com/15098003/196287118-b797de82-fde9-4ebf-82c0-716149194815.png)

- once the device is on a network, the screen shows that network, the device's address there, and a QR code that opens the app from anything on the same network;
- the device also answers to a name of its own, such as http://e-tkt-9c4f.local, where the browser knows such names;
- its own network closes a minute after it has joined another, once nobody is on it;
- its own network opens again when the device has been a minute without a network, so there is always a way in;
- a network the device could not join is named in the app under "Network", with the reason, as far as the device can tell it.

----

## to make the device forget its networks

![_DSC0729](https://user-images.githubusercontent.com/15098003/196286421-695d4b6f-33fd-4a2d-a6f5-8c169c958e76.jpg)

![image](https://user-images.githubusercontent.com/15098003/196287056-4af73b3e-97f8-49bd-8c57-238fa931bda6.png)

One network can be forgotten in the app, under "Network".
The button is for when the app cannot be reached, or when the device changes hands.

- press and hold the physical "WI-FI" button on the device;
- either turn the device off and on again, or press reset;
- keep holding through the splash screen and until the screen confirms that the wi-fi has been cleared;
- release the "WI-FI" button;
- the device restarts with no network remembered, and with a new password on its own network, which the screen shows.

## the "WI-FI" button the rest of the time

Once the device is running, the same button works it without a phone.

- while the device prints, a press stops it;
- while it waits, a press prints the last run of labels again;
- while it waits, holding the button for a second and a half unloads the roll, and the screen then asks for a new one: put it in, and press the button to load it.
