# CYD Spotify Hub

Touch UI for the **ESP32-2432S028**, also known as the **ESP32 CYD / Cheap Yellow Display**, featuring a home dashboard and an integrated Spotify player.

This project is built specifically for the CYD's **ILI9341 320x240 landscape display** and **XPT2046 touch controller**.

![Home screen](docs/images/home.jpeg)

![Spotify player](docs/images/spotify.jpeg)

## Features

- Home dashboard with a large clock, current date, Wi-Fi status, local IP, and a button to open Spotify.
- Spotify player with album art, track title, artist name, and touch controls.
- Previous, play/pause, and next track controls.
- Home button inside the player to return to the dashboard.
- UI tuned for the ESP32-2432S028 at 320x240 resolution.
- Touch handled through the CYD/XPT2046 polling driver.
- Code structure prepared for adding more screens/apps later.

## Hardware

- ESP32-2432S028 / ESP32 CYD / Cheap Yellow Display
- ILI9341 2.8" 320x240 display
- XPT2046 touch controller
- USB serial device available as `/dev/ttyUSB0` or similar

## Requirements

- PlatformIO
- Spotify Premium account for playback control
- An app created in the Spotify Developer Dashboard
- This redirect URI configured in the Spotify app:

```text
http://127.0.0.1:8888/callback
```

Required scopes:

```text
user-read-currently-playing user-read-playback-state user-modify-playback-state
```

## Secure Configuration

This repository **must not include real credentials**. The real secrets file is ignored by Git:

```text
include/secrets.h
```

Create your local secrets file from the example:

```bash
cp include/secrets_example.h include/secrets.h
```

Fill in your values:

```cpp
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define SPOTIFY_CLIENT_ID "YOUR_SPOTIFY_CLIENT_ID"
#define SPOTIFY_CLIENT_SECRET "YOUR_SPOTIFY_CLIENT_SECRET"
#define SPOTIFY_REFRESH_TOKEN "YOUR_SPOTIFY_REFRESH_TOKEN"
```

## Generate a Refresh Token

With the redirect URI configured in Spotify Dashboard, run:

```bash
python3 tools/get_spotify_refresh_token.py "YOUR_CLIENT_ID" "YOUR_CLIENT_SECRET"
```

Open the URL shown in the terminal, authorize the app, and copy the generated `SPOTIFY_REFRESH_TOKEN` into `include/secrets.h`.

## Build and Upload

Compile with PlatformIO:

```bash
pio run
```

Upload to the ESP32:

```bash
pio run -t upload --upload-port /dev/ttyUSB0
```

If you are using the local virtual environment from this project:

```bash
PLATFORMIO_CORE_DIR=/home/lucas/esp32/.platformio /home/lucas/esp32/.venv/bin/pio run -t upload --upload-port /dev/ttyUSB0
```

Open the serial monitor:

```bash
pio device monitor --port /dev/ttyUSB0
```

## WSL and USB

If you are using WSL and `/dev/ttyUSB0` does not exist, attach the USB device from Windows:

```powershell
usbipd list
usbipd bind --busid YOUR-BUSID
usbipd attach --wsl --busid YOUR-BUSID
```

Then check inside WSL:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

## Project Structure

```text
include/
  CYD28_TouchscreenR.h     # CYD/XPT2046 touch driver
  secrets_example.h        # Safe credentials template

src/
  CYD28_TouchscreenR.cpp   # Touch implementation
  main.cpp                 # UI, Home, Player, and Spotify integration

tools/
  get_spotify_refresh_token.py

docs/images/
  home.jpeg
  spotify.jpeg
```

## Security Notes

- Never publish `include/secrets.h`.
- If any credential or token was exposed during development, revoke it in the Spotify Developer Dashboard and generate a new one.
- The firmware currently uses `client.setInsecure()` to simplify HTTPS on the ESP32. That is acceptable for local prototyping, but not ideal for production.
- `SPOTIFY_CLIENT_SECRET` is stored on the ESP32. For a public or production-grade project, the recommended design is to move secrets to a backend service and only provide temporary tokens to the device.

## Future Improvements

- Add more apps/screens to the dashboard
- Create a settings screen
- Improve album art caching
- Add custom app icons
- Move Spotify credentials to a secure backend
