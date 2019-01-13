# Clocky
A ESP8266 7 digit LED(WS2812B) Clock/Timer/Scoreboard/Countdown/Temperature/Humidity display firmware

This is a far too large firmware for a ESP8266 including a MDNS supporting webserver, a NTP-time update mechanism, a Over-The-Air Firmware update option, customizable and uploadable webpages, temperature and humidity handling of a DHT22, a factory reset option and a highly customizable ...CLOCK!

Current features are:
 - Displaying...
  - time! O_o
  - humidity
  - temperature
  - a Scoreboard
  - a Countdown
  - a Stopwatch
 - Changing color
 - Constant color changing option
 - blinking Dots option
 - adjustable brightness

# Hardware

  You need 30 WS2812B LED lights, solder 28 of them into 4 7-digit displays.
  The 4 displays are then connected with each other in the following order:

```
 HOUR       HOUR   DOTS  MINUTES   MINUTES          
  24         17             8         1
29  25  -  22  18 - 16 - 13   9  -  6   2
  30         23            14         7
28  26  -  21  19 - 15 - 12  10  -  5   3
  27         20            11         4
```

  (Great artistic work, isn't it?)

  The current pin configuration is as follows:
  ```
      ESP8266
      Pin D2    ->    DataIN on the first WS2812B
      Pin D1    ->    DATA on the DHT22
      3.3V      ->    VCC on the DHT22
      GND       ->    GND on the DHT22
```
  Power the ESP8266 and the LEDs(Don't try to power the LEDs from the ESP. It's not designed to support such a high load.)

# Usage
  Connect to the accesspoint `Clocky` (Password is `StephanS`) and open http://clocky.local
  Add your local wifi accesspoint name(ssid) and the password. After that the clock will try to connect to your wifi and update its time with a NTP server.
  As long as that doesn't work it'll continue to keep its own AP running and displays a friendly `HI HI`.

# Have Fun!
