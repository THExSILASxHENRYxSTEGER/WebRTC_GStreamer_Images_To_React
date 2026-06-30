from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from typing import Dict, Set, List
import asyncio

app = FastAPI()

clients: Dict[int, WebSocket] = {}
registrations: Dict[int, Set[int]] = {}
pending: Dict[int, List[dict]] = {}  # target_id -> queued messages

async def deliver(target_id: int, data: dict):
    if target_id in clients:
        try:
            await clients[target_id].send_json(data)
            return True
        except Exception:
            pass
    # queue it
    pending.setdefault(target_id, []).append(data)
    print(f"QUEUED {data.get('type')} for {target_id}")
    return False

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    ws_id = id(ws)
    registrations[ws_id] = set()
    print("WS connected")

    try:
        while True:
            data = await ws.receive_json()
            msg_type = data.get("type")
            peer_id  = data.get("id")

            if msg_type == "register":
                if peer_id is None:
                    continue
                clients[peer_id] = ws
                registrations[ws_id].add(peer_id)
                print(f"Registered {peer_id}")

                # flush any queued messages for this peer
                if peer_id in pending:
                    print(f"Flushing {len(pending[peer_id])} queued messages to {peer_id}")
                    for queued in pending.pop(peer_id):
                        try:
                            await ws.send_json(queued)
                        except Exception:
                            pass
                continue

            if peer_id is None:
                continue

            if peer_id not in clients:
                clients[peer_id] = ws
                registrations[ws_id].add(peer_id)
                print(f"Auto-registered {peer_id}")

            target = data.get("target_id")

            if target is not None:
                print(f"{peer_id} -> {target} ({msg_type})")
                await deliver(target, data)
                continue

            for pid, client in list(clients.items()):
                if pid != peer_id:
                    try:
                        await client.send_json(data)
                    except Exception:
                        pass

    except WebSocketDisconnect:
        print("WS disconnected")
    finally:
        ids = registrations.pop(ws_id, set())
        for pid in ids:
            clients.pop(pid, None)
            print(f"Removed {pid}")