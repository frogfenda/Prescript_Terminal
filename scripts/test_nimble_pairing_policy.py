import sys
import unittest

sys.dont_write_bytecode = True

from nimble_pairing_policy import apply_repeat_pairing_policy


class NimblePairingPolicyTest(unittest.TestCase):
    def test_default_handler_is_changed_to_ignore_without_deleting_bond(self):
        source = """        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            /* We already have a bond with the peer, but it is attempting to
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
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        } // BLE_GAP_EVENT_REPEAT_PAIRING"""
        patched = apply_repeat_pairing_policy(source)
        self.assertIn("return BLE_GAP_REPEAT_PAIRING_IGNORE;", patched)
        self.assertNotIn("ble_store_util_delete_peer", patched)
        self.assertNotIn("return BLE_GAP_REPEAT_PAIRING_RETRY;", patched)
        self.assertEqual(apply_repeat_pairing_policy(patched), patched)

    def test_unknown_dependency_handler_fails_closed(self):
        with self.assertRaises(ValueError):
            apply_repeat_pairing_policy("case BLE_GAP_EVENT_REPEAT_PAIRING: {")


if __name__ == "__main__":
    unittest.main()
