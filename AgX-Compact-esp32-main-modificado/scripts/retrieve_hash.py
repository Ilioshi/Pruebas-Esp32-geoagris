#!/usr/bin/env python3
import asyncio
from bleak import BleakClient, BleakScanner, BLEDevice
from typing import Optional

class BLEFirmwareHashRetriever:
    def __init__(self, device_name: str, service_uuid: str, rx_uuid: str, tx_uuid: str,
                 command: str = ">ESPFIRMWAREHASH<"):
        self.device_name = device_name
        self.service_uuid = service_uuid
        self.rx_uuid = rx_uuid  # Write characteristic UUID
        self.tx_uuid = tx_uuid  # Notify characteristic UUID
        self.command = command

        self.firmware_hash: Optional[str] = None
        self._hash_event = asyncio.Event()

    async def _notification_handler(self, sender: int, data: bytearray):
        text = data.decode("ascii", "ignore")
        print(f"[Notification] Received: {text}")
        # Expected format: ">ESPFIRMWAREHASH,{sha256hex}<"
        if text.startswith(">ESPFIRMWAREHASH,") and text.endswith("<"):
            prefix = ">ESPFIRMWAREHASH,"
            suffix = "<"
            self.firmware_hash = text[len(prefix):-len(suffix)]
            self._hash_event.set()

    async def _find_device(self) -> Optional[BLEDevice]:
        print("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=5.0)
        for device in devices:
            if device.name and self.device_name in device.name:
                print(f"Found device: {device.name} ({device.address})")
                return device
        return None

    async def retrieve_hash(self, timeout: int = 10) -> Optional[str]:
        device = await self._find_device()
        if device is None:
            print("Target device not found.")
            return None

        tx_char = None  # Initialize before the try block
        async with BleakClient(device.address) as client:
            try:
                # No need to call client.connect() explicitly here.
                print("Connected to device.")
                
                services = client.services
                rx_char = services.get_characteristic(self.rx_uuid)
                tx_char = services.get_characteristic(self.tx_uuid)
                if rx_char is None or tx_char is None:
                    print("Required characteristics not found.")
                    return None

                # Start notifications on the notify characteristic.
                await client.start_notify(tx_char, self._notification_handler)
                print("Notifications started. Sending command...")

                # Write the command to the write characteristic.
                await client.write_gatt_char(rx_char, self.command.encode("ascii"), response=False)

                # Wait until the hash is received or timeout expires.
                await asyncio.wait_for(self._hash_event.wait(), timeout=timeout)
                return self.firmware_hash
            except asyncio.TimeoutError:
                print("Timeout waiting for firmware hash response.")
                return None
            except Exception as e:
                print(f"Exception: {e}")
                return None
            finally:
                if tx_char is not None and client.is_connected:
                    await client.stop_notify(tx_char)

async def main():
    # Replace these UUIDs and device name with your actual values.
    DEVICE_NAME = "AgX"  # e.g., "AgX 1.0.56-OTA-Sync"
    SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
    RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
    TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

    retriever = BLEFirmwareHashRetriever(DEVICE_NAME, SERVICE_UUID, RX_UUID, TX_UUID)
    firmware_hash = await retriever.retrieve_hash(timeout=10)
    if firmware_hash:
        print(f"Firmware SHA256: {firmware_hash}")
    else:
        print("Failed to retrieve firmware hash.")

if __name__ == "__main__":
    asyncio.run(main())
