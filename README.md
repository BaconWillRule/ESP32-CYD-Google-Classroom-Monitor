# ESP32 Google Classroom Monitor

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/Display-ILI9341-red?style=flat-square" alt="Display">
  <img src="https://img.shields.io/badge/License-GPLv3-blue?style=flat-square" alt="License">
</p>

---

**Turn your "Cheap Yellow Display" into a dedicated command center for your school life.**

This project is a custom firmware for the **ESP32-2432S028** (CYD) that connects to Google Classroom. It doesn't just list your homework; it visualizes your academic status with a smart timetable and a courses breakdown.

## Features

- [x] **Touch Interface**: Smooth, driver-level touch controls to cycle between Dashboard, List, Grid, and Breakdown views.
- [x] **Chill Mode**: A Matrix-style rain animation that kicks in when you have 0 missing assignments.
- [X] **Class Breakdown**: A breakdown of your classes each given a score based on missing work (the score logic will be improved in the future)
- [x] **Smart Timetable**: A 5-day grid that automatically highlights lessons in **RED** if you have missing work for that subject.
- [x] **Smart Filtering**: Automatically maps messy Google Classroom names (e.g., "13A/CS1") to clean subject codes (e.g., "CompSci").
- [ ] **OTA Updates**: (Planned) Update firmware wirelessly.

## Screenshots



| Dashboard | Smart Grid | Sector Status |
| :-------: | :--------: | :-----------: |
| _(Image)_ | _(Image)_  |   _(Image)_   |

## Getting Started

### Hardware Required

- **ESP32-2432S028**: The "Cheap Yellow Display". It has an integrated ILI9341 screen and XPT2046 touch controller.
- **USB-C Cable**: For programming and power.

### Software Prerequisites

- **Arduino IDE**: For flashing the firmware.
- **Google Account**: Access to Google Classroom.
- **Blynk IoT Account**: Acts as the bridge between Google and the ESP32.

### Installation Guide

#### 1. Google Apps Script (The Backend)

We need a script to fetch data from Google and send it to the cloud.

1.  Go to [script.google.com](https://script.google.com) and create a new project.
2.  **Add Service**: Click the `+` next to "Services" and add **Google Classroom API**.
3.  **Paste Code**: Copy the script below into `Code.gs`.
4.  **Configure**: Replace `YOUR_BLYNK_AUTH_TOKEN` with your token.
5.  **Trigger**: Set a generic Time-driven trigger to run `syncToBlynk` every 15 minutes.

<details>
<summary>Click to view Google Apps Script</summary>

```javascript
var BLYNK_AUTH_TOKEN = "YOUR_BLYNK_AUTH_TOKEN";

function syncToBlynk() {
  var missing = 0;
  var assigned = 0;
  var done = 0;
  var courses = Classroom.Courses.list().courses;
  var classData = [];

  if (courses && courses.length > 0) {
    for (var i = 0; i < courses.length; i++) {
      var course = courses[i];
      // ... (Add logic to count missing work)
      // classData.push(course.name + ":" + count);
    }
  }

  // Send to Blynk
  updateBlynk(V0, missing);
  updateBlynk(V5, classData.join("|"));
}

function updateBlynk(pin, value) {
  var url =
    "http://blynk.cloud/external/api/update?token=" +
    BLYNK_AUTH_TOKEN +
    "&" +
    pin +
    "=" +
    encodeURIComponent(value);
  UrlFetchApp.fetch(url);
}
```

</details>

#### 2. Blynk Setup

1.  Create a Template in Blynk Console.
2.  Add Datastreams:
    - `V0` (Int): Missing Count
    - `V1` (Int): Assigned Count
    - `V2` (Int): Done Count
    - `V3` (String): Priority Task Name
    - `V4` (String): List Data
    - `V5` (String): Class Data

#### 3. Flashing the Firmware

1.  Open `ClassroomMonitor.ino` in Arduino IDE.
2.  Install libraries: `TFT_eSPI`, `Blynk`, `XPT2046_Touchscreen`.
3.  Update your credentials at the top of the file:
    ```cpp
    char ssid[] = "YOUR_WIFI_SSID";
    char pass[] = "YOUR_WIFI_PASS";
    #define BLYNK_AUTH_TOKEN "YOUR_TOKEN"
    ```
4.  Select Board: **ESP32 Dev Module**.
5.  Upload!

## Configuration

### Customizing Your Timetable

The timetable is hardcoded in the `setupSchedule()` function for reliability. You can easily edit it:

```cpp
// Day 0 (Monday), Period 0
timetable[0][0] = {"Biology", "Miss Brech", "SC9", 8, 30};
```

### Setting Up Monitored Subjects

To make the "Breakdown" page work, you need to tell the system which subjects to track and how to find them in the Google data.

```cpp
SubjectTarget monitoredSubjects[] = {
  {"Biology",   "BIO"}, // Display Name, Search Keyword
  {"Geography", "GEO"},
  {"CompSci",   "CS1"}
};
```

- **Display Name**: What shows up on the screen.
- **Search Keyword**: A unique string to find in your Google Classroom class names (e.g., "BIO" matches "13B/BIO").

## FAQ

### Why is the screen blank?

Ensure you have configured `User_Setup.h` in the `TFT_eSPI` library correctly for the ILI9341 driver and the specific pins of the CYD.

### Can I use this on other ESP32 boards?

Yes, but you will need to change the pin definitions for the Touch and Display drivers. The logic itself is board-agnostic.

### My Breakdown Score is always 0%?

Check your `searchKey` configuration. The system needs to find a partial match in the data sent from Google. Use `Serial.print` to debug the raw data coming in.

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

You are free to use, modify, and distribute this software. However, if you distribute modifications, they must also be open source under the same license. This ensures the project remains free for everyone.
