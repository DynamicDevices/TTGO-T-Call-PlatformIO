## TTGO-T-Call support with PlatformIO

# Overview

I want to do some cellular work and the TTGO T-CALL is a rather neat little ESP32 device with a SIM800L which is perfect for what I need.

Platform.io doesn't seem to have board support for the T-CALL at the time of writing (31/12/22) but there's a nice repo from `NailsOnLinux` which shows how to add custom board support. So props to them and I have taken their original demonstration code which is [here](https://github.com/nailsonlinux/TTGO-T-Call-PlatformIO)

# Video - getting going with this codebase

I've put together an IoT Training Video which you can take a look at to walk through how to get up and running with this code.

[![Iot Training Video](https://user-images.githubusercontent.com/1537834/210586917-a311c8b3-47a7-455c-bb1c-0d97e0b26c4e.png)](https://www.youtube.com/watch?v=7bi77btM4Us)

This walks you through building and uploading the code to the TTGO-CALL, acquiring the cellular network, bringing up a data context and connecting to an MQTT broker to publish messages and subscribe to messages over cellular.

# MQTT over Cellular

I want to play with low bandwidth comms. over cellular and MQTT seems a good starting point.

There's a [TinyGSM](https://github.com/vshymanskyy/TinyGSM) cellular stack for Arduino and an MQTT [PubSubClient](https://github.com/knolleary/pubsubclient) which is usually used over WiFi but can take a TinyGSM stream object to talk to the modem.

So the MQTT client code here uses these two components. I'm experimenting so I have created local copies of both components rather than allowing Platform.io to install the upstream copies, and you may find my copes at this time are slightly different from the upstream.

In particular:

- I am overriding TCP KeepAlives in the TinyGSM SIM800L implementation (consdering how to discuss a potential upstream PR)
- I have added some byte count metrics to PubSubClient and provided a PR upstream.

Currently the code runs on the TTGO T-CALL and there are a few defines you can use to change the behaviour including

- TCP KeepAlive timeouts
- MQTT KeepAlive timeouts
- How often we publish telemetry messaging

There's also the beginnings of a subscription based command handler which at the moment just allows us to remotely reset devices

I've found with the current settings I can maintain a reliable connection over cellular with very low data overhead. 

I am starting to write up my testing and results in the Wiki [here](https://github.com/DynamicDevices/TTGO-T-Call-PlatformIO/wiki/Analysis-of-data-overhead:-MQTT-Keepalive-versus-TCP-KeepAlive)

# OpenHaystack BLE based Apple AirTag location

Part of what I want to do is to check how the cellular performs in different locations nationally. 

One idea I have for this is to post the device around and about and see how it performs, and in particular how reconnecting may influence the data overhead.

It occurred to me that the ESP32 has BLE and so it might be possible to spoof an Apple AirTag and leverage Apple's AirTag location network to track devices independently as I am monitoring the data-comms.

Rather wonderfully there is an ESP32 implementation of this called [OpenHaystack](https://github.com/seemoo-lab/openhaystack). The existing code is for ESP-IDF but I have ported this work here to build under Platform.io

I've also added a simple command `Custom->Write Public Key` which runs the Python code in publickey.b64 to set the needed tag advertising key into the ESP32.

The process you'd follow is

- Install and run OpenHaystack appliccation on a MAC
- Create a device
- Copy the device public advertising key
- Put this into the `publickey.b64` file here
- Write the firmware to the ESP32 (as this currently erases the key partition - need to look at this)
- Write the public key

After about half an hour - assuming iPhones are around - you should see the location pop up on the OpenHaystack application

Cheers!
