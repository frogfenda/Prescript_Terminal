# Terminal Protocol Notes

V1 keeps the firmware's text command protocol intact.

## Client to Device

```text
GET:LANG
GET:SYNC:ZH
GET:SPC_TXT:Rien
TXT:<text>
SPC:<id>
PRE:ZH:<text>
PRE_DEL:ZH:<text>
ALM:<hh>:<mm>:<name>:<text>
ALM_DEL:<name>
SCH:<yyyy>:<mo>:<d>:<hh>:<mm>:<title>:<text>
SCH_DEL:<title>
POM:<slot>:<name>:<work_min>:<rest_min>
COIN:<base>:<coin>:<count>:<colors>:<name>
COIN_DEL:<name>
WIFI:<ssid>:<password>
```

## Device to Client

```text
LANG:ZH:RUNTIME:<build>
ACK:OK:PRE:ADDED
ACK:WARN:ALM:UPDATED
ACK:ERR:SCH:SCH_INVALID_TIME
SYNC:CLEAR
SYNC:ALM:{...}
SYNC:SCH:{...}
SYNC:PRE:{...}
SYNC:COIN:{...}
SPC_META:C|Rien|...|...
SPC_TXT:Rien|...
```

The app treats BLE as one transport. Later cloud delivery should reuse the same repository/protocol concepts rather than pushing raw strings from UI widgets.
