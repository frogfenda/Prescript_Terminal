# Prescript Terminal Mobile App

Flutter client for the ESP32 Prescript Terminal.

This app intentionally reuses the current firmware protocol:

- BLE device name: `Terminal_01`
- Service UUID: `0000dead-0000-1000-8000-00805f9b34fb`
- Characteristic UUID: `0000beef-0000-1000-8000-00805f9b34fb`

## Current Scope

V1 is an Android-first replacement for the existing WebBLE terminal:

- BLE connect, write, notify
- `GET:LANG`, `GET:SYNC`
- `TXT`, `PRE`, `ALM`, `SCH`, `POM`, `COIN`, `WIFI`, `SPC`
- Special directive metadata and text fetch
- Cloud archive browsing and random injection

The UI already reserves space for future device IDs, remote targets, user IDs, inbox/outbox, and cloud delivery.

## Setup

This repository currently contains Flutter source code only. If `flutter` is not installed yet:

1. Install Flutter and Android tooling.
2. From this directory, run `flutter create . --platforms=android`.
3. Run `flutter pub get`.
4. Run on an Android phone with BLE enabled.

Android BLE permissions may need to be checked after platform files are generated.

## Android BLE Permissions

After `flutter create . --platforms=android`, check `android/app/src/main/AndroidManifest.xml`.
For Android 12+ BLE scanning/connection, the app usually needs:

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

Some Android versions still require location services to be enabled for BLE scanning.

## Firmware Compatibility

The current V1 app does not require firmware changes. A later device-ID version should add a command such as:

```text
GET:INFO
INFO:PT-8F3A-19C2|Terminal_01|ZH|RUNTIME|fw=0.1.0|caps=TXT,PRE,ALM,SCH,COIN,SPC,NFC
```

The UI and models already reserve space for `deviceId`, `userId`, remote delivery, inbox/outbox, and cloud queues.
