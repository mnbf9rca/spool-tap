# op-template: VAR=op://Vault/item/field only, never a value. Resolved per
# command by `op run --env-file=.env.tpl -- <cmd>`.
WIFI_SSID=op://spool-tap/wifi/ssid
WIFI_PASSWORD=op://spool-tap/wifi/ssid-password
OTA_PASSWORD=op://spool-tap/device/password
