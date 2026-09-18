#!/usr/bin/env python3
import asyncio

from bleak import BleakClient, BleakScanner, BLEDevice, BleakGATTCharacteristic

from tqdm import tqdm
import questionary

from os.path import getsize
from math import ceil

OTA_PACKET_SIZE = 480
OTA_HANDSHAKE = b'>OTA12345678<'

debug = False

class BLEFirmwareUpdater:
    def __init__(self, firmware_filename, device_name, service_uuid, rx_uuid, tx_uuid):
        self.device_name = device_name
        self.service_uuid = service_uuid
        self.rx_uuid = rx_uuid
        self.tx_uuid = tx_uuid
        
        self.client = None
        
        self.writingFlag = True
        self.finishFlag = False
        
        self.write_char = None
        self.notify_char = None
        
        self.total_packet_count = ceil(getsize(firmware_filename) / OTA_PACKET_SIZE) - 1
        
        self.curr_packet = -1
        self.packet = OTA_HANDSHAKE
        self.packet_acknowledged = None
        
        self.firmware_file = open(firmware_filename, 'rb')
        self.progress_bar = None
        

    async def initialize_event_loop(self):
        self.packet_acknowledged = asyncio.Event()

    async def initialize_client(self):
        target_device = await self.find_device()
        if not target_device:
            return False

        self.client = BleakClient(target_device.address)
        try:
            await self.client.connect()
            await self.client.pair()
            print('\N{LINK SYMBOL} Connected to BLE device')

            self.write_char = self.client.services.get_characteristic(self.rx_uuid)
            self.notify_char = self.client.services.get_characteristic(self.tx_uuid)

            if debug:
                print(self.write_char.max_write_without_response_size)
                for service in self.client.services:
                    print(f"[Service] {service}")
                    for char in service.characteristics:
                        print(f"  [Characteristic] {char} ({','.join(char.properties)})")
                        for descriptor in char.descriptors:
                            print(f"    [Descriptor] {descriptor}")

            if not (self.write_char and self.notify_char):
                return False
            
            if debug:
                print(f'{",".join(self.notify_char.properties)}')

            await self.client.start_notify(self.notify_char, self.notification_handler)
            return True
        except Exception as e:
            print(f'\N{CROSS MARK} Failed to connect to BLE device: {e}')
            return False

    async def find_device(self) -> BLEDevice:
        print('\U0001f50d Scanning for BLE devices...')
        devices = await BleakScanner.discover(timeout=5.0, return_adv=True)

        query = questionary.select('Please, choose a device to flash.',
                                choices=[{'name': f'{d[0].name} (MAC: {d[0].address})', 'value': d[0]} for d in devices.values() if 'AgX' in d[0].name])

        return await asyncio.to_thread(query.ask)

        # for d in devices.values():
        #     if d[0].name: print(f'\t\N{WHITE CIRCLE} {d[0].name}')
        #     if d[0].name == self.device_name:
        #         print(f'\N{WHITE HEAVY CHECK MARK} Found target device {d[0].name} with address {d[0].address}')
        #         return d[0]



        print(f'\N{CROSS MARK} Device {self.device_name} not found. Make sure it\'s advertising.')

    async def write_firmware(self):
        while self.writingFlag:
            # if debug: print(f'Writing packet {self.curr_packet} : {self.packet}')

            # Reset the acknowledgment event, send and wait for ACK.
            self.packet_acknowledged.clear()
            await self.client.write_gatt_char(self.write_char.uuid, self.packet, response=False)
            if debug: print(f'Sent {self.curr_packet}')
            
            try:
                await asyncio.wait_for(self.packet_acknowledged.wait(), timeout=2)
            except asyncio.TimeoutError: pass

    def next_packet(self):
        chunk = self.firmware_file.read(OTA_PACKET_SIZE)
        
        if len(chunk) == 0:
            self.packet = b'>OTAFINISH<'
            self.finishFlag = True
        elif len(chunk) < OTA_PACKET_SIZE:
            chunk += b'\0' * (OTA_PACKET_SIZE - len(chunk))
            
        if not self.finishFlag:
            self.packet = f'>OTADATA,{self.curr_packet},{len(chunk)},'.encode('ascii') + chunk + b'<'

    def notification_handler(self, sender, data):
        text = data.decode('ascii', 'ignore')
        
        if debug: print(f'[AgX] {text}')
        
        if f'>OTAOK{self.curr_packet}<' in text or OTA_HANDSHAKE.decode('ascii') in text:
            self.curr_packet += 1
            self.progress_bar.update(1)
            self.next_packet()
            self.packet_acknowledged.set()
        elif f'>OTACURRENT{self.curr_packet}<' in text or '>OTAFAILED' in text:
            self.packet_acknowledged.set()
        elif '>OTAFINISH' in text:
            self.writingFlag = False
            self.packet_acknowledged.set()

    async def stress_data(self):
        while self.writingFlag:
            await self.client.write_gatt_char(self.write_char, '>QUS07<'.encode('ascii'), response=False)
            print('>QUS07<')
            await asyncio.sleep(2)

    async def update_firmware(self, concurrent_data_stress = False):
        await self.initialize_event_loop()
        if not await self.initialize_client():
            return

        if concurrent_data_stress: asyncio.create_task(self.stress_data())

        try:
            print('\N{ROCKET} Starting firmware update...')
            self.progress_bar = tqdm(total=self.total_packet_count)
            await self.write_firmware()
            await self.client.write_gatt_char(self.write_char, b'>ESPRESTART<', response=False)
            if debug: print('>ESPRESTART<')

            # Keep listening for notifications for a bit
            await asyncio.sleep(10.0)  # Wait to receive any echoes or other data
        finally:
            await self.client.disconnect()
            self.progress_bar.close()
            self.firmware_file.close()
            print('\N{ELECTRIC PLUG} Disconnected from BLE device')

if __name__ == '__main__':
    print('\N{floppy disk} BLE-OTA Tool')
    print('\N{round pushpin} \033[32mAgX\033[m')
    
    async def main():
        DEVICE_NAME = 'AgX 1.0.45-OTA-Sync'
        
        NORDIC_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
        NORDIC_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e' #'6e400002-b5a3-f393-e0a9-e50e24dcca9e'
        NORDIC_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e' #'6e400003-b5a3-f393-e0a9-e50e24dcca9e'
    
        updater = BLEFirmwareUpdater('16mb_1.0.46-OTA-Sync.bin', DEVICE_NAME, NORDIC_SERVICE_UUID, NORDIC_RX_UUID, NORDIC_TX_UUID)
        await updater.update_firmware(debug)

    try:
        asyncio.run(main())
    except KeyboardInterrupt: print('\N{HEAVY EXCLAMATION MARK SYMBOL} OTA update aborted by user.')
