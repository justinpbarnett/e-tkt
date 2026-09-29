---
layout: default
title: ⏱️ calibration
nav_order: 9
description: "E-TKT"
parent: 🛠️ do it yourself!
---

# ⏱️ **calibration**

----

This final part of the process will make sure the letter alignment is finely adjusted, and together with the correct imprinting pressure.

Only when fully assembled and revised, follow the instructions to [connect the machine](https://andreisperid.github.io/E-TKT/diy/connection.html) and add a [new reel](https://andreisperid.github.io/E-TKT/diy/reel.html);

----

## **<< attention is needed here, please >>**

- reboot your E-TKT;
- after the splash screen the machine will home by rotating until the magnet meets the hall sensor;
- check the letter "J" position in the top window: if the letter is visually centered (right image), then proceed to the step 4.

<img src="https://user-images.githubusercontent.com/15098003/196271237-0aad831a-ca98-495b-8739-150a3f19cbf9.jpg" width="50%"><img src="https://user-images.githubusercontent.com/15098003/196271232-4fec3785-aaef-469e-9138-371bef4d8894.jpg" width="50%">


***<details><summary> ⚠️ click here in case the letter "J" does not meet the above criteria, otherwise move on </summary>***  
  
![_DSC0676_arrow](https://user-images.githubusercontent.com/15098003/196274788-680d20cc-d54b-45de-8b5a-a0055486766a.jpg)

- **<< TURN OFF the device >>** otherwise you might short circuit the hall sensor terminals;
- delicately adjust the hall sensor position following the tangent (cyan) axis in the image:
  - if "J" is too much to the right, push the hall sensor inward the device (yellow);
  - if "J" is too much to the left, pull the hall sensor outward the device (magenta);
- make sure to keep the same distance on the transversal (red) axis;
- only when the letter "J" is visually centered in the top window, you might proceed to the next step;

Also, when turning the machine on, the press angle should be at 2mm from the I_nema_wheel_hub. If not, then you might need to go back to the [press assembly procedure](https://andreisperid.github.io/E-TKT/diy/assembly/04_servo.html).
   
</details> 

----

## fine tuning

In the app, click on "SETUP".

The "Calibration" card holds the two settings, each with a test of its own, and we will start with the first: the alignment.

<img src="../assets/app/setup.png" width="50%" alt="The Setup view, with the align and force settings under Calibration">

### Alignment

![_DSC0678](https://user-images.githubusercontent.com/15098003/196290213-a086a727-1584-46e7-9f97-4fc3b931565e.jpg)
*The image above shows the exact moment the press is moved to the tooth.*

- the align values ranges from 1 to 9, having 5 as a default (middle).
- now click the "TEST ALIGN" button to the right side of "align";
- the machine will slowly and lightly press the daisy wheel letter, an "M";
- the goal is to have the press to be fully aligned with the character, that should also be right in the middle of the character window;
- adjust the values and test to make sure it is alright;

### Force

![_DSC0730](https://user-images.githubusercontent.com/15098003/196290623-2462133b-b84c-46e9-a5ca-e81a04f28c05.jpg)
*The image above shows several tests with progressive "force" values, and it is noticeable that up from a certain point there is no real difference.*

- now we will adjust the peak servo angle, that will determine if the letter will be lightly or heavily pressed against the tape;
- the default value is 1 in a scale from 1 to 9;
- increase it one at a time and hit the "TEST PRINT" button, which prints and cuts an "E-TKT" label;
- take the opportunity to see if the alignment is correct, and if not adjust it too;

---

### 🕐 when you are happy with the result, click "SAVE", then "SAVE AND RESTART", and wait for the machine to reboot.
<br>

# 🎉 **you are now ready to label around!** 🎉

<br>
