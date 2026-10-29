"""Pull log files off the BootBoots catcam's SD card over BLE (list_logs + get_log_file).

Usage: catcam_logs.py OUT_DIR [--list] [FILENAME ...]
With no filenames, downloads every log file the device lists.
"""
import asyncio
import json
import os
import sys

from bleak import BleakClient, BleakScanner

NAME = "BootBoots-CatCam"
COMMAND_UUID = "bb00b007-c0de-49fa-89c5-31e705b74d87"
IDLE_TIMEOUT = 180  # seconds with no notification before giving up on a file


class Session:
    def __init__(self):
        self.queue: asyncio.Queue = asyncio.Queue()

    def on_notify(self, _char, data: bytearray):
        text = data.decode("utf-8", "replace")
        try:
            self.queue.put_nowait(json.loads(text))
        except json.JSONDecodeError:
            self.queue.put_nowait({"type": "_unparsed", "raw": text})

    async def next(self):
        return await asyncio.wait_for(self.queue.get(), IDLE_TIMEOUT)


async def send(client, cmd: dict):
    await client.write_gatt_char(COMMAND_UUID, json.dumps(cmd).encode(), response=True)


async def list_logs(client, s: Session) -> list[str]:
    await send(client, {"command": "list_logs"})
    files = []
    while True:
        m = await s.next()
        t = m.get("type")
        if t == "log_list_chunk":
            files.append(m["filename"])
        elif t == "log_list_complete":
            return files
        elif t == "log_list_debug":
            print("device:", m.get("message"), flush=True)


async def get_file(client, s: Session, name: str, out_dir: str):
    await send(client, {"command": "get_log_file", "filename": name})
    path = os.path.join(out_dir, os.path.basename(name))
    lines = 0
    with open(path + ".partial", "w") as f:
        while True:
            m = await s.next()
            t = m.get("type")
            if t == "log_file_chunk":
                f.write(m["data"].rstrip("\n") + "\n")
                lines += 1
                if lines % 500 == 0:
                    print(f"  {name}: {lines} lines", flush=True)
            elif t == "log_file_complete":
                break
            elif t == "_unparsed":
                f.write("# UNPARSED NOTIFICATION: " + m["raw"] + "\n")
            # other message types (status pushes etc.) are ignored
    os.replace(path + ".partial", path)
    print(f"{name}: {lines} lines -> {path}", flush=True)


async def main():
    args = sys.argv[1:]
    out_dir = args.pop(0)
    list_only = "--list" in args
    wanted = [a for a in args if a != "--list"]
    os.makedirs(out_dir, exist_ok=True)

    print(f"Scanning for {NAME}...", flush=True)
    dev = await BleakScanner.find_device_by_name(NAME, timeout=20)
    if not dev:
        sys.exit(f"{NAME} not found (is the browser still connected to it?)")
    s = Session()
    async with BleakClient(dev, timeout=30) as client:
        print(f"Connected, MTU {client.mtu_size}", flush=True)
        await client.start_notify(COMMAND_UUID, s.on_notify)
        files = await list_logs(client, s)
        print(f"{len(files)} log files:", *files, sep="\n  ", flush=True)
        if list_only:
            return
        for name in wanted or files:
            await get_file(client, s, name, out_dir)


asyncio.run(main())
