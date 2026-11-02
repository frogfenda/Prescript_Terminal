"""Apply the terminal's explicit re-pairing policy to NimBLE-Arduino 1.4.x."""

_DEFAULT_REPEAT_PAIRING = """            /* We already have a bond with the peer, but it is attempting to
             * establish a new secure link.  This app sacrifices security for
             * convenience: just throw away the old bond and accept the new link.
             */

            /* Delete the old bond. */
            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            if (rc != 0){
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }

            ble_store_util_delete_peer(&desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
             * continue with the pairing operation.
             */
            return BLE_GAP_REPEAT_PAIRING_RETRY;"""

_EXPLICIT_REPAIR_ONLY = """            // 只有用户从终端设置显式开启配对后才允许建立新密钥。
            // 设备配对流程会先清除旧 bond；普通连接遇到重复配对直接拒绝。
            return BLE_GAP_REPEAT_PAIRING_IGNORE;"""


def apply_repeat_pairing_policy(source: str) -> str:
    if _EXPLICIT_REPAIR_ONLY in source:
        return source
    count = source.count(_DEFAULT_REPEAT_PAIRING)
    if count != 1:
        raise ValueError(
            "Unexpected NimBLEServer.cpp repeat-pairing handler; refusing an unsafe patch"
        )
    return source.replace(_DEFAULT_REPEAT_PAIRING, _EXPLICIT_REPAIR_ONLY, 1)
