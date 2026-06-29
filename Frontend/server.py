from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from typing import Dict, Set

app = FastAPI()

# peer_id -> websocket
clients: Dict[int, WebSocket] = {}

# websocket -> set(peer_ids)
registrations: Dict[WebSocket, Set[int]] = {}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()

    print("New websocket connected")

    registrations[ws] = set()

    try:
        while True:
            data = await ws.receive_json()

            msg_type = data.get("type")
            peer_id = data.get("id")

            #
            # Registration message
            #
            if msg_type == "register":

                if peer_id is None:
                    continue

                clients[peer_id] = ws
                registrations[ws].add(peer_id)

                print(f"Registered peer {peer_id}")

                continue

            #
            # Ignore malformed packets
            #
            if peer_id is None:
                continue

            #
            # If a peer wasn't explicitly registered,
            # register it automatically.
            #
            if peer_id not in clients:
                clients[peer_id] = ws
                registrations[ws].add(peer_id)

                print(f"Auto-registered peer {peer_id}")

            #
            # Directed packet
            #
            target = data.get("target_id")

            if target is not None:

                if target in clients:

                    print(
                        f"{peer_id} -> {target} ({msg_type})"
                    )

                    await clients[target].send_json(data)

                else:

                    print(
                        f"Dropping {msg_type}: target {target} not connected"
                    )

                continue

            #
            # Broadcast (typically Answer / ICE from browser)
            #
            for pid, client in list(clients.items()):

                if pid == peer_id:
                    continue

                try:
                    await client.send_json(data)
                except Exception:
                    pass

    except WebSocketDisconnect:

        print("Websocket disconnected")

    finally:

        #
        # Remove every peer owned by this websocket
        #
        ids = registrations.pop(ws, set())

        for pid in ids:
            clients.pop(pid, None)
            print(f"Removed peer {pid}")