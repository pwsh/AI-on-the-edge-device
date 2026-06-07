<h1 align="center">AI on the Edge Device: Digitizing Your non-digital meters with an ESP32-CAM</h1>
<br>
<br>

## Table of Contents
- [Key Features 🚀](#key-features-)
- [Workflow 🔧](#workflow-)
- [Impressions 📷](#impressions-)
  - [AI-on-the-edge-device on a Water Meter 💧](#ai-on-the-edge-device-on-a-water-meter-)
  - [Web Interface (Water Meter) 💻](#web-interface-water-meter-)
  - [AI-on-the-edge-device on an Electrical Power Meter ⚡](#ai-on-the-edge-device-on-an-electrical-power-meter-)
  - [Modern Web Interface (v17) 🌓](#modern-web-interface-v17-)
- [What's New in v17 🆕](#whats-new-in-v17-)
- [Setup 🛠️](#setup-%EF%B8%8F)
- [Download 🔽](#download-)
- [Flashing the ESP32 💾](#flashing-the-esp32-)
- [Flashing the SD Card 💾](#flashing-the-sd-card-)
- [Casing 🛠️](#casing-%EF%B8%8F)
- [Donate ☕](#donate-)
- [Support 💬](#support-)
- [Changes and History 📜](#changes-and-history-)
- [Build It Yourself 🔨](#build-it-yourself-)
- [Tools 🛠️](#tools-%EF%B8%8F)
- [Additional Ideas 💡](#additional-ideas-)
- [Our Contributors ❤️](#our-contributors-%EF%B8%8F)

<p align="center">
    <a href="#top">
        <img src="https://img.shields.io/badge/Back%20to%20Top-000000?style=for-the-badge&logo=github&logoColor=white" alt="Back to Top">
    </a>
</p>




[![made-with-c++](https://img.shields.io/badge/Made%20with-C++-1f425f.svg)](https://github.com/jomjol/AI-on-the-edge-device/tree/main/code)
[![Doc](https://img.shields.io/badge/Doc-MkDocs-526CFE.svg)](https://jomjol.github.io/AI-on-the-edge-device-docs/)
[![Total downloads](https://img.shields.io/github/downloads/jomjol/AI-on-the-edge-device/total.svg)](https://GitHub.com/jomjol/AI-on-the-edge-device/releases/)
[![GitHub release](https://img.shields.io/github/release/jomjol/AI-on-the-edge-device.svg)](https://GitHub.com/jomjol/AI-on-the-edge-device/releases/)
[![GitHub forks](https://img.shields.io/github/forks/jomjol/AI-on-the-edge-device.svg?style=social&label=Fork&maxAge=2592000)](https://GitHub.com/jomjol/AI-on-the-edge-device/network/)
[![GitHub stars](https://img.shields.io/github/stars/jomjol/AI-on-the-edge-device.svg?style=social&label=Star&maxAge=2592000)](https://GitHub.com/jomjol/AI-on-the-edge-device/stargazers/)

<p align="center" id="top">
  <img src="images/icon/watermeter.svg" width="150px">
</p>

Artificial intelligence is everywhere, from speech to image recognition. While most AI systems rely on powerful processors or cloud computing, **edge computing** brings AI closer to the end user by utilizing the capabilities of modern processors.  
This project demonstrates edge computing using the **ESP32**, a low-cost, AI-capable device, to digitize your analog meters—whether water, gas, or electricity. With affordable hardware and simple instructions, you can turn any standard meter into a smart device.

Let's explore how to make **AI on the Edge** a reality! 🌟

All you need is an [ESP32 board with a supported camera](https://jomjol.github.io/AI-on-the-edge-device-docs/Hardware-Compatibility/) and some practical skills. 🛠️

---

<br>

## Key Features 🚀
- 🔗 **Tensorflow Lite (TFLite) integration** – including an easy-to-use wrapper.
- 📸 **Inline image processing** (feature detection, alignment, ROI extraction).
- 💡 **Small** and **affordable** device (3 x 4.5 x 2 cm³, less than 10 EUR).
- 📷 Integrated camera and illumination.
- 🌐 Web interface for administration and control.
- 🔄 OTA interface for updating directly via the web interface.
- 🏠 Full integration with Home Assistant.
- 📊 Support for **Influx DB 1** and **2**.
- 📡 **MQTT protocol** support, now including **verified MQTTS/HTTPS out of the box**.
- 📥 **REST API** available for data access.
- 🧩 **ESP32-S3** support (8 / 16 MB) in addition to the classic ESP32-CAM — runs **with or without an SD card**.
- ⏱️ **Flexible scheduling** – fixed interval *or* specific daily times, plus **FastRead** incremental recognition for 5–10 s updates.
- 🌓 **Modern, responsive web UI** with **dark mode**, configuration **backups/restore**, and live updates.
- 🔁 **OTA rollback** and automatic crash recovery for safe, reliable updates.

> 🆕 **Upgrading from v16?** See [What's New in v17](#whats-new-in-v17-) below for the full list of changes.

<br>

## Workflow 🔧
The device captures a photo of your meter at set intervals. It then extracts the Regions of Interest (ROIs) from the image and runs them through artificial intelligence. As a result, you get the digitized value of your meter.

There are several options for what to do with that value:
- 📤 Send it to a **MQTT broker**.
- 📝 Write it to an **InfluxDb**.
- 🔗 Provide access via a **REST API**.

<p align="center">
  <img src="images/idea.jpg" width="600"> 
</p>

---

<br>

## Impressions 📷

+ ### AI-on-the-edge-device on a Water Meter 💧
  <p align="center">
    <img src="images/watermeter_all.jpg" width="200"><img 
  src="images/main.jpg" width="200"><img 
  src="images/size.png" width="200"> 
  </p>

+ ### Web Interface (Water Meter) 💻
  <p align="center">
    <img src="images/watermeter.jpg" width="600"> 
  </p>

+ ### AI-on-the-edge-device on an Electrical Power Meter ⚡
  <p align="center">
    <img src="images/powermeter.jpg" width="600"> 
  </p>

+ ### Modern Web Interface (v17) 🌓
  The redesigned, responsive web UI with a built-in dark mode. Live on a real water meter below:

  <p align="center">
    <img src="images/webui/overview.png" width="420" alt="Overview – live reading, ROI overlay, per-round diagnostics and recognition confidence">
    <img src="images/webui/roi-editor.png" width="420" alt="ROI editor – Examine a single region on demand: analysed crop, reading and confidence shown inline">
  </p>
  <p align="center">
    <em>Overview (live reading + ROI overlay + per-round diagnostics + recognition&nbsp;confidence) &nbsp;·&nbsp; ROI editor (Examine a region on demand — reading&nbsp;+&nbsp;confidence inline; pull a fresh aligned frame)</em>
  </p>

  <p align="center">
    <img src="images/webui/configuration.png" width="280" alt="Configuration – card layout, FastRead and config backup/restore">
    <img src="images/webui/system-info.png" width="280" alt="System info – firmware/web-UI version, chip, camera and host details">
    <img src="images/webui/overview-mobile.png" width="160" alt="Overview on a phone – the layout stacks responsively">
  </p>
  <p align="center">
    <em>Configuration (FastRead, expert parameters, Backup&nbsp;on&nbsp;save / Restore) &nbsp;·&nbsp; System info &nbsp;·&nbsp; Mobile-friendly responsive layout</em>
  </p>

  <p align="center">
    <img src="images/webui/publishing.png" width="300" alt="Data Publishing – a per-platform matrix choosing exactly which parameters reach MQTT, InfluxDB and Home Assistant, plus a send-only-changed mode">
  </p>
  <p align="center">
    <em>Data Publishing — pick exactly which parameters reach MQTT / InfluxDB / Home&nbsp;Assistant, and send only when values change</em>
  </p>

---

<br>

## What's New in v17 🆕
A summary of the functionality changes since **v16**. See the [Changelog](Changelog.md) for the full detail.

### 🧩 Hardware & platform
- Rebuilt on **ESP-IDF 6.0.1** (modern GCC&nbsp;15 / C++ toolchain).
- **New: ESP32-S3 support** (8&nbsp;MB and 16&nbsp;MB variants) alongside the classic **ESP32-CAM**. ESP32-S3 boards run **with or without an SD card** — the web UI, CNN models and configuration can live in on-board flash. The **ESP32-WROVER** is also supported.
- Camera sensors supported: **OV2640, OV3660 and OV5640**.
- A maintained [Board Feature Matrix](docs/BOARD-FEATURE-MATRIX.md) tracks the differences between boards.

### ⚡ Recognition & accuracy
- **Scheduling** – run at a fixed interval *or* at specific daily times (multiple time slots).
- **FastRead (incremental)** – re-runs the neural network only on the digits whose image actually changed, enabling **5–10&nbsp;s** update intervals.
- **Meter Type + physics-bounded reading** – pick *Water / Electricity / Gas* and the device derives the maximum rate the meter can physically advance (from residential pipe/service assumptions, all overridable) to **auto-fill the Max Rate** and reject impossible jumps. See [docs/PREDICTIVE-READING.md](docs/PREDICTIVE-READING.md).
- **Predictive digit reading** – uses that rate bound plus a per-digit history to skip digits that *cannot* have changed, and a **confident-read matrix** (shown on the overview) to resolve unknown (“N”) digits to a valid value.
- **Confidence vote** (on by default) – automatically recovers when a single misread latches the value too high: after a few consistent lower reads it overrides the stuck-high outlier.
- **Leak detection** (water/gas) – flags a potential leak when the meter advances *continuously* past a threshold (default **2&nbsp;h**); publishes a `leak` flag and `continuous_usage` time to MQTT, Home Assistant, InfluxDB and the REST API.
- Per-round **diagnostics** (analysis type, digits analysed, time-to-next full read) shown on the overview and exposed via MQTT / JSON / InfluxDB.

### 🌓 Modern web interface
- **Dark mode** and a **responsive, mobile-friendly** layout built from content cards.
- **Confidence display** – the overview shows the recognition **confidence** for each number sequence (above its digit matrix and below its value), so a low-confidence read is visible at a glance.
- **Examine a single ROI on demand** – in the digit/analog ROI editors, an **Examine selected ROI** button runs the neural network on just that region and shows the analysed crop, the reading and its confidence inline — no save or full round needed.
- **Pull a fresh camera image** – the ROI editors can grab and align a **live frame** to use in place of the stored reference image, so you can tell at a glance whether the reference / alignment is still accurate.
- **Configuration backups** – every save snapshots the configuration; **one-click Restore** keeps the latest 10 (bundled models are excluded to keep backups small).
- **Changed-digit highlighting** on the overview, a **Pause processing** control, and an auto-tailing live log viewer.
- **Keyboard nudging** in the ROI editors and faster **WebGL** data graphs.
- **Apply the interval live** (no reboot) and a **one-click migration** prompt when the firmware and web-UI versions differ.

### 🔁 Reliability
- **Camera auto-recovery** – a stuck OV2640 is now power-cycled in firmware over its power-down line before each init attempt (with escalating drains), so a sensor wedged by a software reset recovers on its own instead of needing a physical unplug. *(Fixes a long-standing bug where the reset was silently compiled out on every board.)*
- **OTA rollback** – a crash-looping update is automatically rolled back to the last working firmware.
- **Crash dumps are saved to the SD card** for later retrieval.
- **Wi-Fi resilience** – exponential-backoff reconnect, automatic fall-back to the setup access point after repeated failures (now **including an empty/corrupt Wi-Fi config**, so the device can always be reconfigured from its portal — no SD pull needed), fast wrong-password detection, atomic `wlan.ini` writes, and a 3×-reset Wi-Fi factory reset.
- RGB status LED (ESP32-S3) reflects the Wi-Fi / processing state.

### 🔒 Connectivity & security
- **Data Publishing page** – a per-platform matrix to pick exactly **which parameters** are sent to **MQTT, InfluxDB and Home Assistant**, including values the device gathers but didn't previously send (*previous value*, *recognition confidence*, plus *raw/rate* for InfluxDB). The bulky **raw** and **JSON** topics are now **off by default**. An **"only send changed readings"** mode skips a sequence's reading topics while its value is unchanged (diagnostics & leak state still send), cutting traffic. Home Assistant entities (now also covering confidence, previous value and the round diagnostics) carry proper units/device-classes — e.g. free memory converts between B/kB/MB — and a disabled entity is removed via an empty retained discovery message.
- **Verified MQTTS / HTTPS out of the box** via a built-in Mozilla CA bundle — connect to public TLS brokers (e.g. HiveMQ Cloud) and InfluxDB Cloud **without uploading a certificate**.
- Default hostname `edgeai-<mac>` so multiple devices don't clash on the network.

---

<br>

## Setup 🛠️
There is growing [documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/) which provides you with a lot of information. Head there to get started, set it up, and configure it.

There are also articles in the German Heise magazine "make:" about the setup and technical background (behind a paywall): [DIY - Setup](https://www.heise.de/select/make/2021/2/2103513300897420296) 📰

A lot of people have created useful YouTube videos that might help you get started:
- 🎥 [youtube.com/watch?v=HKBofb1cnNc](https://www.youtube.com/watch?v=HKBofb1cnNc)
- 🎥 [youtube.com/watch?v=yyf0ORNLCk4](https://www.youtube.com/watch?v=yyf0ORNLCk4)
- 🎥 [youtube.com/watch?v=XxmTubGek6M](https://www.youtube.com/watch?v=XxmTubGek6M)
- 🎥 [youtube.com/watch?v=mDIJEyElkAU](https://www.youtube.com/watch?v=mDIJEyElkAU)
- 🎥 [youtube.com/watch?v=SssiPkyKVVs](https://www.youtube.com/watch?v=SssiPkyKVVs)
- 🎥 [youtube.com/watch?v=MAHE_QyHZFQ](https://www.youtube.com/watch?v=MAHE_QyHZFQ)
- 🎥 [youtube.com/watch?v=Uap_6bwtILQ](https://www.youtube.com/watch?v=Uap_6bwtILQ)

For further background information, head to:
- [Neural Networks](https://www.heise.de/select/make/2021/6/2126410443385102621)
- [Training Neural Networks](https://www.heise.de/select/make/2022/1/2134114065999161585)
- [Programming on the ESP32](https://www.heise.de/select/make/2022/2/2204010051597422030)

---

<br>

## Download 🔽
The latest available version can be found on the [Releases page](https://github.com/jomjol/AI-on-the-edge-device/releases).

---

<br>

## Flashing the ESP32 💾
Initially, you will have to flash the ESP32 via a USB connection. Later updates are possible directly over the air (OTA using Wi-Fi).

There are different ways to flash your ESP32:
- The preferred way is the [Web Installer and Console](https://jomjol.github.io/AI-on-the-edge-device/index.html), a browser-based tool to flash the ESP32 and extract the log over USB:  
  ![](images/web-installer.png)
- Flash Tool from Espressif
- ESPtool (command-line tool)

See the [documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/) for more information.

---

<br>

## Flashing the SD Card 💾
The SD card can be set up automatically after the firmware is installed. See the [documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#remote-setup-using-the-built-in-access-point) for details. For this to work, the SD card must be FAT formatted (which is the default on a new SD card).

Alternatively, the SD card can still be set up manually. See the [documentation](https://jomjol.github.io/AI-on-the-edge-device-docs/Installation/#3-sd-card) for details.

---

<br>

## Casing 🛠️
Various 3D-printable housings can be found here:
- 💧 [Water Meter](https://www.thingiverse.com/thing:4573481)
- ⚡ [Power Meter](https://www.thingiverse.com/thing:5028229)
- 🔥 [Gas Meter](https://www.thingiverse.com/thing:5224101)
- 📷 [ESP32-cam housing only](https://www.thingiverse.com/thing:4571627)
- 📡 [Top cover and Antenna Holder](https://www.printables.com/model/452139-cover-for-esp32-cam-water-meter)

---

<br>

## Donate ☕
If you'd like to support the developer with a cup of coffee, you can do so via [PayPal](https://www.paypal.com/donate?hosted_button_id=8TRSVYNYKDSWL).

<p align="center">
  <a href="https://www.paypal.com/donate?hosted_button_id=8TRSVYNYKDSWL"><img border="0" src="images/paypal.png" width="200px" target="_blank"></a>
</p>

---

<br>

## Support 💬
If you have any technical problems, please search the [discussions](https://github.com/jomjol/AI-on-the-edge-device/discussions). In case you find a bug or have a feature request, please open an [issue](https://github.com/jomjol/AI-on-the-edge-device/issues).

For any other issues, you can contact the developer via email:  
<p align="center">
  <img src="images/mail.jpg" height="25">
</p>

---

<br>

## Changes and History 📜
See the [Changelog](Changelog.md) for detailed information.

---

<br>

## Build It Yourself 🔨
See the [Build Instructions](code/README.md) for step-by-step guidance.

---

<br>

## Tools 🛠️
* Logfile downloader and combiner (Thanks to [reserve85](https://github.com/reserve85))  
  * It can be found at ['/tools/logfile-tool'](https://github.com/jomjol/AI-on-the-edge-device/tree/main/tools/logfile-tool).

---

<br>

## Additional Ideas 💡
There are some ideas and feature requests which are not currently being pursued—mainly due to capacity constraints on the part of the developers. These features are collected in the [issues](https://github.com/jomjol/AI-on-the-edge-device/issues) and in [FeatureRequest.md](FeatureRequest.md).

---

<br>

## Our Contributors ❤️
<!-- Do not manually edit this section! It should get updated using the Github action "Manually update contributors list" -->
<!-- readme: contributors -start -->
<table>
	<tbody>
		<tr>
            <td align="center">
                <a href="https://github.com/jomjol">
                    <img src="https://avatars.githubusercontent.com/u/30766535?v=4" width="100;" alt="jomjol"/>
                    <br />
                    <sub><b>jomjol</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/caco3">
                    <img src="https://avatars.githubusercontent.com/u/1783586?v=4" width="100;" alt="caco3"/>
                    <br />
                    <sub><b>CaCO3</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/haverland">
                    <img src="https://avatars.githubusercontent.com/u/412645?v=4" width="100;" alt="haverland"/>
                    <br />
                    <sub><b>Frank Haverland</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/SybexX">
                    <img src="https://avatars.githubusercontent.com/u/587201?v=4" width="100;" alt="SybexX"/>
                    <br />
                    <sub><b>michael</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/Slider0007">
                    <img src="https://avatars.githubusercontent.com/u/115730895?v=4" width="100;" alt="Slider0007"/>
                    <br />
                    <sub><b>Slider0007</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/nliaudat">
                    <img src="https://avatars.githubusercontent.com/u/6782613?v=4" width="100;" alt="nliaudat"/>
                    <br />
                    <sub><b>Nicolas Liaudat</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/Zwer2k">
                    <img src="https://avatars.githubusercontent.com/u/10438794?v=4" width="100;" alt="Zwer2k"/>
                    <br />
                    <sub><b>Zwer2k</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/phlupp">
                    <img src="https://avatars.githubusercontent.com/u/6304863?v=4" width="100;" alt="phlupp"/>
                    <br />
                    <sub><b>phlupp</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/jasaw">
                    <img src="https://avatars.githubusercontent.com/u/721280?v=4" width="100;" alt="jasaw"/>
                    <br />
                    <sub><b>jasaw</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/dockSquadron">
                    <img src="https://avatars.githubusercontent.com/u/11964767?v=4" width="100;" alt="dockSquadron"/>
                    <br />
                    <sub><b>dockSquadron</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/rdmueller">
                    <img src="https://avatars.githubusercontent.com/u/1856308?v=4" width="100;" alt="rdmueller"/>
                    <br />
                    <sub><b>Ralf D. Müller</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/cristianmitran">
                    <img src="https://avatars.githubusercontent.com/u/36613624?v=4" width="100;" alt="cristianmitran"/>
                    <br />
                    <sub><b>cristianmitran</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/michaeljoos72">
                    <img src="https://avatars.githubusercontent.com/u/20517474?v=4" width="100;" alt="michaeljoos72"/>
                    <br />
                    <sub><b>michaeljoos72</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/henrythasler">
                    <img src="https://avatars.githubusercontent.com/u/6277203?v=4" width="100;" alt="henrythasler"/>
                    <br />
                    <sub><b>Henry Thasler</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/amantyagiprojects">
                    <img src="https://avatars.githubusercontent.com/u/174239452?v=4" width="100;" alt="amantyagiprojects"/>
                    <br />
                    <sub><b>Naman Tyagi</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/pixeldoc2000">
                    <img src="https://avatars.githubusercontent.com/u/376715?v=4" width="100;" alt="pixeldoc2000"/>
                    <br />
                    <sub><b>pixel::doc</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/mad2xlc">
                    <img src="https://avatars.githubusercontent.com/u/37449746?v=4" width="100;" alt="mad2xlc"/>
                    <br />
                    <sub><b>Stefan</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/jochenchrist">
                    <img src="https://avatars.githubusercontent.com/u/2930448?v=4" width="100;" alt="jochenchrist"/>
                    <br />
                    <sub><b>jochenchrist</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/parhedberg">
                    <img src="https://avatars.githubusercontent.com/u/13777521?v=4" width="100;" alt="parhedberg"/>
                    <br />
                    <sub><b>parhedberg</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/fsck-block">
                    <img src="https://avatars.githubusercontent.com/u/58307481?v=4" width="100;" alt="fsck-block"/>
                    <br />
                    <sub><b>fsck-block</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/slovdahl">
                    <img src="https://avatars.githubusercontent.com/u/1417619?v=4" width="100;" alt="slovdahl"/>
                    <br />
                    <sub><b>Sebastian Lövdahl</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/RaHehl">
                    <img src="https://avatars.githubusercontent.com/u/7577984?v=4" width="100;" alt="RaHehl"/>
                    <br />
                    <sub><b>Raphael Hehl</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/LordGuilly">
                    <img src="https://avatars.githubusercontent.com/u/13271835?v=4" width="100;" alt="LordGuilly"/>
                    <br />
                    <sub><b>LordGuilly</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/muggenhor">
                    <img src="https://avatars.githubusercontent.com/u/484066?v=4" width="100;" alt="muggenhor"/>
                    <br />
                    <sub><b>Giel van Schijndel</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/bilalmirza74">
                    <img src="https://avatars.githubusercontent.com/u/84387676?v=4" width="100;" alt="bilalmirza74"/>
                    <br />
                    <sub><b>Bilal Mirza</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/AngryApostrophe">
                    <img src="https://avatars.githubusercontent.com/u/89547888?v=4" width="100;" alt="AngryApostrophe"/>
                    <br />
                    <sub><b>AngryApostrophe</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/ralf1307">
                    <img src="https://avatars.githubusercontent.com/u/46164027?v=4" width="100;" alt="ralf1307"/>
                    <br />
                    <sub><b>Ralf Rachinger</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/Ranjana761">
                    <img src="https://avatars.githubusercontent.com/u/129291313?v=4" width="100;" alt="Ranjana761"/>
                    <br />
                    <sub><b>Ranjana761</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/SURYANSH-RAI">
                    <img src="https://avatars.githubusercontent.com/u/79277130?v=4" width="100;" alt="SURYANSH-RAI"/>
                    <br />
                    <sub><b>SURYANSH RAI</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/SkylightXD">
                    <img src="https://avatars.githubusercontent.com/u/16561545?v=4" width="100;" alt="SkylightXD"/>
                    <br />
                    <sub><b>SkylightXD</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/ottk3">
                    <img src="https://avatars.githubusercontent.com/u/5236802?v=4" width="100;" alt="ottk3"/>
                    <br />
                    <sub><b>Sven Rojek</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/Turbo87">
                    <img src="https://avatars.githubusercontent.com/u/141300?v=4" width="100;" alt="Turbo87"/>
                    <br />
                    <sub><b>Tobias Bieniek</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/tkopczuk">
                    <img src="https://avatars.githubusercontent.com/u/101632?v=4" width="100;" alt="tkopczuk"/>
                    <br />
                    <sub><b>Tomek Kopczuk</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/yonz2">
                    <img src="https://avatars.githubusercontent.com/u/13886257?v=4" width="100;" alt="yonz2"/>
                    <br />
                    <sub><b>Yonz</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/Yveaux">
                    <img src="https://avatars.githubusercontent.com/u/7716005?v=4" width="100;" alt="Yveaux"/>
                    <br />
                    <sub><b>Yveaux</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/flooxo">
                    <img src="https://avatars.githubusercontent.com/u/93255373?v=4" width="100;" alt="flooxo"/>
                    <br />
                    <sub><b>flox_x</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/gneluka">
                    <img src="https://avatars.githubusercontent.com/u/32097881?v=4" width="100;" alt="gneluka"/>
                    <br />
                    <sub><b>gneluka</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/kalwados">
                    <img src="https://avatars.githubusercontent.com/u/11840444?v=4" width="100;" alt="kalwados"/>
                    <br />
                    <sub><b>kalwados</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/kub3let">
                    <img src="https://avatars.githubusercontent.com/u/95883234?v=4" width="100;" alt="kub3let"/>
                    <br />
                    <sub><b>kub3let</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/pfeifferch">
                    <img src="https://avatars.githubusercontent.com/u/73090220?v=4" width="100;" alt="pfeifferch"/>
                    <br />
                    <sub><b>pfeifferch</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/rstephan">
                    <img src="https://avatars.githubusercontent.com/u/8532364?v=4" width="100;" alt="rstephan"/>
                    <br />
                    <sub><b>rstephan</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/smartboart">
                    <img src="https://avatars.githubusercontent.com/u/38385805?v=4" width="100;" alt="smartboart"/>
                    <br />
                    <sub><b>smartboart</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/wetneb">
                    <img src="https://avatars.githubusercontent.com/u/309908?v=4" width="100;" alt="wetneb"/>
                    <br />
                    <sub><b>Antonin Delpeuch</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/adarazs">
                    <img src="https://avatars.githubusercontent.com/u/6269603?v=4" width="100;" alt="adarazs"/>
                    <br />
                    <sub><b>Attila Darazs</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/austindrenski">
                    <img src="https://avatars.githubusercontent.com/u/21338699?v=4" width="100;" alt="austindrenski"/>
                    <br />
                    <sub><b>Austin Drenski</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/PLCHome">
                    <img src="https://avatars.githubusercontent.com/u/29116097?v=4" width="100;" alt="PLCHome"/>
                    <br />
                    <sub><b>PLCHome</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/CFenner">
                    <img src="https://avatars.githubusercontent.com/u/9592452?v=4" width="100;" alt="CFenner"/>
                    <br />
                    <sub><b>Christopher Fenner</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/dkneisz">
                    <img src="https://avatars.githubusercontent.com/u/43378003?v=4" width="100;" alt="dkneisz"/>
                    <br />
                    <sub><b>Dave</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/FarukhS52">
                    <img src="https://avatars.githubusercontent.com/u/129654632?v=4" width="100;" alt="FarukhS52"/>
                    <br />
                    <sub><b>Farookh Zaheer Siddiqui</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/hex7c0">
                    <img src="https://avatars.githubusercontent.com/u/4419146?v=4" width="100;" alt="hex7c0"/>
                    <br />
                    <sub><b>Francesco Carnielli</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/040medien">
                    <img src="https://avatars.githubusercontent.com/u/115072?v=4" width="100;" alt="040medien"/>
                    <br />
                    <sub><b>Frederik Kemner</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/eltociear">
                    <img src="https://avatars.githubusercontent.com/u/22633385?v=4" width="100;" alt="eltociear"/>
                    <br />
                    <sub><b>Ikko Eltociear Ashimine</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/queeek">
                    <img src="https://avatars.githubusercontent.com/u/9533371?v=4" width="100;" alt="queeek"/>
                    <br />
                    <sub><b>Ina</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/joergrosenkranz">
                    <img src="https://avatars.githubusercontent.com/u/310438?v=4" width="100;" alt="joergrosenkranz"/>
                    <br />
                    <sub><b>Joerg Rosenkranz</b></sub>
                </a>
            </td>
		</tr>
		<tr>
            <td align="center">
                <a href="https://github.com/Innovatorcloudy">
                    <img src="https://avatars.githubusercontent.com/u/183274513?v=4" width="100;" alt="Innovatorcloudy"/>
                    <br />
                    <sub><b>KrishCode</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/myxor">
                    <img src="https://avatars.githubusercontent.com/u/1397377?v=4" width="100;" alt="myxor"/>
                    <br />
                    <sub><b>Marco H</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/rainman110">
                    <img src="https://avatars.githubusercontent.com/u/3213107?v=4" width="100;" alt="rainman110"/>
                    <br />
                    <sub><b>Martin Siggel</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/mkelley88">
                    <img src="https://avatars.githubusercontent.com/u/5567324?v=4" width="100;" alt="mkelley88"/>
                    <br />
                    <sub><b>Matthew T. Kelley</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/toolsfactory">
                    <img src="https://avatars.githubusercontent.com/u/7744975?v=4" width="100;" alt="toolsfactory"/>
                    <br />
                    <sub><b>Michael Geissler</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/ppisljar">
                    <img src="https://avatars.githubusercontent.com/u/13629809?v=4" width="100;" alt="ppisljar"/>
                    <br />
                    <sub><b>Peter Pisljar</b></sub>
                </a>
            </td>
		</tr>
	<tbody>
</table>
<!-- readme: contributors -end -->

---

<div align="center">
    <a href="#top">
        <img src="https://img.shields.io/badge/Back%20to%20Top-000000?style=for-the-badge&logo=github&logoColor=white" alt="Back to Top">
    </a>
</div>
