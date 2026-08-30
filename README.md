<div align="center">

<img width="1448" height="844" src="https://github.com/user-attachments/assets/e935a1c7-a0e7-48de-a2c1-1c4329da1baf" />


# sendspin-guition

Sendspin player firmware for Guition devices with Line Out.

(and other devices where you can easily ADD a Line Out)

Turn any amplifier, active speaker, or existing hi-fi setup into part of a perfectly synchronized multi-room audio system. With microsecond-precise timing, automatic local discovery, encrypted pairing, and support for mixed brands, it delivers “one song, every room” without cloud lock-in, accounts, or Big Tech telemetry.

Not many esp32 devices include Line Out, but Guition has made two: the 1.80" silver puck and a new version of the 1.85" rotary dial knob. These devices are perfect for Sendspin players because they support Line Out through the 3.5 mm mini-jack connector.

There are two versions available in this project:

**Simple Player** – A basic player example that others can use as a reference or as a starting point for their own projects.

**Modular** – A more advanced version featuring two voice assistants and a Sendspin player.

Both versions can be flashed directly from the project webpage for users who simply want everything to work without having to compile the firmware themselves.

---

NEW!: Added the bigger 7" display from guition (JC1060P470C-I-W-Y) to the project because it's "almost" DIY free like the others, the DAC module does require soldering but thats it. 

NEW: Added Waveshare's 3.5" with Line Out DAC

### More devices [here](Other/)

</div>

---

## Installation:

Either flash directly from the webpage using the links below, or if manually installing remember to keep your name and friendly_name:

```
substitutions:
  name: esphome-web-e9f624
  friendly_name: Xiaozhi Taichi Pi v1
```

And change name_add_mac_suffix to false:

```
esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  min_version: 2026.8.0
  name_add_mac_suffix: false <-------- THIS
  project:
    name: realdeco.sendspin_guition
    version: "2026.8.0"
```

Then compile in Esphome Builder.

---

## Guition 1.80" Silver Puck

<div align="center">

### Flash Firmware

For your convenience, you can flash the device directly from this webpage. Click your version below to get started.

<a href="https://realdeco.github.io/sendspin-guition/Guition_Silver_Puck_v1/index.html">
  <strong>Guition Silver Puck v1</strong>
</a>
<br>
<a href="https://realdeco.github.io/sendspin-guition/Guition_Silver_Puck_v2/index.html">
  <strong>Guition Silver Puck v2</strong>
</a>
<br>
<a href="https://realdeco.github.io/sendspin-guition/Guition_Silver_Puck_v1-modular/index.html">
  <strong>Guition Silver Puck v1 Dual VA Modular</strong>
</a>
<br>
<a href="https://realdeco.github.io/sendspin-guition/Guition_Silver_Puck_v2-modular/index.html">
  <strong>Guition Silver Puck v2 Dual VA Modular</strong>
</a>

<br><br>

<img
width="1448"
height="615"
alt="Guition Line Out devices"
src="https://github.com/user-attachments/assets/e6e6a76f-20ce-4229-90d6-5cc22ac82135"
/>

<br>
Identify v1 vs. v2

<img
src="https://github.com/user-attachments/assets/a27cb006-72d2-4488-8219-5e16227eeb8f"
width="350"
alt="Guition 1.80 silver puck photo"
/>

</div>

### Where to buy

Guition 1.80" Silver Puck:
[https://www.aliexpress.com/item/1005007338590852.html](https://www.aliexpress.com/item/1005007338590852.html)

---

## Guition 1.85" Rotary Knob

Beware there is two versions of this, the older more expensive JC3636K518C with dual processor, and the newer JC3636K718C with a single esp32-s3, this is for the new version.

<div align="center">


### Flash Firmware

<a href="https://realdeco.github.io/sendspin-guition/Guition_Knob_v2/index.html">
  <strong>Guition Knob v2</strong>
</a>
<br>
<a href="https://realdeco.github.io/sendspin-guition/Guition_Knob_1.85_v2-VA-modular/index.html">
  <strong>Guition Knob v2 Dual VA Modular</strong>
</a>

<br><br>

<img width="400" src="https://github.com/user-attachments/assets/063362d3-3063-4a42-8740-1f7cea6c51af" />


</div>

### Where to buy (in Red, Blue or Black)

Guition 1.85" Rotary Knob:
[https://www.aliexpress.com/item/1005011771382178.html](https://www.aliexpress.com/item/1005011771382178.html)

## BACK IN STOCK!.

(note: V2 has a LED ring in the middle, v1 does not and v1 does NOT work for this project)

---

## Guition 7"

This device is great for this project but requires to add a external DAC to the expansion port on the back for dual i2s (duplex) with LINE OUT, so there is a "little" soldering involved (soldering the pinheader to the DAC). The device also features a Ethernet port for those that prefer wired network.

<div align="center">


### Flash Firmware

<a href="https://realdeco.github.io/sendspin-guition/Guition_P4_70/index.html">
  <strong>Guition P4 7"</strong>
</a>

<br><br>

<img height="400" src="https://github.com/user-attachments/assets/0e865528-f34c-4929-ae87-7834c4924c05" />
<img height="400" src="https://github.com/user-attachments/assets/5015ee69-e862-4095-ae75-bc54511be341" />
<img height="300" src="https://github.com/user-attachments/assets/4ad47016-ae60-4707-b613-58a21c1b4c97" />

</div>

### Where to buy

Guition 7" 1024x600:
[https://www.aliexpress.com/item/1005010022828767.html](https://www.aliexpress.com/item/1005010022828767.html)

DAC
[https://www.aliexpress.com/item/1005008130629022.html](https://www.aliexpress.com/item/1005008130629022.html)

(DAC comes with male header, for this display you need a female header and only use 5 pins, not all 6)

---

## Waveshare 3.5"

Added this device because i had it, and because it's just as easy to add a DAC for stereo outout and make it a nice little Sendspin Player for the desk.

<div align="center">


### Flash Firmware

<a href="https://realdeco.github.io/sendspin-guition/Waveshare_3.5/index.html">
  <strong>Waveshare 3.5"</strong>
</a>

<br><br>
<img height="300"  src="https://github.com/user-attachments/assets/44811973-4d9e-4f3c-9bcd-4a55bd37e3aa" />
<img height="300"  src="https://github.com/user-attachments/assets/c620f79e-4f62-4bf2-8d6e-1d7a7bf76e22" />

<img height="300" src="https://github.com/user-attachments/assets/422d539c-de26-4fdc-9145-7fd6f576d417" />
<img height="300" src="https://github.com/user-attachments/assets/5272e353-e266-465e-9185-7f9c045fad9f" />


</div>

### Where to buy

Waveshare 3.5c:
[https://www.aliexpress.com/item/1005010597169509.html](https://www.aliexpress.com/item/1005010597169509.html)

DAC
[https://www.aliexpress.com/item/1005008130629022.html](https://www.aliexpress.com/item/1005008130629022.html)

(for this display you need a female header as the DAC come with and only use 5 pins, not all 6)

---

### **How to use** (general for all)

Sendspin Player:

While music is playing, swipe left or right to move to the previous or next song.

Tap the screen to open the player controls, where you can mute the volume, toggle shuffle or repeat, pause, or stop playback. When the player controls are visible or music is playing and showing album art, swipe down to open the playlist selector or up to start the VA.

When no music is playing, the display switches to standby clock. Optionally, the screen can fade to black when left idle. A single tap will wake the display again. In standby mode, swiping left or right changes clock style.

### **Playlists, Shuffle & Repeat**

For playlists, shuffle and repeat to work you need to enter the url and name of playlist in the fields in Home Assistant, and set the music assistant mediaplayer name of the device (not the esphome mediaplayer):


<img width="902" height="347" alt="Screenshot 2026-06-19 at 18 50 11" src="https://github.com/user-attachments/assets/79c7866b-49b9-4682-9f32-ff5a4a2df0e6" />

<img width="626" height="233" alt="Screenshot 2026-06-19 at 18 52 30" src="https://github.com/user-attachments/assets/80d7d9f3-f411-4658-bf60-0deecf49ceb2" />


and allow the device to perform Home Assistant actions:

<img width="830" height="519" alt="Screenshot 2026-06-19 at 18 53 30" src="https://github.com/user-attachments/assets/c6773cad-7b1c-4e11-81c5-8d7cfa0cde0b" />


For Dual Voice Assistants to work, you need to set SAME NAME in the label box below the wake words like this:

<img width="716" height="300" alt="Screenshot 2026-06-19 at 18 44 04" src="https://github.com/user-attachments/assets/ca05e6b1-0301-47f0-a5ea-a36a9d551d5f" />


This is because we can't read from the device which wakewords are set in HA, and we need that to compare with the wake word used.

---

